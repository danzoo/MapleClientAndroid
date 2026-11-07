#!/usr/bin/env python3
"""Convert loose MapleStory .img trees (standard MapleLib imgs, GMS keystream) into plain PKG1 .wz.

Reads a client's Data/<Category>/**/*.img, decrypts the string/canvas layers with the
GMS IV, and re-emits each category as one .wz using nx2wz.py's validated container
writer, so the output opens with the same WzComparerR2 settings as the nx-derived
archives (IV ems, patch version 83).
"""
import os, sys, struct, argparse, collections, time
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nx2wz as N

M32 = 0xFFFFFFFF
SRC_IV = bytes.fromhex('4D23C72B')          # what the loose imgs are obfuscated with
STD_ZLIB = {0x9C78, 0xDA78, 0x0178, 0x5E78}
SOUND_GUID_LEN = len(N.SOUND_HEADER)   # 51-byte DSOUND preamble, then a wavFormat length byte
Prop = N.Prop

_TABS = {}


def xor(a, b):
    return (int.from_bytes(a, 'little') ^ int.from_bytes(b, 'little')).to_bytes(len(a), 'little')


def tabs(iv):
    """The two string XOR tables for one region IV, built once per IV.

    The keystream is folded into the tables so decrypting a string is one bignum XOR.
    """
    if iv not in _TABS:
        ks = N.make_keystream(iv, 1 << 22)
        a = bytes((ks[i] ^ ((0xAA + i) & 0xFF)) for i in range(len(ks)))
        u = bytearray(2 * len(ks))
        for i in range(len(ks) // 2):
            w = (ks[2 * i] | (ks[2 * i + 1] << 8)) ^ ((0xAAAA + i) & 0xFFFF)
            u[2 * i] = w & 0xFF
            u[2 * i + 1] = w >> 8
        _TABS[iv] = (a, bytes(u), ks)
    return _TABS[iv]


class Img:
    """Reader over one decrypted-in-place loose .img."""

    def __init__(self, data, iv=SRC_IV, base=0):
        self.d = data
        self.p = base
        self.base = base
        self.A, self.U, self.KS = tabs(iv)

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def i8(self):
        v = struct.unpack_from('<b', self.d, self.p)[0]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.p)[0]; self.p += 2; return v

    def i16(self):
        v = struct.unpack_from('<h', self.d, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]; self.p += 4; return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]; self.p += 4; return v

    def i64(self):
        v = struct.unpack_from('<q', self.d, self.p)[0]; self.p += 8; return v

    def f32(self):
        v = struct.unpack_from('<f', self.d, self.p)[0]; self.p += 4; return v

    def bytes(self, n):
        v = self.d[self.p:self.p + n]; self.p += n; return v

    def ci(self):
        s = self.i8()
        return self.i32() if s == -128 else s

    def string(self):
        n0 = self.i8()
        if n0 == 0:
            return ''
        if n0 > 0:
            n = self.i32() if n0 == 127 else n0
            raw = xor(self.bytes(2 * n), self.U[:2 * n])
            return raw.decode('utf-16-le', 'replace')
        n = self.i32() if n0 == -128 else -n0
        return xor(self.bytes(n), self.A[:n]).decode('latin-1')

    def string_at(self, off):
        save = self.p
        self.p = off
        s = self.string()
        self.p = save
        return s

    def name(self, base):
        code = self.u8()
        if code in (0, 0x73):
            return self.string()
        if code in (1, 0x1B):
            return self.string_at(base + self.i32())
        return ''

    # ------------------------------------------------------------ structure

    def prop_list(self, base, end):
        n = self.ci()
        if self.p > end:
            raise ValueError('property count runs past the block')
        out = []
        for _ in range(n):
            name = self.name(base)
            t = self.u8()
            if t == 0:
                out.append(Prop(name, 'null'))
            elif t in (2, 11):
                out.append(Prop(name, 'short', self.i16()))
            elif t in (3, 19):
                out.append(Prop(name, 'int', self.ci()))
            elif t == 20:
                s = self.i8()
                out.append(Prop(name, 'long', self.i64() if s == -128 else s))
            elif t == 4:
                sub = self.u8()
                out.append(Prop(name, 'float', self.f32() if sub == 0x80 else 0.0))
            elif t == 5:
                v = struct.unpack_from('<d', self.d, self.p)[0]; self.p += 8
                out.append(Prop(name, 'double', v))
            elif t == 8:
                out.append(Prop(name, 'str', self.name(base)))
            elif t == 9:
                ln = self.u32()
                eob = self.p + ln
                p = self.ext(base, eob, name)
                out.append(p)
                self.p = eob
            else:
                raise ValueError('unknown property type %d at 0x%x' % (t, self.p - 1))
        return out

    def ext(self, base, eob, name):
        code = self.u8()
        if code in (1, 0x1B):
            iname = self.string_at(base + self.i32())
        elif code in (0, 0x73):
            iname = self.string()
        else:
            raise ValueError('bad extended header %d at 0x%x' % (code, self.p - 1))
        SCAN['ext:' + iname] += 1
        if iname == 'Property':
            self.p += 2
            return Prop(name, 'sub', None, self.prop_list(base, eob))
        if iname == 'Canvas':
            self.p += 1
            props = []
            if self.u8() == 1:
                self.p += 2
                props = self.prop_list(base, eob)
            return Prop(name, 'canvas', self.canvas(), props)
        if iname == 'Shape2D#Vector2D':
            return Prop(name, 'vector', (self.ci(), self.ci()))
        if iname == 'Shape2D#Convex2D':
            n = self.ci()
            items = [self.ext(base, eob, name) for _ in range(n)]
            return Prop(name, 'convex', None, items)
        if iname == 'Sound_DX8':
            self.p += 1
            dlen = self.ci()
            ms = self.ci()
            fmt_len = self.d[self.p + SOUND_GUID_LEN]
            header = self.bytes(SOUND_GUID_LEN + 1 + fmt_len)
            return Prop(name, 'sound', (dlen, ms, header, self.bytes(dlen)))
        if iname == 'UOL':
            self.p += 1
            c = self.u8()
            if c == 0:
                return Prop(name, 'uol', self.string())
            if c == 1:
                return Prop(name, 'uol', self.string_at(base + self.i32()))
            raise ValueError('unsupported UOL subtype %d' % c)
        raise ValueError('unsupported extended type %r at 0x%x' % (iname, self.p))

    def canvas(self):
        w, h = self.ci(), self.ci()
        fmt = self.ci() + (self.ci() << 8)
        self.p += 4
        ln = self.u32() - 1
        self.p += 1
        payload = self.bytes(ln)
        SCAN['fmt:%d' % fmt] += 1
        if len(payload) >= 2 and (payload[0] | (payload[1] << 8)) not in STD_ZLIB:
            payload = delistwz(payload, self.KS)
            SCAN['listwz'] += 1
        return (w, h, fmt, payload)


