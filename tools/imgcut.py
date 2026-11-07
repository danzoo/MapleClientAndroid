#!/usr/bin/env python3
"""Splice sub-trees from one loose .img into another and write the result back out.

The reader half is F:/nxfiles/tools/imgdec/img2wz.py read_block(); this adds the
matching writer, so the output is the same single-image-block layout the client's
imgwz module maps: 0x73 + "Property" + u16 0 + one property list, with the string
layers obfuscated by the region IV.  Every name is written inline, so no cross-record
string offsets have to be relocated when a sub-tree moves between files.

    python imgcut.py BASE.img --add WorldSelect/default --from MERGE.img -o OUT.img
    python imgcut.py FILE.img --list
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import img2wz as I          # read_img / read_block / Prop / tabs / xor

MAGIC = 0x73


class ImgBuf:
    """Sequential little-endian writer that obfuscates inline strings as it goes."""

    def __init__(self, iv):
        self.A, self.U, _ = I.tabs(iv)
        self.buf = bytearray()

    # ------------------------------------------------------------ primitives
    def raw(self, data):
        self.buf += data

    def u8(self, v):
        self.buf.append(v & 0xFF)

    def u16(self, v):
        self.buf += struct.pack('<H', v)

    def i8(self, v):
        self.buf += struct.pack('<b', v)

    def i32(self, v):
        self.buf += struct.pack('<i', v)

    def i64(self, v):
        self.buf += struct.pack('<q', v)

    @property
    def pos(self):
        return len(self.buf)

    def length_slot(self):
        field = self.pos
        self.i32(0)
        return field

    def patch32(self, field, value):
        struct.pack_into('<i', self.buf, field, value)

    def compressed_int(self, v):
        if -128 < v <= 127:
            self.i8(v)
        else:
            self.i8(-128)
            self.i32(v)

    def compressed_long(self, v):
        if -128 < v <= 127:
            self.i8(v)
        else:
            self.i8(-128)
            self.i64(v)

    # --------------------------------------------------------------- strings
    def string(self, text):
        """One length-prefixed string: ASCII when it fits, UTF-16 otherwise."""
        try:
            plain = text.encode('ascii')
        except UnicodeEncodeError:
            wide = text.encode('utf-16-le')
            units = len(wide) // 2
            if units < 127:
                self.i8(units)
            else:
                self.i8(127)
                self.i32(units)
            self.raw(I.xor(wide, self.U[:len(wide)]))
            return
        n = len(plain)
        if n == 0:
            self.i8(0)
        elif n <= 127:
            self.i8(-n)
        else:
            self.i8(-128)
            self.i32(n)
        self.raw(I.xor(plain, self.A[:n]))

    def inline_name(self, text, code=MAGIC):
        self.u8(code)
        self.string(text)


# --------------------------------------------------------------------- writing

def write_props(b, props):
    b.u16(0)
    b.compressed_int(len(props))
    for p in props:
        write_prop(b, p)


def write_prop(b, p):
    b.inline_name(p.name, 0x00)
    if p.kind in ('sub', 'canvas', 'vector', 'sound', 'uol', 'convex'):
        b.u8(9)
        field = b.length_slot()
        write_body(b, p)
        b.patch32(field, b.pos - field - 4)
    else:
        write_body(b, p)


def write_body(b, p):
    k, v = p.kind, p.value
    if k == 'sub':
        b.inline_name('Property')
        write_props(b, p.props)
    elif k == 'convex':
        b.inline_name('Shape2D#Convex2D')
        b.compressed_int(len(p.props))
        for it in p.props:
            write_body(b, it)
    elif k == 'canvas':
        w, h, fmt, packed = v
        b.inline_name('Canvas')
        b.u8(0)
        if p.props:
            b.u8(1)
            write_props(b, p.props)
        else:
            b.u8(0)
        b.compressed_int(w)
        b.compressed_int(h)
        b.compressed_int(fmt & 0xFF)
        b.compressed_int(fmt >> 8)
        b.i32(0)
        b.i32(len(packed) + 1)
        b.u8(0)
        b.raw(packed)
    elif k == 'sound':
        _dlen, ms, header, data = v
        b.inline_name('Sound_DX8')
        b.u8(0)
        b.compressed_int(len(data))
        b.compressed_int(ms)
        b.raw(header)
        b.raw(data)
    elif k == 'vector':
        b.inline_name('Shape2D#Vector2D')
        b.compressed_int(v[0])
        b.compressed_int(v[1])
    elif k == 'uol':
        b.inline_name('UOL')
        b.u8(0)
        b.inline_name(v, 0x00)
    elif k == 'null':
        b.u8(0)
    elif k == 'short':
        b.u8(2)
        b.raw(struct.pack('<h', v))
    elif k == 'int':
        b.u8(3)
        b.compressed_int(v)
    elif k == 'long':
        b.u8(20)
        b.compressed_long(v)
    elif k == 'float':
        b.u8(4)
        b.u8(0x80)
        b.raw(struct.pack('<f', v))
    elif k == 'double':
        b.u8(5)
        b.raw(struct.pack('<d', v))
    elif k == 'str':
        b.u8(8)
        b.inline_name(v, 0x00)
    else:
        raise NotImplementedError(k)


def write_img(props, iv=I.SRC_IV):
    b = ImgBuf(iv)
    b.u8(MAGIC)
    b.string('Property')
    b.u16(0)
    b.compressed_int(len(props))
    for p in props:
        write_prop(b, p)
    return bytes(b.buf)


# --------------------------------------------------------------- tree handling

def child(props, name):
    for p in props:
        if p.name == name:
            return p
    return None


def take_path(props, path):
    found = None
    for part in path.split('/'):
        found = child(props, part)
        if found is None:
            raise SystemExit('source has no %s (stopped at %s)' % (path, part))
        if found.kind != 'sub':
            raise SystemExit('source path %s hits a %s at %s' % (path, found.kind, part))
        props = found.props
    return found


def put_path(dst_props, parent_path, new):
    props = dst_props
    if parent_path:
        for part in parent_path.split('/'):
            cursor = child(props, part)
            if cursor is None or cursor.kind != 'sub':
                raise SystemExit('destination has no parent sub-tree %s' % part)
            props = cursor.props
    old = child(props, new.name)
    if old is None:
        props.append(new)
    elif old is not new:
        props[props.index(old)] = new


def digest(props, out=None, depth=0):
    """A canonical text form of a property tree, used for the round-trip check."""
    if out is None:
        out = []
    for p in props:
        pad = '  ' * depth
        if p.kind in ('sub', 'convex'):
            out.append('%s%s/ %s' % (pad, p.name, p.kind))
            digest(p.props, out, depth + 1)
        elif p.kind == 'canvas':
            w, h, fmt, packed = p.value
            out.append('%s%s canvas %dx%d fmt%d %d %s'
                       % (pad, p.name, w, h, fmt, len(packed), _md5(packed)))
            digest(p.props, out, depth + 1)
        elif p.kind == 'sound':
            dlen, ms, header, data = p.value
            out.append('%s%s sound %d %d %s %s'
                       % (pad, p.name, dlen, ms, _md5(header), _md5(data)))
        elif p.kind == 'vector':
            out.append('%s%s vector %d,%d' % (pad, p.name, p.value[0], p.value[1]))
        else:
            out.append('%s%s %s %r' % (pad, p.name, p.kind, p.value))
    return out


def _md5(data):
    import hashlib
    return hashlib.md5(data).hexdigest()


def read(path):
    with open(path, 'rb') as f:
        return I.read_block(f.read())


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('img', help='the base .img that keeps everything it already holds')
    ap.add_argument('--from', dest='src', help='.img the added sub-trees come from')
    ap.add_argument('--add', action='append', default=[],
                    dest='paths', help='slash separated sub-tree path to splice in, repeatable')
    ap.add_argument('-o', '--out', help='where to write the spliced .img')
    ap.add_argument('--list', action='store_true', help='dump the tree and exit')
    a = ap.parse_args()

    props = read(a.img)
    if a.list:
        print('\n'.join(digest(props)))
        return
    if not a.paths:
        raise SystemExit('--add PATH is required (or use --list)')
    if not a.src:
        raise SystemExit('--from FILE.img is required')
    if not a.out:
        raise SystemExit('-o OUT.img is required')

    source = read(a.src)
    for path in a.paths:
        new = take_path(source, path)
        parent = path.rsplit('/', 1)[0] if '/' in path else ''
        put_path(props, parent, new)
        print('+ %s  (%d records)' % (path, len(digest([new]))))

    data = write_img(props)
    with open(a.out, 'wb') as f:
        f.write(data)

    again = read(a.out)
    want, got = '\n'.join(digest(props)), '\n'.join(digest(again))
    if want != got:
        import difflib
        sys.stdout.writelines(l + '\n' for l in list(difflib.unified_diff(
            want.splitlines(), got.splitlines(), 'in-memory', a.out, lineterm='\n'))[:40])
        raise SystemExit('round-trip mismatch, %s is not usable' % a.out)
    print('%s: %d -> %d bytes, %d records, round-trip clean'
          % (a.out, os.path.getsize(a.img), len(data), len(got.splitlines())))


if __name__ == '__main__':
    main()
