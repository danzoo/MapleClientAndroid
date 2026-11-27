#!/usr/bin/env python3
"""Import a PNG into an existing Canvas node in a loose MapleStory .img."""
import argparse
import os
import sys
import zlib
from pathlib import Path

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import img2wz as I
import imgcut as C
import png_export as E


def png_canvas(path):
    path = Path(path)
    if not path.is_file():
        raise E.ResourceError('PNG 文件不存在: %s' % path)
    with Image.open(path) as source:
        if source.format != 'PNG':
            raise E.ResourceError('输入文件不是 PNG 格式: %s' % path)
        if getattr(source, 'is_animated', False):
            raise E.ResourceError('输入文件是动态 PNG，Canvas 只支持静态图片: %s' % path)
        image = source.convert('RGBA')

    red, green, blue, alpha = image.split()
    bgra = Image.merge('RGBA', (blue, green, red, alpha)).tobytes()
    return image.width, image.height, 2, zlib.compress(bgra, 6)


def import_png(picture, resource):
    canvas = png_canvas(picture)
    img_path, node_path, props, node = E.locate(resource)
    if node.kind != 'canvas':
        raise E.ResourceError('资源节点不是 PNG/Canvas: %s (%s)' % (node_path, node.kind))

    old_width, old_height = node.value[:2]
    node.value = canvas
    expected = C.digest(props)
    data = C.write_img(props)
    if C.digest(I.read_block(data)) != expected:
        raise E.ResourceError('IMG 写回前回读校验失败，原文件未修改')

    def write(temporary):
        with open(temporary, 'wb') as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())

    E.atomic_replace(img_path, write)
    print('导入成功: %s -> %s:%s (%dx%d -> %dx%d)，round-trip clean' %
          (picture, img_path, node_path, old_width, old_height, canvas[0], canvas[1]))


def main():
    parser = argparse.ArgumentParser(
        description='将 PNG 导入 IMG 中已有的 Canvas。资源路径格式：IMG文件路径/节点路径。',
        epilog='示例: python png_import.py "D:\\EXE\\WzComparerR2\\backgrnd.png" "UIWindow2.img\\BestClassEvent\\backgrnd"')
    parser.add_argument('picture', help='输入 PNG 文件')
    parser.add_argument('resource', help='IMG 文件路径加节点路径')
    args = parser.parse_args()
    try:
        import_png(args.picture, args.resource)
    except Exception as error:
        print('失败: %s' % error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
