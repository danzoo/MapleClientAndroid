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
        dest = os.path.join(data_dir, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copy2(ap, dest)
        staged.append(dest)
        cats.add(rel.replace(os.sep, "/").split("/", 1)[0])
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
    n = 0
    for root, _, names in os.walk(xml_out):
        for fn in names:
            if not fn.endswith(".img.xml"):
                continue
            src = os.path.join(root, fn)
            rel = os.path.relpath(src, xml_out)
            dst = os.path.join(to_root, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            n += 1
    return n


def main():
    t0 = time.time()
    to_root, files = parse_args(sys.argv[1:])

    if not os.path.isfile(IMG2WZ) or not os.path.isfile(WZ2XML):
        raise SystemExit("找不到转换工具：%s / %s" % (IMG2WZ, WZ2XML))
    os.makedirs(to_root, exist_ok=True)

    cleanup = None
    if files:
        stage, data_dir, cats, check = stage_selected(files)
        cleanup = stage
        print("选中 %d 个 img，涉及分类：%s" % (len(files), ",".join(cats)))
    else:
        stage = tempfile.mkdtemp(prefix="to_server_all_")
        cleanup = stage
        data_dir = DATA            # 全量：直接读 Data，img2wz 逐分类产 wz
        print("全量同步：整棵 resources/Data -> %s" % to_root)
        check = [os.path.join(root, n)
                 for root, _, names in os.walk(DATA) for n in names if n.endswith(".img")]

    try:
        # 转换前先验 IV：外部编辑器导出的 IV=0 img 在 img2wz 那步会静默产出错树
        bad = bad_paths(check)
        if bad:
            report(bad, '转换')
            sys.exit(1)
        xml_out = convert(data_dir, stage)
        n = publish(xml_out, to_root)
        print("完成：写入 %d 个 XML 到 %s，用时 %.1fs" % (n, to_root, time.time() - t0))
    finally:
        if cleanup:
            shutil.rmtree(cleanup, ignore_errors=True)


if __name__ == "__main__":
    main()
