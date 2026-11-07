#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""img_reencode.py —— 把外部编辑器导出的 IV=0 loose .img 重编码成客户端能读的 GMS IV。

用法：
    img_reencode.py <img路径> [<img路径>…] [--dry-run]

外部美术编辑器（WzComparerR2 系）保存 loose img 时用的是**零 keystream**（IV=00000000），
而客户端 imgwz 解头写死区域 IV 0x4D23C72B（img_internal.hpp:163），于是 logcat 报
「header is not Property/0」、整档放弃 —— 表现就是进图黑屏。本脚本把属性树原样搬进标准 IV
容器：读旧文件 -> digest 记录 -> 写标准 IV -> 重新读回比 digest，一致才落地。

原始文件先备份到 MapleClientAndroid/resources/Backups/reencode/<时间戳>/<相对路径>
（Backups 已被 .gitignore 排除，不上库）。
"""
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import img2wz as I              # read_block / SRC_IV
import imgcut                   # write_img / digest

DATA = r'F:/Code/MapleAndroid/MapleClientAndroid/resources/Data'
BACKUPS = r'F:/Code/MapleAndroid/MapleClientAndroid/resources/Backups'
ZERO_IV = bytes(4)


def iv_of(head):
    """判断一个 img 头部用的是哪套 IV：'std' / 'zero' / None。"""
    for iv, tag in ((I.SRC_IV, 'std'), (ZERO_IV, 'zero')):
        try:
            r = I.Img(head, iv)
            if r.u8() == 0x73 and r.string() == 'Property':
                return tag
        except Exception:
            pass
    return None


def rel_of(path):
    ap = os.path.abspath(path.replace('\\', '/'))
    rel = os.path.relpath(ap, DATA)
    if rel.startswith('..'):
        return None
    return rel.replace(os.sep, '/')


def reencode(path, dry):
    ap = os.path.abspath(path.replace('\\', '/'))
    if not ap.endswith('.img') or not os.path.isfile(ap):
        return '跳过：不是 .img 或文件不存在'
    data = open(ap, 'rb').read()
    tag = iv_of(data[:16])
    if tag is None:
        return '失败：两种 IV 都解不开头部，文件已损坏'
    if tag == 'std':
        return '已是标准 IV，无需处理'

    props = I.read_block(data, ZERO_IV)
    before = imgcut.digest(props)
    out = imgcut.write_img(props)                     # 默认 I.SRC_IV
    after = imgcut.digest(I.read_block(out, I.SRC_IV))
    if before != after:
        return '失败：重编码前后属性树 digest 不一致，原文件未动'

    if dry:
        return '待重编码（--dry-run 未写）：%d -> %d 字节，%d 条 digest 全等' % (
            len(data), len(out), len(after))

    stamp = time.strftime('%Y%m%d-%H%M%S')
    rel = rel_of(ap) or os.path.basename(ap)
    bak = os.path.join(BACKUPS, 'reencode', stamp, rel)
    os.makedirs(os.path.dirname(bak), exist_ok=True)
    shutil.copy2(ap, bak)
    tmp = ap + '.reenc.tmp'
    with open(tmp, 'wb') as f:
        f.write(out)
    os.replace(tmp, ap)
    return '已重编码 -> %d 字节（digest %d 行全等），原件备份 Backups/%s' % (
        len(out), len(after), os.path.join('reencode', stamp, rel).replace(os.sep, '/'))


def main():
    args = [a for a in sys.argv[1:] if a != '--dry-run']
    dry = '--dry-run' in sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(2)
    bad = 0
    for p in args:
        r = reencode(p, dry)
        print('%s\n    %s' % (p, r))
        if r.startswith('失败'):
            bad += 1
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
