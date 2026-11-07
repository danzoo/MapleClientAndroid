#!/usr/bin/env python3
"""Dump a plain PKG1 .wz into the XML tree the server reads from assets/wz.

One <Wz>.wz becomes <out>/<Wz>.wz/<mirrored dirs>/<name>.img.xml, in the same element
vocabulary the existing assets use (WzComparerR2 style: imgdir/canvas/vector/extended/
sound/uol, no pixel data). This is metadata only - the server never looks at pixels.
"""
import os, sys, mmap, struct, argparse, time
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nx2wz as N
import wzread as W
import img2wz as I

DECL = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
ENTITIES = [('&', '&amp;'), ('<', '&lt;'), ('>', '&gt;'), ('"', '&quot;'), ("'", '&apos;')]


def esc(s):
    s = ''.join(c if c >= ' ' else '&#x%02X;' % ord(c) for c in s)
    for c, e in ENTITIES:
        s = s.replace(c, e)
    return s


def fmt_float(v):
    """Shortest fixed-point decimal that round-trips through float32, always with a
    fractional part - the corpus writes 10.0 and 1.3, never 1e+01."""
    f32 = lambda s: struct.unpack('<f', struct.pack('<f', float(s)))[0]
    for digits in range(0, 16):
        s = '%.*f' % (digits, v)
        if f32(s) == v:
            return s if digits else s + '.0'
    return repr(v)


def fmt_double(v):
    """Comma decimal separator, because GameConstants.parseNumber runs under Locale.FRANCE."""
    s = repr(v)
    if 'e' in s or 'E' in s:
        s = '%.15f' % v
    if '.' in s:
        s = s.rstrip('0').rstrip('.')
    return s.replace('.', ',')


class Out:
    def __init__(self):
        self.lines = []

    def add(self, depth, text):
        self.lines.append('  ' * depth + text)


def emit(o, props, depth):
    for p in props:
        k, v, name = p.kind, p.value, esc(p.name)
        if k == 'sub':
            o.add(depth, '<imgdir name="%s">' % name)
            emit(o, p.props, depth + 1)
            o.add(depth, '</imgdir>')
        elif k == 'canvas':
            w, h, fmt, _ = v
            o.add(depth, '<canvas name="%s" width="%d" height="%d">' % (name, w, h))
            emit(o, p.props, depth + 1)
            o.add(depth, '</canvas>')
        elif k == 'convex':
            # the exporter repeats the container's name on every point, and the server
            # only understands <convex>, so this stays the corpus-compatible <extended>
            o.add(depth, '<extended name="%s">' % name)
            for it in p.props:
                if it.kind == 'vector':
                    o.add(depth + 1, '<vector name="%s" x="%d" y="%d"/>' % (name, it.value[0], it.value[1]))
                else:
                    emit(o, [it], depth + 1)
            o.add(depth, '</extended>')
        elif k == 'vector':
            o.add(depth, '<vector name="%s" x="%d" y="%d"/>' % (name, v[0], v[1]))
        elif k == 'sound':
            o.add(depth, '<sound name="%s"/>' % name)
        elif k == 'uol':
            o.add(depth, '<uol name="%s" value="%s"/>' % (name, esc(v)))
        elif k == 'null':
            o.add(depth, '<null name="%s"/>' % name)
        elif k == 'str':
            o.add(depth, '<string name="%s" value="%s"/>' % (name, esc(v)))
        elif k == 'int':
            o.add(depth, '<int name="%s" value="%d"/>' % (name, v))
        elif k == 'short':
            o.add(depth, '<short name="%s" value="%d"/>' % (name, v))
        elif k == 'long':
            o.add(depth, '<long name="%s" value="%d"/>' % (name, v))
        elif k == 'float':
            o.add(depth, '<float name="%s" value="%s"/>' % (name, fmt_float(v)))
        elif k == 'double':
            o.add(depth, '<double name="%s" value="%s"/>' % (name, fmt_double(v)))
        else:
            raise NotImplementedError(k)


