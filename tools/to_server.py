#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""to_server.py —— 把 resources/Data 里的 loose .img 转成服务端 assets/wz 的 XML 并同步。

用法：
    to_server.py                          # 全量：整棵 Data 转成 XML，覆盖 assets/wz 对应文件
    to_server.py <img路径> [<img路径>…]   # 只转指定 .img

路径映射与 assets/wz 逐路径一致：
    resources/Data/<分类>/<相对>/<名>.img  ->  assets/wz/<分类>.wz/<相对>/<名>.img.xml

转换走项目既定链路：img2wz（loose img -> PKG1 wz，IV=ems/版本 83）-> wz2xml（wz -> XML 目录树，
只导元数据）。可选 --to <目录> 把产物写到别处（干跑/校验用），默认写进仓库的 assets/wz。

服务端排除清单（SERVER_SKIP，2026-11 按 server-core 全量静态引用分析定下）：
这些 XML 服务端代码永远不会打开，转换时被跳过；全量同步还会把目标树里已存在的这些文件
以及「本地 Data 已没有对应 img」的孤儿 XML 一并删掉，删图后不用再手工清服务端。
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = HERE                                   # img2wz.py / wz2xml.py 及其依赖都在本目录
IMG2WZ = os.path.join(TOOLS, "img2wz.py")
WZ2XML = os.path.join(TOOLS, "wz2xml.py")
DATA = r"F:/Code/MapleAndroid/MapleClientAndroid/resources/Data"
SERVER_WZ = r"F:/Code/MapleAndroid/MapleServerAndroid/app/src/main/assets/wz"
PY = sys.executable
JOBS = "6"

sys.path.insert(0, HERE)
from imgcheck import bad_paths, report

# 服务端不读的 XML（2026-11 按 server-core 全部 getData/WZFiles 引用静态分析定下）。
# rel 形如 <分类>.wz/<相对>/<名>.img.xml。
SKIP_DIRS = ("Base.wz", "Effect.wz", "Morph.wz", "Sound.wz", "TamingMob.wz")   # 整归档零引用
SKIP_SUBTREES = ("Map.wz/Back", "Map.wz/Tile", "Map.wz/WorldMap")  # 运行时零引用；Obj 启动全扫，保留
KEEP_FILES = {  # 这几个分类只有列出的文件会被打开，其余一律不算在用
    "UI.wz": {"UIWindow.img.xml", "UIWindow2.img.xml"},
    "String.wz": {"Cash.img.xml", "Consume.img.xml", "Eqp.img.xml", "Etc.img.xml", "Ins.img.xml",
                  "Map.img.xml", "Mob.img.xml", "Npc.img.xml", "Pet.img.xml", "Skill.img.xml"},
    "Etc.wz": {"CashPackage.img.xml", "Commodity.img.xml", "ItemMake.img.xml",
               "MakeCharInfo.img.xml", "MapNeighbors.img.xml", "OXQuiz.img.xml"},
    "Quest.wz": {"Act.img.xml", "Check.img.xml", "QuestInfo.img.xml"},
}


def skipped(rel):
    """rel 用正斜杠，形如 Map.wz/Back/xxx.img.xml。"""
    top, _, rest = rel.partition("/")
    if top in SKIP_DIRS:
        return True
    if any(rel.startswith(d + "/") for d in SKIP_SUBTREES):
        return True
    keep = KEEP_FILES.get(top)
    return keep is not None and rest not in keep


def xml_rel(data_rel):
    """resources/Data 相对路径 -> assets/wz 相对路径（x.img -> x.img.xml）。"""
    head, _, rest = data_rel.replace(os.sep, "/").partition("/")
    return head + ".wz/" + rest + ".xml"