def delistwz(src, ks):
    out = bytearray()
    o = 0
    while o < len(src):
        size = struct.unpack_from('<i', src, o)[0]; o += 4
        if size < 0 or size > len(src) - o:
            raise ValueError('bad listwz block size %d' % size)
        out += xor(src[o:o + size], ks[:size])
        o += size
    return bytes(out)


SCAN = collections.Counter()


def read_block(d, iv=SRC_IV):
    """Property tree of one image block; name references inside it count from 0."""
    if not d:
        return None
    r = Img(d, iv)
    if r.u8() != 0x73:
        raise ValueError('img does not start with 0x73')
    if r.string() != 'Property' or r.u16() != 0:
        raise ValueError('img header is not Property/0')
    return r.prop_list(0, len(d))


def read_img(path, iv=SRC_IV):
    with open(path, 'rb') as f:
        return read_block(f.read(), iv)


# ------------------------------------------------------------------- writing

EXTENDED = {'sub', 'canvas', 'vector', 'uol', 'sound', 'convex'}


def write_props(b, props):
    b.u16(0)
    b.compressed_int(len(props))
    for p in props:
        b.string_value(p.name, 0x00, 0x01)
        if p.kind in EXTENDED:
            b.u8(9)
            field = b.pos
            b.i32(0)
            write_body(b, p)
            struct.pack_into('<i', b.buf, field, b.pos - field - 4)
        else:
            write_body(b, p)


