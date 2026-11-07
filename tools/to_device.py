#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""to_device.py —— 把 resources/Data 里的 loose .img 资源用 adb 同步到指定设备。

用法：
    to_device.py <设备ID>                         # 增量：按 md5 比对，改哪个/新增哪个就同步哪个
    to_device.py <设备ID> <img路径> [<img路径>…]   # 只同步指定的资源文件（同样先比 md5，已一致则跳过）

设备落点固定为 /sdcard/Android/data/com.OpenMapleClient/files/Data/<相对路径>，
和 resources/Data 目录结构一一对应。推送一律走 /data/local/tmp 中转再 cp+chmod 666：
直接 adb push 覆盖已存在文件会 remote fchown failed 并把目标删掉。同步后回读设备 md5 校验。

开头会先把资源重编码成标准 IV：外部美术编辑器（WzComparerR2 系）导出的 loose img 是零
keystream（IV=00000000），而客户端 imgwz 解头写死区域 IV 0x4D23C72B，推上去就是进图黑屏。
发现这种文件就地重编码（原件备份进 resources/Backups/reencode/），并把同一份结果覆盖到
Assets/v209/img_develop 与 Assets/v209/img 两份基线，保持三份 md5 一致。
"""
import hashlib
import os
import shutil
import subprocess
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ADB = r"D:/AndroidDependency/Sdk/platform-tools/adb.exe"
DATA = r"F:/Code/MapleAndroid/MapleClientAndroid/resources/Data"
DEV_ROOT = "/sdcard/Android/data/com.OpenMapleClient/files/Data"
HEX = set("0123456789abcdef")

if not os.path.exists(ADB):
    ADB = shutil.which("adb") or ADB

from imgcheck import client_readable
import img_reencode

BASELINES = [r"F:/Code/MapleAndroid/Assets/v209/img_develop",
             r"F:/Code/MapleAndroid/Assets/v209/img"]


def repair_iv(jobs):
    """推送前的第一步：把 IV=0 的资源就地重编码成标准 IV，并跟齐两份基线。

    返回重编码后仍然解不开的文件 [(绝对路径, 原因)]，这些才真的拒绝推送。"""
    still_bad = []
    for rel, lp in jobs:
        ok, why = client_readable(lp)
        if ok:
            continue
        print("  检测到非标准 IV，先重编码：%s" % rel)
        print("    %s" % img_reencode.reencode(lp, False))
        ok, why = client_readable(lp)
        if not ok:
            still_bad.append((lp, why))
            continue
        for base in BASELINES:
            dst = os.path.join(base, rel.replace("/", os.sep))
            if os.path.isfile(dst):
                shutil.copy2(lp, dst)
                print("    跟齐基线：%s" % dst)
    return still_bad


def run(args):
    return subprocess.run(args, capture_output=True, encoding="utf-8", errors="replace")


def adb(dev, *cmd):
    return run([ADB, "-s", dev] + list(cmd))


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_device(dev):
    out = run([ADB, "devices"])
    for line in out.stdout.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2 and parts[0] == dev and parts[1] == "device":
            return True
    return False


def device_md5_map(dev):
    """一次 find+md5sum 拿回整棵设备树的 rel->md5，避免逐文件往返。"""
    res = adb(dev, "shell", "cd '%s' && find . -type f -name '*.img' -exec md5sum {} +" % DEV_ROOT)
    m = {}
    for line in res.stdout.splitlines():
        line = line.rstrip()
        if len(line) < 34 or line[32] not in (" ", "\t"):
            continue
        digest, path = line[:32], line[33:].strip()
        if digest and set(digest) <= HEX:
            rel = path[2:] if path.startswith("./") else path
            m[rel] = digest
    return m


def dev_md5_one(dev, rel):
    res = adb(dev, "shell", "md5sum '%s/%s' 2>/dev/null" % (DEV_ROOT, rel))
    got = res.stdout.strip()
    return got[:32] if len(got) >= 32 and set(got[:32]) <= HEX else None


def push_one(dev, local, rel):
    """中转推送：mkdir -p 父目录(777) → push 到 /data/local/tmp → cp+chmod 666 → 回读校验。"""
    dev_path = "%s/%s" % (DEV_ROOT, rel)
    parent = dev_path.rsplit("/", 1)[0]
    tmp = "/data/local/tmp/to_device_%s.img" % uuid.uuid4().hex[:12]
    adb(dev, "shell", "mkdir -p '%s' && chmod 777 '%s'" % (parent, parent))
    r = adb(dev, "push", local, tmp)
    if r.returncode != 0:
        return False, "push 失败: " + (r.stderr.strip() or r.stdout.strip())
    r = adb(dev, "shell", "cp '%s' '%s' && chmod 666 '%s' && rm -f '%s'"
            % (tmp, dev_path, dev_path, tmp))
    if r.returncode != 0:
        adb(dev, "shell", "rm -f '%s'" % tmp)
        return False, "cp 失败: " + (r.stderr.strip() or r.stdout.strip())
    got = dev_md5_one(dev, rel)
    want = md5(local)
    if got != want:
        return False, "回读校验不符: 设备=%s 期望=%s" % (got, want)
    return True, ""


def resolve_targets(paths):
    """把命令行给出的 img 路径解析成 (rel, local_abs)，忽略不在 DATA 下或非 .img 的项。"""
    jobs = []
    for t in paths:
        ap = os.path.abspath(t.replace("\\", "/"))
        rel = os.path.relpath(ap, DATA)
        if rel.startswith(".."):
            print("  跳过（不在 resources/Data 下）: %s" % t)
            continue
        if not ap.endswith(".img"):
            print("  跳过（不是 .img）: %s" % t)
            continue
        if not os.path.isfile(ap):
            print("  跳过（本地文件不存在）: %s" % t)
            continue
        jobs.append((rel.replace(os.sep, "/"), ap))
    return jobs


def delta_targets(dev):
    """全树增量：本地 md5 与设备不一致或设备缺失的文件。"""
    dmap = device_md5_map(dev)
    jobs = []
    for root, _, names in os.walk(DATA):
        for n in names:
            if not n.endswith(".img"):
                continue
            lp = os.path.join(root, n)
            rel = os.path.relpath(lp, DATA).replace(os.sep, "/")
            if dmap.get(rel) != md5(lp):
                jobs.append((rel, lp))
    return jobs


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    dev = sys.argv[1]
    explicit = sys.argv[2:]

    if not os.path.exists(ADB):
        print("找不到 adb：%s" % ADB)
        sys.exit(1)
    if not check_device(dev):
        print("设备 %s 不在线（adb devices 里没有该 ID 或状态不是 device）" % dev)
        sys.exit(1)

    if explicit:
        jobs = resolve_targets(explicit)
    else:
        jobs = delta_targets(dev)

    # 指定文件模式也先比一次 md5，已一致的跳过，保持幂等
    kept = []
    for rel, lp in jobs:
        if dev_md5_one(dev, rel) == md5(lp):
            continue
        kept.append((rel, lp))
    jobs = kept

    # 第一步：IV=0 的资源就地重编码成标准 IV（不重编码推上去就是进图黑屏）
    bad = repair_iv(jobs)
    if bad:
        print("以下 %d 个文件重编码后客户端 imgwz 仍解不开，已拒绝推送：" % len(bad))
        for ap, why in bad:
            print("  %s : %s" % (ap, why))
        sys.exit(1)

    if not jobs:
        print("设备 %s：没有需要同步的文件（已全部一致）" % dev)
        sys.exit(0)

    print("设备 %s：待同步 %d 个文件" % (dev, len(jobs)))
    ok = fail = 0
    for i, (rel, local) in enumerate(jobs, 1):
        good, err = push_one(dev, local, rel)
        if good:
            ok += 1
            print("  [%d/%d] 同步 %s" % (i, len(jobs), rel))
        else:
            fail += 1
            print("  [%d/%d] 失败 %s : %s" % (i, len(jobs), rel, err))
    print("完成：成功 %d，失败 %d" % (ok, fail))
    sys.exit(1 if fail else 0)


if __name__ == "__main__":
    main()