def image_xml(img_name, props):
    o = Out()
    o.add(0, DECL)
    o.add(0, '<imgdir name="%s">' % esc(img_name))
    emit(o, props, 1)
    o.add(0, '</imgdir>')
    return '\r\n'.join(o.lines) + '\r\n'


def wz_images(path, iv, patch):
    """[(relative dir, image name, offset, size)] for every image block in one .wz."""
    ks = N.make_keystream(N.IVS[iv], 1 << 21)
    with open(path, 'rb') as f:
        data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        assert data[:4] == b'PKG1', path
        fstart = struct.unpack_from('<I', data, 0xC)[0]
        r = W.Reader(data, fstart + 2, lambda n: ks[:n], N.version_hash(patch), fstart)
        tree = W.parse_dir(data, r)
        out = []

        def walk(d, prefix):
            for i in d['imgs']:
                out.append((prefix, i['name'], i['off'], i['size']))
            for sd in d['dirs']:
                walk(sd, sd['name'] if not prefix else prefix + '/' + sd['name'])
        walk(tree, '')
        data.close()
    return out


def write_group(job):
    """Convert a slice of one .wz; each worker re-reads only the blocks it needs."""
    wz, out_dir, iv, patch, group = job
    t0 = time.time()
    errs = []
    written = 0
    ks = N.make_keystream(N.IVS[iv], 1 << 21)
    with open(wz, 'rb') as f:
        data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        for prefix, name, off, size in group:
            try:
                props = I.read_block(data[off:off + size], N.IVS[iv])
                xml = image_xml(name, props or [])
            except Exception as e:
                errs.append('%s/%s: %s' % (prefix, name, e))
                continue
            dst = os.path.join(out_dir, os.path.basename(wz), *(prefix.split('/') if prefix else []))
            os.makedirs(dst, exist_ok=True)
            with open(os.path.join(dst, name + '.xml'), 'w', encoding='utf-8', newline='') as g:
                g.write(xml)
            written += 1
    return dict(wz=os.path.basename(wz), n=len(group), written=written,
                secs=round(time.time() - t0, 1), errs=errs[:5], nerrs=len(errs))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', help='.wz file or a directory of them')
    ap.add_argument('-o', '--out', required=True, help='assets-style output directory')
    ap.add_argument('--iv', default='ems', choices=sorted(N.IVS))
    ap.add_argument('--version', type=int, default=83)
    ap.add_argument('--only', default='', help='comma separated wz base names')
    ap.add_argument('--per-job', type=int, default=1500, help='images per worker task')
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args()

    if os.path.isdir(a.src):
        files = [e.path for e in sorted(os.scandir(a.src), key=lambda e: e.name)
                 if e.name.endswith('.wz')]
    else:
        files = [a.src]
    if a.only:
        want = set(a.only.split(','))
        files = [f for f in files if os.path.splitext(os.path.basename(f))[0] in want]

    jobs = []
    for f in files:
        imgs = wz_images(f, a.iv, a.version)
        for i in range(0, len(imgs), a.per_job):
            jobs.append((f, a.out, a.iv, a.version, imgs[i:i + a.per_job]))
    print('%d images in %d wz, %d jobs' % (sum(len(j[4]) for j in jobs), len(files), len(jobs)), flush=True)

    os.makedirs(a.out, exist_ok=True)
    total = errs = 0
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=a.jobs) as ex:
        for r in ex.map(write_group, jobs):
            total += r['written']
            errs += r['nerrs']
            print('%-12s %5d/%5d %5.1fs errs=%d %s' %
                  (r['wz'], r['written'], r['n'], r['secs'], r['nerrs'], r['errs'][:1]), flush=True)
    print('wrote %d xml files in %.1fs, %d errors -> %s' % (total, time.time() - t0, errs, a.out))


if __name__ == '__main__':
    main()