def write_body(b, p):
    k, v = p.kind, p.value
    if k == 'sub':
        b.string_value('Property', 0x73, 0x1B)
        write_props(b, p.props)
    elif k == 'convex':
        b.string_value('Shape2D#Convex2D', 0x73, 0x1B)
        b.compressed_int(len(p.props))
        for it in p.props:
            write_body(b, it)
    elif k == 'canvas':
        w, h, fmt, packed = v
        b.string_value('Canvas', 0x73, 0x1B)
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
        dlen, ms, header, data = v
        b.string_value('Sound_DX8', 0x73, 0x1B)
        b.u8(0)
        b.compressed_int(len(data))
        b.compressed_int(ms)
        b.raw(header)
        b.raw(data)
    elif k == 'vector':
        b.string_value('Shape2D#Vector2D', 0x73, 0x1B)
        b.compressed_int(v[0])
        b.compressed_int(v[1])
    elif k == 'uol':
        b.string_value('UOL', 0x73, 0x1B)
        b.u8(0)
        b.string_value(v, 0x00, 0x01)
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
        b.f64(v)
    elif k == 'str':
        b.u8(8)
        b.string_value(v, 0x00, 0x01)
    else:
        raise NotImplementedError(k)


ERRS = []


def encode_img(path, ks):
    """Parse one .img and serialize it into an ImageBlock, recording failures.

    ks=None is the scan pass: the tree is parsed and counted but not written.
    """
    try:
        props = read_img(path)
        if props is None:
            return None
        if ks is None:
            return N.ImageBlock(os.path.basename(path), b'')
        b = N.WzBuf(ks, 1, 0)
        b.string_value('Property', 0x73, 0x1B)
        write_props(b, props)
        return N.ImageBlock(os.path.basename(path), b.buf)
    except Exception as e:
        ERRS.append('%s: %s' % (path, e))
        return None


def build_dir(root_path, out_name, ks=None):
    """Mirror the Data/<category> tree: dirs keep their folder name, imgs keep the file name."""
    d = N.DirNode(out_name)
    for entry in sorted(os.scandir(root_path), key=lambda e: e.name):
        if entry.is_dir():
            sub = build_dir(entry.path, entry.name, ks)
            if sub.imgs or sub.dirs:
                d.dirs.append(sub)
        elif entry.name.endswith('.img'):
            blk = encode_img(entry.path, ks)
            if blk is not None:
                d.imgs.append(blk)
    return d


def dump(props, depth=0):
    out = []
    for p in props:
        if p.kind == 'sub':
            out.append('  ' * depth + '%s/  [%d]' % (p.name, len(p.props)))
            out.append(dump(p.props, depth + 1))
        elif p.kind == 'convex':
            out.append('  ' * depth + '%s  Convex2D[%d]' % (p.name, len(p.props)))
        elif p.kind == 'canvas':
            out.append('  ' * depth + '%s  Canvas %dx%d fmt%d %d bytes' %
                       (p.name, p.value[0], p.value[1], p.value[2], len(p.value[3])))
        elif p.kind == 'sound':
            out.append('  ' * depth + '%s  Sound %d ms, %d bytes' %
                       (p.name, p.value[1], len(p.value[3])))
        else:
            out.append('  ' * depth + '%s = %r (%s)' % (p.name, p.value, p.kind))
    return '\n'.join(out)


def build_tree(paths, out_name, ks):
    """DirNode out_name holding the given .img files, each stored under its own name."""
    d = N.DirNode(out_name)
    for p in paths:
        blk = encode_img(p, ks)
        if blk is not None:
            d.imgs.append(blk)
    return d


def write_wz(tree, out_path, iv_name, patch):
    vh = N.version_hash(patch)
    encver = N.version_header(vh)
    ks_bytes = N.make_keystream(N.IVS[iv_name], 1 << 21)
    ks = lambda n: ks_bytes[:n]
    tree.name = ''
    cache = {}
    N.measure(tree, cache)
    dir_start = N.FSTART + 2
    after_dirs = N.assign_offsets(tree, dir_start)
    total = N.assign_img_offsets(tree, after_dirs)
    images = []

    def collect(x):
        images.extend(x.imgs)
        for sd in x.dirs:
            collect(sd)
    collect(tree)

    b = N.WzBuf(ks, vh, N.FSTART)
    b.raw(b'PKG1')
    b.i64(total - N.FSTART)
    b.i32(N.FSTART)
    b.raw(N.COPYRIGHT + b'\x00')
    while b.pos < N.FSTART:
        b.u8(0)
    b.u16(encver)
    if b.pos != dir_start:
        raise AssertionError('dir start %d != %d' % (b.pos, dir_start))
    N.write_dir(b, tree)
    if b.pos != after_dirs:
        raise AssertionError('dir region %d != %d' % (b.pos, after_dirs))
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    with open(out_path, 'wb') as f:
        f.write(b.buf)
        pos = b.pos
        for img in images:
            if pos != img.offset:
                raise AssertionError('image %s at %d expected %d' % (img.name, pos, img.offset))
            f.write(img.data)
            pos += len(img.data)
            img.data = None
        if pos != total:
            raise AssertionError('total %d != %d' % (pos, total))
    return dict(bytes=pos, images=len(images), version=patch, hash=vh, encver=encver, iv=iv_name)


