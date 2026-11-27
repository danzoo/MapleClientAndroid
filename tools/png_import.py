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


def parse_origin(value):
    text = value.strip()
    if text.startswith('(') and text.endswith(')'):
        text = text[1:-1]
    parts = text.split(',')
    if len(parts) != 2:
        raise argparse.ArgumentTypeError('origin 格式应为 (x,y) 或 x,y')
    try:
        return tuple(int(part.strip()) for part in parts)
    except ValueError:
        raise argparse.ArgumentTypeError('origin 坐标必须是整数')


def set_origin(node, origin):
    for prop in node.props:
        if prop.name == 'origin':
            if prop.kind != 'vector':
                raise E.ResourceError('origin 节点不是向量: %s' % prop.kind)
            prop.value = origin
            return
    node.props.append(I.Prop('origin', 'vector', origin))


def save_props(img_path, props):
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


def import_png(picture, resource, origin=None):
    canvas = png_canvas(picture)
    img_path, node_path, props, node = E.locate(resource)
    if node.kind != 'canvas':
        raise E.ResourceError('资源节点不是 PNG/Canvas: %s (%s)' % (node_path, node.kind))

    old_width, old_height = node.value[:2]
    node.value = canvas
    if origin is not None:
        set_origin(node, origin)
    save_props(img_path, props)
    origin_text = '' if origin is None else '，origin=(%d,%d)' % origin
    print('导入成功: %s -> %s:%s (%dx%d -> %dx%d)%s，round-trip clean' %
          (picture, img_path, node_path, old_width, old_height, canvas[0], canvas[1], origin_text))


def update_origin(resource, origin):
    img_path, node_path, props, node = E.locate(resource)
    if node.kind != 'canvas':
        raise E.ResourceError('资源节点不是 PNG/Canvas: %s (%s)' % (node_path, node.kind))

    old_origin = next((prop.value for prop in node.props if prop.name == 'origin'), None)
    set_origin(node, origin)
    save_props(img_path, props)
    old_text = '无' if old_origin is None else '(%d,%d)' % old_origin
    print('坐标更新成功: %s:%s origin %s -> (%d,%d)，Canvas 未改动，round-trip clean' %
          (img_path, node_path, old_text, origin[0], origin[1]))


def main():
    parser = argparse.ArgumentParser(
        description='导入 PNG 并可选更新 origin，或只更新已有 Canvas 的 origin。',
        usage='%(prog)s PNG RESOURCE [ORIGIN]\n       %(prog)s RESOURCE ORIGIN',
        epilog='导入示例: python png_import.py "backgrnd.png" "UIWindow2.img\\BestClassEvent\\backgrnd" "(-23,90)"\n'
               '仅改坐标: python png_import.py "UIWindow2.img\\BestClassEvent\\backgrnd" "(-12,23)"')
    parser.add_argument('arguments', nargs='+', metavar='PATH_OR_ORIGIN')
    args = parser.parse_args()
    try:
        if len(args.arguments) == 2:
            resource, origin_text = args.arguments
            try:
                origin = parse_origin(origin_text)
            except argparse.ArgumentTypeError:
                import_png(resource, origin_text)
            else:
                update_origin(resource, origin)
        elif len(args.arguments) == 3:
            picture, resource, origin_text = args.arguments
            import_png(picture, resource, parse_origin(origin_text))
        else:
            parser.error('需要 2 个或 3 个参数')
    except argparse.ArgumentTypeError as error:
        parser.error(str(error))
    except Exception as error:
        print('失败: %s' % error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
