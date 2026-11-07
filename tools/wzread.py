#!/usr/bin/env python3
"""Independent plain-WZ reader used to validate nx2wz output.

Mirrors MapleLib's parse path: PKG1 header, encrypted file lists, version-hash
protected offsets, image blocks with the mutable-key string layer.
"""
import os, struct, sys, zlib, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nx2wz import (M32, OFFSET_CONST, IVS, FSTART, COPYRIGHT, rotl32, version_hash,
                   make_keystream)

M4 = 0xFFFFFFFF


class Reader:
    def __init__(self, data, pos, ks, vhash, fstart, base=0):
        self.d = data; self.p = pos; self.ks = ks; self.h = vhash; self.f = fstart
        self.base = base

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def i8(self):
        v = struct.unpack_from('<b', self.d, self.p)[0]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.p)[0]; self.p += 2; return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]; self.p += 4; return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]; self.p += 4; return v

    def i64(self):
        v = struct.unpack_from('<q', self.d, self.p)[0]; self.p += 8; return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.p)[0]; self.p += 4; return v

    def f64(self):
        v = struct.unpack_from('<d', self.d, self.p)[0]; self.p += 8; return v

    def ci(self):
        v = self.i8()
        return self.i32() if v == -128 else v

    def clong(self):
        v = self.i8()
        return self.i64() if v == -128 else v

    def string(self):
        n = self.i8()
        if n == 0:
            return ''
        if n > 0:
            ln = self.i32() if n == 127 else n
            ks = self.ks(ln * 2)
            out = []
            mask = 0xAAAA
            for i in range(ln):
                w = struct.unpack_from('<H', self.d, self.p)[0]; self.p += 2
                out.append(chr(w ^ (ks[2 * i] | (ks[2 * i + 1] << 8)) ^ mask))
                mask = (mask + 1) & M4
            return ''.join(out)
        ln = self.i32() if n == -128 else -n
        ks = self.ks(ln)
        out = bytearray()
        mask = 0xAA
        for i in range(ln):
            out.append(self.d[self.p + i] ^ ks[i] ^ mask)
            mask = (mask + 1) & 0xFF
        self.p += ln
        return out.decode('ascii')

    def str_block(self):
        code = self.u8()
        if code in (0, 0x73):
            return self.string()
        if code in (1, 0x1B):
            off = self.i32()
            r = Reader(self.d, 0, self.ks, self.h, self.f)
            r.p = off if off < len(self.d) else off
            # inside an image block the reference is relative to the block start
            r.p = self.base + off
            return r.string()
        raise ValueError('bad string code %02x at %d' % (code, self.p))

    def name_or_ref(self):
        code = self.u8()
        if code == 2:
            off = self.i32()
            r = Reader(self.d, 0, self.ks, self.h, self.f)
            r.p = self.f + off
            r.u8()
            return r.string()
        return self.string()

    def offset(self):
        pos = self.p
        o = (pos - self.f) & M32
        o ^= M32
        o = (o * self.h) & M32
        o = (o - OFFSET_CONST) & M32
        o = rotl32(o, o & 0x1F)
        o ^= self.u32()
        o = (o + self.f * 2) & M32
        return o


def dir_entry(data, r):
    """(type, name) for one dir-table entry; a leading 2 means the name was deduped."""
    code = r.u8()
    if code == 2:
        sub = Reader(data, r.f + r.i32(), r.ks, r.h, r.f)
        return sub.u8(), sub.string()
    r.p -= 1
    return code, r.name_or_ref()


def parse_dir(data, r, depth=0, out=None):
    cnt = r.ci()
    entries = []
    for _ in range(cnt):
        t, name = dir_entry(data, r)
        size = r.ci(); check = r.ci(); off = r.offset()
        entries.append((t, name, size, check, off))
    dirs, imgs = [], []
    for t, name, size, check, off in entries:
        if t == 3:
            sub = Reader(data, off, r.ks, r.h, r.f)
            node = parse_dir(data, sub, depth + 1)
            node['name'] = name
            dirs.append(node)
        elif t == 4:
            imgs.append({'name': name, 'size': size, 'check': check, 'off': off})
        else:
            raise ValueError('bad entry type %d' % t)
    return {'name': '', 'dirs': dirs, 'imgs': imgs}


def parse_props(r, end):
    # the caller consumes the u16 0 marker, like MapleLib's ParsePropertyList
    props = []
    cnt = r.ci()
    for _ in range(cnt):
        name = r.str_block()
        t = r.u8()
        if t == 0:
            props.append((name, 'null', None))
        elif t in (2, 11):
            props.append((name, 'short', struct.unpack_from('<h', r.d, r.p)[0])); r.p += 2
        elif t in (3, 19):
            props.append((name, 'int', r.ci()))
        elif t == 20:
            props.append((name, 'long', r.clong()))
        elif t == 4:
            b = r.u8()
            v = 0.0 if b == 0 else (r.f32() if b == 0x80 else 0.0)
            props.append((name, 'float', v))
        elif t == 5:
            props.append((name, 'double', r.f64()))
        elif t == 8:
            props.append((name, 'str', r.str_block()))
        elif t == 9:
            ln = r.i32(); eob = r.p + ln
            props.append((name,) + parse_ext(r, eob))
        else:
            raise ValueError('unknown prop type %d at %d (name %r)' % (t, r.p - 1, name))
        if r.p > end:
            raise ValueError('property overran the image block')
    return props