def run(cmd):
    print("  $ " + " ".join(cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, encoding="utf-8", errors="replace", cwd=TOOLS)
    tail = (r.stdout or "").strip().splitlines()
    for line in tail[-6:]:
        print("    " + line)
    if r.returncode != 0:
        sys.stderr.write(r.stderr or "")
        raise SystemExit("命令失败：%s" % " ".join(cmd))
    return r


def parse_args(argv):
    to = SERVER_WZ
    files = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--to", "-t"):
            i += 1
            to = os.path.abspath(argv[i])
        else:
            files.append(a)
        i += 1
    return to, files


def stage_selected(paths):
    """把选中的 img 按相对路径拷进一个临时 Data 树，img2wz 只按里面出现的分类产 wz。"""
    stage = tempfile.mkdtemp(prefix="to_server_sel_")
    data_dir = os.path.join(stage, "Data")
    cats = set()
    staged = []
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
        rel = rel.replace(os.sep, "/")
        if skipped(xml_rel(rel)):
            print("  跳过（服务端排除清单里，本来就不转）: %s" % t)
            continue
        dest = os.path.join(data_dir, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copy2(ap, dest)
        staged.append(dest)
        cats.add(rel.split("/", 1)[0])
    if not staged:
        shutil.rmtree(stage, ignore_errors=True)
        raise SystemExit("没有可转换的 .img")
    return stage, data_dir, sorted(cats), staged


def convert(data_dir, stage):
    wz_out = os.path.join(stage, "wz")
    xml_out = os.path.join(stage, "xml")
    os.makedirs(wz_out, exist_ok=True)
    os.makedirs(xml_out, exist_ok=True)
    run([PY, IMG2WZ, data_dir, "-o", wz_out, "--iv", "ems", "--version", "83", "--jobs", JOBS])
    run([PY, WZ2XML, wz_out, "-o", xml_out, "--iv", "ems", "--version", "83", "--jobs", JOBS])
    return xml_out


def publish(xml_out, to_root):
    n = skip = 0
    for root, _, names in os.walk(xml_out):
        for fn in names:
            if not fn.endswith(".img.xml"):
                continue
            src = os.path.join(root, fn)
            rel = os.path.relpath(src, xml_out).replace(os.sep, "/")
            if skipped(rel):
                skip += 1
                continue
            dst = os.path.join(to_root, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            n += 1
    return n, skip


def sweep(to_root, expected):
    """全量模式发布后的镜像清理：删掉排除清单残留 + Data 已无对应 img 的孤儿 XML，并收空目录。"""
    removed = held = 0
    dirs = set()
    for root, _, names in os.walk(to_root):
        for fn in names:
            if not fn.endswith(".img.xml"):
                continue
            p = os.path.join(root, fn)
            rel = os.path.relpath(p, to_root).replace(os.sep, "/")
            dirs.add(root)
            if rel in expected and not skipped(rel):
                continue
            try:
                os.remove(p)
                removed += 1
            except OSError:
                held += 1
                print("  删不掉（可能正被运行中的服务端占用）: %s" % rel)
    for d in sorted(dirs, key=lambda x: -x.count(os.sep)):
        try:
            os.rmdir(d)
        except OSError:
            pass
    return removed, held


def main():
    t0 = time.time()
    to_root, files = parse_args(sys.argv[1:])

    if not os.path.isfile(IMG2WZ) or not os.path.isfile(WZ2XML):
        raise SystemExit("找不到转换工具：%s / %s" % (IMG2WZ, WZ2XML))
    os.makedirs(to_root, exist_ok=True)

    cleanup = None
    expected = None
    if files:
        stage, data_dir, cats, check = stage_selected(files)
        cleanup = stage
        print("选中 %d 个 img，涉及分类：%s" % (len(files), ",".join(cats)))
    else:
        stage = tempfile.mkdtemp(prefix="to_server_all_")
        cleanup = stage
        data_dir = DATA            # 全量：直接读 Data，img2wz 逐分类产 wz
        print("全量同步：整棵 resources/Data -> %s" % to_root)
        check = []
        expected = set()
        for root, _, names in os.walk(DATA):
            for n in names:
                if not n.endswith(".img"):
                    continue
                p = os.path.join(root, n)
                check.append(p)
                expected.add(xml_rel(os.path.relpath(p, DATA)))

    try:
        # 转换前先验 IV：外部编辑器导出的 IV=0 img 在 img2wz 那步会静默产出错树
        bad = bad_paths(check)
        if bad:
            report(bad, '转换')
            sys.exit(1)
        xml_out = convert(data_dir, stage)
        n, skip = publish(xml_out, to_root)
        if expected is not None:
            removed, held = sweep(to_root, expected)
            print("完成：写入 %d 个 XML（排除清单跳过 %d），清理 %d 个残留/孤儿"
                  "（删不掉 %d），用时 %.1fs" % (n, skip, removed, held, time.time() - t0))
        else:
            print("完成：写入 %d 个 XML 到 %s（排除清单跳过 %d），用时 %.1fs"
                  % (n, to_root, skip, time.time() - t0))
    finally:
        if cleanup:
            shutil.rmtree(cleanup, ignore_errors=True)


if __name__ == "__main__":
    main()
