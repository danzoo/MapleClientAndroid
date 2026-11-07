#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""imgcheck.py —— 校验 loose .img 的容器 IV 是否是客户端认的那一套。

客户端 imgwz 解头写死区域 IV 0x4D23C72B（img_internal.hpp:163）。外部美术编辑器
（WzComparerR2 系）保存 loose img 时用的是零 keystream（IV=00000000），头解不出
'Property'，logcat 报「header is not Property/0」后整档放弃 —— 表现就是进图黑屏。
to_device.py 推送前、to_server.py 转换前都用本模块拦一道。
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

try:
    import img2wz as I
except Exception as _e:                          # 校验器自身不可用时不拦路
    I = None
    _IMPORT_ERR = repr(_e)


def client_readable(path):
    """(可读, 原因)。原因只在不可读或非标准 IV 时给。"""
    if I is None:
        return True, 'skip-check:%s' % _IMPORT_ERR
    try:
        with open(path, 'rb') as f:
            head = f.read(16)
    except Exception as e:
        return False, repr(e)
    try:
        r = I.Img(head, I.SRC_IV)
        if r.u8() == 0x73 and r.string() == 'Property':
            return True, ''
    except Exception:
        pass
    try:
        r = I.Img(head, bytes(4))
        if r.u8() == 0x73 and r.string() == 'Property':
            return False, 'IV=0（外部编辑器导出），客户端解不开头部'
    except Exception:
        pass
    return False, '头部不是 Property，两种 IV 都解不开'


def bad_paths(paths):
    """挑出客户端解不开的路径，返回 [(绝对路径, 原因)]。"""
    out = []
    for p in paths:
        ap = os.path.abspath(str(p).replace('\\', '/'))
        ok, why = client_readable(ap)
        if not ok:
            out.append((ap, why))
    return out


def report(bad, action='推送'):
    """打印拒绝清单和修法。"""
    print('以下 %d 个文件客户端 imgwz 解不开，已拒绝%s：' % (len(bad), action))
    for ap, why in bad:
        print('  %s : %s' % (ap, why))
    print('先重编码再同步：python "%s" "%s"'
          % (os.path.join(HERE, 'img_reencode.py'), bad[0][0]))