def parse_ext(r, eob):
    code = r.u8()
    if code in (1, 0x1B):
        iname_ref = r.i32()
        sub = Reader(r.d, 0, r.ks, r.h, r.f)
        sub.base = r.base
        sub.p = r.base + iname_ref
        iname = sub.string()
    elif code in (0, 0x73):
        iname = r.string()
    else:
        raise ValueError('bad extended header %02x' % code)
    if iname == 'Property':
        r.p += 2
        return ('sub', parse_props(r, eob))
    if iname == 'Canvas':
        r.p += 1
        subs = []
        if r.u8() == 1:
            r.p += 2
            subs = parse_props(r, eob)
        w = r.ci(); h = r.ci(); f1 = r.ci(); f2 = r.ci(); r.i32()
        ln = r.i32() - 1
        r.p += 1
        blob = r.d[r.p:r.p + ln]; r.p += ln
        return ('canvas', {'w': w, 'h': h, 'fmt': f1 | (f2 << 8), 'blob': blob, 'subs': subs})
    if iname == 'Shape2D#Vector2D':
        return ('vector', (r.ci(), r.ci()))
    if iname == 'Shape2D#Convex2D':
        # entries have no name and no per-item length, only the outer block length bounds them
        return ('convex', [parse_ext(r, eob) for _ in range(r.ci())])
    if iname == 'Sound_DX8':
        r.p += 1
        dl = r.ci()
        ms = r.ci()
        out = ('sound', {'datalen': dl, 'ms': ms, 'hdr_at': r.p, 'eob': eob})
        r.p = eob
        return out
    if iname == 'UOL':
        r.p += 1
        return ('uol', r.str_block())
    raise ValueError('unsupported extended %r' % iname)


def inflate(payload):
    return zlib.decompressobj(-15).decompress(payload[2:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wz')
    ap.add_argument('--iv', default='ems', choices=sorted(IVS))
    ap.add_argument('--version', type=int, default=83)
    ap.add_argument('--path', help='image path to inspect, e.g. Cash/0501.img')
    ap.add_argument('--png', help='write <imgpath>/<prop> canvas to this png')
    args = ap.parse_args()

    data = open(args.wz, 'rb').read()
    assert data[:4] == b'PKG1', data[:4]
    fsize = struct.unpack_from('<q', data, 4)[0]
    fstart = struct.unpack_from('<I', data, 0xC)[0]
    copy = data[0x10:].split(b'\x00')[0]
    print('fsize=%d (%+d) fstart=%d copyright=%r' % (fsize, fsize + fstart - len(data), fstart, copy.decode()))
    vh = version_hash(args.version)
    enc = Reader(data, 0, lambda n: b'', vh, fstart)
    enc.p = fstart
    encver = struct.unpack_from('<H', data, fstart)[0]
    print('encVer=%04x versionHash=%08x (patch %d)' % (encver, vh, args.version))
    ks = make_keystream(IVS[args.iv], 1 << 21)
    r = Reader(data, fstart + 2, lambda n: ks[:n], vh, fstart)
    tree = parse_dir(data, r)
    total = [0]
    def walk(d, depth=0):
        print('  ' * depth + '/' + d['name'] + ('  [dir %d imgs]' % len(d['imgs'])))
        total[0] += len(d['imgs'])
        for i in d['imgs'][:6]:
            print('  ' * (depth + 1) + i['name'] + ' size=%d check=%d off=%d' % (i['size'], i['check'], i['off']))
        if len(d['imgs']) > 6:
            print('  ' * (depth + 1) + '... (%d images)' % len(d['imgs']))
        for s in d['dirs']:
            walk(s, depth + 1)
    walk(tree)
    print('images total', total[0])

    if args.path:
        parts = args.path.split('/')
        node = tree
        for p in parts[:-1]:
            node = [x for x in node['dirs'] if x['name'] == p][0]
        img = [x for x in node['imgs'] if x['name'] == parts[-1]][0]
        ir = Reader(data, img['off'], lambda n: ks[:n], vh, fstart)
        ir.base = img['off']
        b = ir.u8()
        assert b == 0x73, hex(b)
        nm = ir.string()
        assert nm == 'Property', nm
        assert ir.u16() == 0
        props = parse_props(ir, img['off'] + img['size'])
        got = sum(data[img['off']:img['off'] + img['size']]) & M32
        got = (got + 2**31) % 2**32 - 2**31
        print('image %s props=%d checksum ok=%s' % (args.path, len(props), got == img['check']))
        for nm, k, v in props[:12]:
            print('   ', nm, k, str(v)[:90])
        if args.png:
            def find(path, ps):
                cur = ps
                last = None
                for seg in path.split('/'):
                    m = [x for x in cur if x[0] == seg]
                    if not m:
                        raise KeyError(seg)
                    last = m[0]
                    cur = last[2] if last[1] == 'sub' else (last[2]['subs'] if last[1] == 'canvas' else [])
                return last
            prop = find(args.png, props)
            cv = prop[2]
            raw = inflate(cv['blob'])
            print('canvas %dx%d fmt=%d raw=%d expect=%d' % (cv['w'], cv['h'], cv['fmt'], len(raw), cv['w'] * cv['h'] * 4))
            from PIL import Image
            img2 = Image.frombytes('RGBA', (cv['w'], cv['h']), raw, 'raw', 'BGRA')
            outp = os.path.join(os.getcwd(), 'preview.png')
            img2.save(outp)
            print('saved', outp)


if __name__ == '__main__':
    main()