def convert_one(job):
    global ERRS
    ERRS = []
    t0 = time.time()
    ks_bytes = N.make_keystream(N.IVS[job['iv']], 1 << 21)
    ks = lambda n: ks_bytes[:n]
    if job.get('imgs') is not None:
        tree = build_tree(job['imgs'], job['name'], ks)
    else:
        tree = build_dir(job['src'], job['name'], ks)
    info = write_wz(tree, job['dst'], job['iv'], job['patch'])
    info['secs'] = round(time.time() - t0, 1)
    info['src'] = job['name']
    info['errs'] = ERRS[:20]
    info['nerrs'] = len(ERRS)
    return info


def scan_one(job):
    global ERRS, SCAN
    ERRS = []
    SCAN = collections.Counter()
    t0 = time.time()
    tree = (build_tree(job['imgs'], job['name'], None)
            if job.get('imgs') is not None else build_dir(job['src'], job['name'], None))
    info = dict(src=job['name'], images=N.count_tree(tree)[1], secs=round(time.time() - t0, 1),
                scan=dict(SCAN), errs=ERRS[:5], nerrs=len(ERRS))
    return info


def category_jobs(data, out, iv, patch, root_wz):
    entries = sorted(os.scandir(data), key=lambda e: e.name)
    jobs = [{'src': e.path, 'name': e.name, 'dst': os.path.join(out, e.name + '.wz'),
             'iv': iv, 'patch': patch} for e in entries if e.is_dir()]
    roots = [e.path for e in entries if e.is_file() and e.name.endswith('.img')]
    if roots:
        jobs.append({'imgs': roots, 'name': root_wz, 'src': data,
                     'dst': os.path.join(out, root_wz + '.wz'), 'iv': iv, 'patch': patch})
    return jobs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('data', help='client Data directory')
    ap.add_argument('-o', '--out', help='output directory for the .wz files')
    ap.add_argument('--iv', default='ems', choices=sorted(N.IVS))
    ap.add_argument('--version', type=int, default=83)
    ap.add_argument('--only', default='', help='comma separated category names')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--scan', action='store_true', help='only report what the trees contain')
    ap.add_argument('--peek', nargs='*', default=[], help='dump the tree of these .img paths and exit')
    ap.add_argument('--root-wz', default='Misc', help='wz name for the .img files sitting at the Data root')
    a = ap.parse_args()

    global SCAN
    if a.peek:
        for p in a.peek:
            SCAN = collections.Counter()
            print(p)
            print(dump(read_img(p)))
            print('  ', dict(SCAN))
        return

    jobs = category_jobs(a.data, a.out or '.', a.iv, a.version, a.root_wz)
    if a.only:
        want = set(a.only.split(','))
        jobs = [j for j in jobs if j['name'] in want]

    if a.scan:
        with ProcessPoolExecutor(max_workers=a.jobs) as ex:
            for i in ex.map(scan_one, jobs):
                print('%-14s imgs=%-6d %5.1fs errs=%d %s' %
                      (i['src'], i['images'], i['secs'], i['nerrs'], i['scan']), flush=True)
                if i['errs']:
                    print('   ', i['errs'], flush=True)
        return

    os.makedirs(a.out, exist_ok=True)
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        for info in ex.map(convert_one, jobs):
            print('%-14s %10d bytes  %5d imgs  %5.1fs  errs=%d' %
                  (info['src'], info['bytes'], info['images'], info['secs'], info['nerrs']), flush=True)
            for e in info['errs']:
                print('    !', e, flush=True)


if __name__ == '__main__':
    main()
