#!/usr/bin/env python3
"""Export one Canvas node from a loose MapleStory .img as PNG."""
import argparse
import os
import shutil
import sys
import tempfile
import zlib
from pathlib import Path

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import img2wz as I


class ResourceError(ValueError):
    pass


def split_resource(resource):
    full = Path(os.path.abspath(resource))
    cursor = full
    while True:
        if cursor.is_file():
            if cursor.suffix.lower() != '.img':
                raise ResourceError('资源文件不是 .img: %s' % cursor)
            parts = full.relative_to(cursor).as_posix().split('/')
            parts = [part for part in parts if part not in ('', '.')]
            while parts and parts[0] == cursor.name:
                parts.pop(0)
            if not parts:
                raise ResourceError('资源路径缺少节点名: %s' % resource)
            return cursor, parts
        if cursor.parent == cursor:
            break
        cursor = cursor.parent
    raise ResourceError('资源文件不存在: %s' % resource)


def find_node(props, parts):
    current = props
    for index, part in enumerate(parts):
        node = next((item for item in current if item.name == part), None)
        path = '/'.join(parts[:index + 1])
        if node is None:
            raise ResourceError('资源节点不存在: %s' % path)
        if index + 1 < len(parts):
            if node.kind != 'sub':
                raise ResourceError('资源路径中间节点不是目录: %s (%s)' % (path, node.kind))
            current = node.props
    return node


def locate(resource):
    img_path, parts = split_resource(resource)
    props = I.read_img(str(img_path))
    return img_path, '/'.join(parts), props, find_node(props, parts)


def canvas_image(node):
    width, height, canvas_format, packed = node.value
    pixels = width * height
    if width <= 0 or height <= 0:
        raise ResourceError('Canvas 尺寸无效: %dx%d' % (width, height))
    if canvas_format not in (1, 2):
        raise ResourceError('Canvas 不是客户端支持的 PNG 像素格式: fmt=%d' % canvas_format)

    # The client accepts complete pixel data even when the zlib end marker is missing.
    decoder = zlib.decompressobj()
    raw = decoder.decompress(packed) + decoder.flush()
    expected = pixels * (2 if canvas_format == 1 else 4)
    if len(raw) == expected * 2 and not any(raw[expected:]):
        raw = raw[:expected]
    if len(raw) != expected:
        raise ResourceError('Canvas 像素长度错误: %d != %d' % (len(raw), expected))

    if canvas_format == 2:
        bgra = raw
    else:
        low = bytes((value & 0x0F) * 17 for value in range(256))
        high = bytes(((value >> 4) & 0x0F) * 17 for value in range(256))
        first, second = raw[0::2], raw[1::2]
        bgra = bytearray(pixels * 4)
        bgra[0::4] = first.translate(low)
        bgra[1::4] = first.translate(high)
        bgra[2::4] = second.translate(low)
        bgra[3::4] = second.translate(high)
    return Image.frombytes('RGBA', (width, height), bytes(bgra), 'raw', 'BGRA')


def atomic_replace(path, write):
    path = Path(path)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name + '.', suffix='.tmp',
                                     dir=str(path.parent))
    os.close(fd)
    try:
        write(temporary)
        if path.exists():
            shutil.copymode(path, temporary)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def export(resource, output_dir):
    img_path, node_path, _props, node = locate(resource)
    if node.kind != 'canvas':
        raise ResourceError('资源节点不是 PNG/Canvas: %s (%s)' % (node_path, node.kind))

    image = canvas_image(node)
    output_dir = Path(output_dir)
    if output_dir.exists() and not output_dir.is_dir():
        raise ResourceError('导出目标不是目录: %s' % output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / (node.name + '.png')

    def write(temporary):
        image.save(temporary, format='PNG')
        with Image.open(temporary) as check:
            if check.format != 'PNG' or check.size != image.size:
                raise ResourceError('导出的 PNG 回读校验失败')

    atomic_replace(output, write)
    print('导出成功: %s:%s -> %s (%dx%d)' %
          (img_path, node_path, output, image.width, image.height))


def main():
    parser = argparse.ArgumentParser(
        description='导出 IMG 内的 Canvas 为 PNG。资源路径格式：IMG文件路径/节点路径。',
        epilog='示例: python png_export.py "UIWindow2.img\\BestClassEvent\\backgrnd" "D:\\EXE\\WzComparerR2"')
    parser.add_argument('resource', help='IMG 文件路径加节点路径')
    parser.add_argument('output_dir', help='PNG 输出目录')
    args = parser.parse_args()
    try:
        export(args.resource, args.output_dir)
    except Exception as error:
        print('失败: %s' % error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
