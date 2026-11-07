#!/usr/bin/env python3
"""NoLifeNx (.nx) -> plain WZ (PKG1) converter.

Byte layout mirrors MapleLib's write path: header, directory file lists, then
image blocks.  Verified against a real v072 wz: version hash, offset encryption
and the string keystream (position independent, mask 0xAA/0xAAAA).
"""

import argparse
import mmap
import os
import struct
import sys
import zlib
from collections import Counter

M32 = 0xFFFFFFFF
OFFSET_CONST = 0x581C3F6D
AES_USER_KEY = bytes(
    [0x13, 0, 0, 0, 0x08, 0, 0, 0, 0x06, 0, 0, 0, 0xB4, 0, 0, 0,
     0x1B, 0, 0, 0, 0x0F, 0, 0, 0, 0x33, 0, 0, 0, 0x52, 0, 0, 0])
IVS = {
    "ems": bytes.fromhex("B97D63E9"),   # KMS / MSEA - what the 072 files use
    "gms": bytes.fromhex("4D23C72B"),
    "bms": bytes(4),                    # zero IV -> all-zero keystream
}
COPYRIGHT = b"Package file v1.0 Copyright 2002 Wizet, ZMS"
FSTART = 60

NX_NONE, NX_INT, NX_REAL, NX_STR, NX_VEC, NX_BMP, NX_AUD = range(7)
ENTRY_DIR, ENTRY_IMG, ENTRY_STRREF = 3, 4, 2
EXTENDED = {"sub", "canvas", "vector", "uol", "sound"}

# The fixed 51-byte preamble every wz Sound_DX8 property stores in front of a
# serialised WAVEFORMATEXTENSIBLE.  NoLifeNx keeps it verbatim inside the blob.
SOUND_HEADER = bytes([0x02,
    0x83, 0xEB, 0x36, 0xE4, 0x4F, 0x52, 0xCE, 0x11, 0x9F, 0x53, 0x00, 0x20,
    0xAF, 0x0B, 0xA7, 0x70,
    0x8B, 0xEB, 0x36, 0xE4, 0x4F, 0x52, 0xCE, 0x11, 0x9F, 0x53, 0x00, 0x20,
    0xAF, 0x0B, 0xA7, 0x70,
    0x00, 0x01,
    0x81, 0x9F, 0x58, 0x05, 0x56, 0xC3, 0xCE, 0x11, 0xBF, 0x01, 0x00, 0xAA,
    0x00, 0x55, 0x59, 0x5A])

STATS = Counter()


def rotl32(x, n):
    n &= 31
    return x & M32 if n == 0 else ((x << n) | (x >> (32 - n))) & M32


def version_hash(patch):
    h = 0
    for b in str(patch).encode():
        h = (h * 32 + b + 1) & M32
    return h


def version_header(vh):
    return (~(((vh >> 24) ^ (vh >> 16) ^ (vh >> 8) ^ vh) & 0xFF)) & 0xFF


def make_keystream(iv, size):
    if iv == b"\x00\x00\x00\x00":
        return bytes(size)
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    enc = Cipher(algorithms.AES(AES_USER_KEY), modes.ECB()).encryptor()
    out = bytearray()
    blk = bytes(iv * 4)
    while len(out) < size:
        blk = enc.update(blk)
        out += blk
    return bytes(out[:size])


try:
    from lz4.block import decompress as _lz4_block
except ImportError:                                  # pragma: no cover
    _lz4_block = None


def lz4_decompress(src, dst_size):
    if _lz4_block is not None:
        try:
            return _lz4_block(src, uncompressed_size=dst_size)
        except Exception:
            pass  # fall back to the portable decoder below
    if not isinstance(src, bytes):
        src = bytes(src)
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        tok = src[i]
        i += 1
        ll = tok >> 4
        if ll == 15:
            while True:
                b = src[i]
                i += 1
                ll += b
                if b != 255:
                    break
        if ll:
            out += src[i:i + ll]
            i += ll
        if i >= n:
            break
        off = src[i] | (src[i + 1] << 8)
        i += 2
        ml = (tok & 0xF) + 4
        if ml == 19:
            while True:
                b = src[i]
                i += 1
                ml += b
                if b != 255:
                    break
        pos = len(out) - off
        if pos < 0:
            raise ValueError("lz4: corrupt match offset")
        seg = out[pos:pos + off]
        out += seg[:ml] if off >= ml else (seg * ((ml + off - 1) // off))[:ml]
    if len(out) != dst_size:
        raise ValueError("lz4: got %d bytes, expected %d" % (len(out), dst_size))
    return bytes(out)


# ---------------------------------------------------------------- nx reading

class NxFile:
    def __init__(self, path):
        # Mapped, not read: the map .nx files run to gigabytes and the process
        # has to stay small enough to convert several of them at once.
        self._f = open(path, "rb")
        self.data = mmap.mmap(self._f.fileno(), 0, access=mmap.ACCESS_READ)
        (magic, self.node_count, self.node_off, self.str_count, self.str_off,
         self.bmp_count, self.bmp_off, self.aud_count, self.aud_off) = \
            struct.unpack_from("<IIQIQIQIQ", self.data, 0)
        if magic != 0x34474B50:
            raise ValueError("not a NoLifeNx file (magic %08x)" % magic)
        self.str_table = struct.unpack_from("<%dQ" % self.str_count, self.data, self.str_off)
        self.bmp_table = struct.unpack_from("<%dQ" % self.bmp_count, self.data, self.bmp_off)
        self.aud_table = struct.unpack_from("<%dQ" % self.aud_count, self.data, self.aud_off)
        self._names = {}

    def close(self):
        self.data.close()
        self._f.close()

    def name(self, sidx):
        v = self._names.get(sidx)
        if v is not None:
            return v
        off = self.str_table[sidx]
        ln = struct.unpack_from("<H", self.data, off)[0]
        v = self.data[off + 2:off + 2 + ln].decode("utf-8", "replace")
        self._names[sidx] = v
        return v

    def record(self, idx):
        return struct.unpack_from("<IIHH8s", self.data, self.node_off + 20 * idx)

    def kids(self, idx):
        _, ch, num, _, _ = self.record(idx)
        return [self.record(c) + (c,) for c in range(ch, ch + num)]

    def pixels(self, bidx, w, h):
        off = self.bmp_table[bidx]
        clen, = struct.unpack_from("<I", self.data, off)
        return lz4_decompress(memoryview(self.data)[off + 4:off + 4 + clen], 4 * w * h)

    def audio(self, index, length):
        off = self.aud_table[index]
        return bytes(memoryview(self.data)[off:off + length])


# -------------------------------------------------------------- property model

class Prop:
    __slots__ = ("name", "kind", "value", "props")

    def __init__(self, name, kind, value=None, props=()):
        self.name, self.kind, self.value, self.props = name, kind, value, list(props)


def node_to_prop(nx, rec):
    name, ch, num, typ, pay, idx = rec
    name = nx.name(name)
    child_recs = [nx.record(c) + (c,) for c in range(ch, ch + num)]
    if typ == NX_NONE:
        return Prop(name, "sub", None, [node_to_prop(nx, r) for r in child_recs])
    if typ == NX_INT:
        v, = struct.unpack_from("<q", pay, 0)
        return Prop(name, "int" if -2**31 <= v < 2**31 else "long", v)
    if typ == NX_REAL:
        return Prop(name, "double", struct.unpack_from("<d", pay, 0)[0])
    if typ == NX_STR:
        return Prop(name, "str", nx.name(struct.unpack_from("<I", pay, 0)[0]))
    if typ == NX_VEC:
        return Prop(name, "vector", struct.unpack_from("<ii", pay, 0))
    if typ == NX_BMP:
        bidx, = struct.unpack_from("<I", pay, 0)
        w, h = struct.unpack_from("<HH", pay, 4)
        STATS["canvas"] += 1
        # Pixels stay in the file until the image block is serialised; holding
        # every decoded canvas at once would not fit for the map archives.
        return Prop(name, "canvas", (nx, bidx, w, h),
                    [node_to_prop(nx, r) for r in child_recs])
    if typ == NX_AUD:
        STATS["audio"] += 1
        return Prop(name, "sound", (nx,) + struct.unpack_from("<II", pay, 0))
    raise NotImplementedError("nx node type %d (%r) unsupported" % (typ, name))


def walk_dirs(nx, idx):
    """(name, [subdir tuples], [(img name, props)]) for an nx directory node."""
    name = nx.name(nx.record(idx)[0])
    subdirs, images, loose = [], [], []
    for rec in nx.kids(idx):
        cname, cch, cnum, ctyp = rec[0], rec[1], rec[2], rec[3]
        cname = nx.name(cname)
        if ctyp == NX_NONE and cname.lower().endswith(".img"):
            STATS["image"] += 1
            images.append((cname, [node_to_prop(nx, nx.record(c) + (c,))
                                   for c in range(cch, cch + cnum)]))
        elif ctyp == NX_NONE:
            subdirs.append(walk_dirs(nx, rec[5]))
        else:
            loose.append(rec)
    if loose:
        # scalars directly under a directory cannot exist in wz; keep them in a
        # synthetic image instead of dropping them silently.
        STATS["loose"] += len(loose)
        subdirs.append(("__nx_loose__", [],
                        [("loose.img", [node_to_prop(nx, r) for r in loose])]))
    return (name, subdirs, images)


# ---------------------------------------------------------------- wz primitives

class WzBuf:
    def __init__(self, ks, vhash, fstart, pos=0):
        self.ks = ks            # callable(n) -> first n keystream bytes
        self.hash = vhash
        self.fstart = fstart
        self.pos = pos
        self.buf = bytearray()
        self.cache = {}

    def _emit(self, b):
        self.buf += b
        self.pos += len(b)

    def u8(self, v): self._emit(struct.pack("<B", v & 0xFF))
    def i8(self, v): self._emit(struct.pack("<b", v))
    def u16(self, v): self._emit(struct.pack("<H", v & 0xFFFF))
    def i32(self, v): self._emit(struct.pack("<i", v))
    def u32(self, v): self._emit(struct.pack("<I", v & M32))
    def i64(self, v): self._emit(struct.pack("<q", v))
    def f64(self, v): self._emit(struct.pack("<d", v))
    def raw(self, b): self._emit(b)

    def compressed_int(self, v):
        if ((v + 127) & M32) > 254:
            self.i8(-128)
            self.i32(v)
        else:
            self.i8(v)

    def compressed_long(self, v):
        if v > 127 or v <= -128:
            self.i8(-128)
            self.i64(v)
        else:
            self.i8(v)

    def string(self, s):
        if not s:
            self.i8(0)
            return
        if any(ord(c) > 127 for c in s):        # UTF-16
            if len(s) >= 127:
                self.i8(127)
                self.i32(len(s))
            else:
                self.i8(len(s))
            ks = self.ks(len(s) * 2)
            out = bytearray()
            mask = 0xAAAA
            for i, c in enumerate(s):
                key = ks[2 * i] | (ks[2 * i + 1] << 8)
                out += struct.pack("<H", (ord(c) ^ key ^ mask) & 0xFFFF)
                mask = (mask + 1) & 0xFFFF
            self.raw(bytes(out))
        else:                                    # ASCII
            if len(s) > 127:
                self.i8(-128)
                self.i32(len(s))
            else:
                self.i8(-len(s))
            ks = self.ks(len(s))
            out = bytearray()
            mask = 0xAA
            for i, c in enumerate(s):
                out.append(ord(c) ^ ks[i] ^ mask)
                mask = (mask + 1) & 0xFF
            self.raw(bytes(out))

    def string_value(self, s, code_plain, code_ref):
        """Name/value string field, deduped inside one image block."""
        if len(s) > 4 and s in self.cache:
            self.u8(code_ref)
            self.i32(self.cache[s])
            return
        self.u8(code_plain)
        self.cache[s] = self.pos
        self.string(s)

    def object_value(self, s, entry_type):
        """File-list name field, deduped across the whole file (offset from fstart)."""
        key = "%d_%s" % (entry_type, s)
        if len(s) > 4 and key in self.cache:
            self.u8(ENTRY_STRREF)
            self.i32(self.cache[key])
            return
        self.cache[key] = self.pos - self.fstart
        self.u8(entry_type)
        self.string(s)

    def offset(self, value):
        o = (self.pos - self.fstart) & M32
        o ^= M32
        o = (o * self.hash) & M32
        o = (o - OFFSET_CONST) & M32
        o = rotl32(o, o & 0x1F)
        self.u32((o ^ ((value - self.fstart * 2) & M32)) & M32)


def write_property_list(b, props):
    b.u16(0)
    b.compressed_int(len(props))
    for p in props:
        b.string_value(p.name, 0x00, 0x01)
        if p.kind in EXTENDED:
            b.u8(9)
            field = b.pos
            b.i32(0)
            write_body(b, p)
            struct.pack_into("<i", b.buf, field, b.pos - field - 4)
        else:
            write_body(b, p)


def write_body(b, p):
    k = p.kind
    if k == "sub":
        b.string_value("Property", 0x73, 0x1B)
        write_property_list(b, p.props)
    elif k == "vector":
        b.string_value("Shape2D#Vector2D", 0x73, 0x1B)
        b.compressed_int(p.value[0])
        b.compressed_int(p.value[1])
    elif k == "uol":
        b.string_value("UOL", 0x73, 0x1B)
        b.u8(0)
        b.string_value(p.value, 0x00, 0x01)
    elif k == "canvas":
        nx, bidx, w, h = p.value
        pixels = nx.pixels(bidx, w, h)
        b.string_value("Canvas", 0x73, 0x1B)
        b.u8(0)
        if p.props:
            b.u8(1)
            write_property_list(b, p.props)
        else:
            b.u8(0)
        b.compressed_int(w)
        b.compressed_int(h)
        b.compressed_int(2)          # format 2 = BGRA8888
        b.compressed_int(0)
        b.i32(0)
        co = zlib.compressobj(6, zlib.DEFLATED, -15)
        packed = b"\x78\x9c" + co.compress(pixels) + co.flush()
        b.i32(len(packed) + 1)
        b.u8(0)
        b.raw(packed)
    elif k == "sound":
        nx, index, length = p.value
        blob = nx.audio(index, length)
        b.string_value("Sound_DX8", 0x73, 0x1B)
        b.u8(0)
        if len(blob) > len(SOUND_HEADER) and blob[:len(SOUND_HEADER)] == SOUND_HEADER:
            fmt_len = blob[len(SOUND_HEADER)]
            hdr_end = len(SOUND_HEADER) + 1 + fmt_len
            if hdr_end > len(blob):
                STATS["bad_audio"] += 1
                hdr_end, fmt_len = len(blob), 0
            header = blob[:hdr_end]
            data = blob[hdr_end:]
            # WAVEFORMATEXTENSIBLE.nAvgBytesPerSec sits right after the GUIDs.
            avg = struct.unpack_from("<I", blob, len(SOUND_HEADER) + 1 + 8)[0] \
                if len(blob) >= 64 else 0
            ms = len(data) * 1000 // avg if avg else 0
        else:
            STATS["bad_audio"] += 1
            header, data, ms = blob, b"", 0
        b.compressed_int(len(data))
        b.compressed_int(ms)
        b.raw(header)
        b.raw(data)
    elif k == "int":
        b.u8(3)
        b.compressed_int(p.value)
    elif k == "long":
        b.u8(20)
        b.compressed_long(p.value)
    elif k == "double":
        b.u8(5)
        b.f64(p.value)
    elif k == "str":
        b.u8(8)
        b.string_value(p.value, 0x00, 0x01)
    else:
        raise NotImplementedError(k)


class ImageBlock:
    __slots__ = ("name", "data", "size", "checksum", "offset")

    def __init__(self, name, data):
        self.name = name
        self.data = data
        self.size = len(data)
        # MapleLib sums into a signed int32 and lets it wrap
        self.checksum = (sum(data) + 2**31) % 2**32 - 2**31
        self.offset = 0


class DirNode:
    __slots__ = ("name", "dirs", "imgs", "offset", "offset_size", "size", "checksum")

    def __init__(self, name, dirs=(), imgs=()):
        self.name = name
        self.dirs = list(dirs)
        self.imgs = list(imgs)
        self.offset = 0
        self.offset_size = 0
        self.size = 0
        self.checksum = 0


def build_dir(node, ks):
    name, subdirs, images = node
    d = DirNode(name)
    for img_name, props in images:
        b = WzBuf(ks, 1, 0)
        b.string_value("Property", 0x73, 0x1B)
        write_property_list(b, props)
        d.imgs.append(ImageBlock(img_name, b.buf))
    for sd in subdirs:
        d.dirs.append(build_dir(sd, ks))
    return d


# ------------------------------------------------------------- layout & output

def comp_int_len(v):
    return 5 if ((v + 127) & M32) > 254 else 1


def string_len(s):
    if not s:
        return 1
    if any(ord(c) > 127 for c in s):
        return (5 if len(s) >= 127 else 1) + len(s) * 2
    return (5 if len(s) > 127 else 1) + len(s)


def object_value_len(s, entry_type, cache):
    key = "%d_%s" % (entry_type, s)
    if len(s) > 4 and key in cache:
        return 5
    cache[key] = 1
    return 1 + string_len(s)


def measure(d, cache):
    n_entries = len(d.dirs) + len(d.imgs)
    if n_entries == 0:
        d.offset_size = 1
        d.size = 0
        return
    d.offset_size = comp_int_len(n_entries)
    d.size = comp_int_len(n_entries)
    for img in d.imgs:
        nl = object_value_len(img.name, ENTRY_IMG, cache)
        d.offset_size += nl + comp_int_len(img.size) + comp_int_len(img.checksum) + 4
        d.size += nl + comp_int_len(img.size) + img.size + comp_int_len(img.checksum) + 4
    for sd in d.dirs:
        nl = object_value_len(sd.name, ENTRY_DIR, cache)
        measure(sd, cache)
        d.offset_size += nl + comp_int_len(sd.size) + comp_int_len(sd.checksum) + 4
        d.size += nl + comp_int_len(sd.size) + comp_int_len(sd.checksum) + 4 + sd.size


def assign_offsets(d, cur):
    d.offset = cur
    cur += d.offset_size
    for sd in d.dirs:
        cur = assign_offsets(sd, cur)
    return cur


def assign_img_offsets(d, cur):
    for img in d.imgs:
        img.offset = cur
        cur += img.size
    for sd in d.dirs:
        cur = assign_img_offsets(sd, cur)
    return cur


def write_dir(b, d):
    n_entries = len(d.dirs) + len(d.imgs)
    if n_entries == 0:
        return
    b.compressed_int(n_entries)
    for img in d.imgs:
        b.object_value(img.name, ENTRY_IMG)
        b.compressed_int(img.size)
        b.compressed_int(img.checksum)
        b.offset(img.offset)
    for sd in d.dirs:
        b.object_value(sd.name, ENTRY_DIR)
        b.compressed_int(sd.size)
        b.compressed_int(sd.checksum)
        b.offset(sd.offset)
    for sd in d.dirs:
        if sd.size > 0:
            write_dir(b, sd)
        else:
            b.u8(0)


def prune_images(d, state):
    kept = []
    for img in d.imgs:
        if state["left"] > 0:
            state["left"] -= 1
            kept.append(img)
    d.imgs = kept
    d.dirs = [sd for sd in (prune_images(x, state) for x in d.dirs) if sd is not None]
    if not d.imgs and not d.dirs:
        return None
    return d


def count_tree(d):
    dirs, imgs, size = 1, len(d.imgs), sum(i.size for i in d.imgs)
    for sd in d.dirs:
        a, b, c = count_tree(sd)
        dirs += a
        imgs += b
        size += c
    return dirs, imgs, size


def convert(nx_path, out_path, iv_name, patch, as64, limit):
    STATS.clear()
    nx = NxFile(nx_path)
    vh = version_hash(patch)
    encver = version_header(vh)
    ks_bytes = make_keystream(IVS[iv_name], 1 << 21)
    ks = lambda n: ks_bytes[:n]

    root = walk_dirs(nx, 0)
    tree = build_dir(root, ks)
    tree.name = ""
    if limit is not None:
        tree = prune_images(tree, {"left": limit}) or DirNode("")

    cache = {}
    measure(tree, cache)
    dir_start = FSTART + (0 if as64 else 2)
    after_dirs = assign_offsets(tree, dir_start)
    total = assign_img_offsets(tree, after_dirs)

    images = []

    def collect(d):
        images.extend(d.imgs)
        for sd in d.dirs:
            collect(sd)
    collect(tree)

    b = WzBuf(ks, vh, FSTART)
    b.raw(b"PKG1")
    b.i64(total - FSTART)
    b.i32(FSTART)
    b.raw(COPYRIGHT + b"\x00")
    while b.pos < FSTART:
        b.u8(0)
    if b.pos != FSTART:
        raise AssertionError("copyright longer than the header: %d" % b.pos)
    if not as64:
        b.u16(encver)
    if b.pos != dir_start:
        raise AssertionError("dir start %d != %d" % (b.pos, dir_start))
    write_dir(b, tree)
    if b.pos != after_dirs:
        raise AssertionError("dir region %d != %d" % (b.pos, after_dirs))

    d, n, sz = count_tree(tree)
    out_dir = os.path.dirname(out_path)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    # Stream the image blocks straight to disk and drop each one after writing,
    # so the peak stays at one image instead of the whole archive.
    with open(out_path, "wb") as f:
        f.write(b.buf)
        pos = b.pos
        for img in images:
            if pos != img.offset:
                raise AssertionError("image %s at %d, expected %d" % (img.name, pos, img.offset))
            f.write(img.data)
            pos += len(img.data)
            img.data = None
        if pos != total:
            raise AssertionError("total %d != %d" % (pos, total))
    info = dict(bytes=pos, dirs=d, images=n, imgbytes=sz,
                version=patch, hash=vh, encver=encver, iv=iv_name, as64=bool(as64))
    info.update(STATS)
    nx.close()
    return info


def main():
    ap = argparse.ArgumentParser(description="convert a NoLifeNx .nx file to plain .wz")
    ap.add_argument("input")
    ap.add_argument("-o", "--output", help="target .wz, or a directory to write into")
    ap.add_argument("--iv", default="ems", choices=sorted(IVS),
                    help="wz string IV (ems=KMS/MSEA, gms, bms=zero/no encryption)")
    ap.add_argument("--version", type=int, default=83,
                    help="patch version the reader must be told (builds the version hash)")
    ap.add_argument("--64bit", dest="as64", action="store_true",
                    help="omit the 2-byte encVer field (modern clients try 770-780)")
    ap.add_argument("--limit", type=int, help="keep only the first N images (smoke test)")
    args = ap.parse_args()

    base = os.path.splitext(os.path.basename(args.input))[0]
    out = args.output or base + ".wz"
    if os.path.isdir(out) or out.endswith(("\\", "/")):
        out = os.path.join(out, base + ".wz")

    s = convert(args.input, out, args.iv, args.version, args.as64, args.limit)
    print("wrote %s" % out)
    print("bytes=%(bytes)d dirs=%(dirs)d images=%(images)d canvases=%(canvas)d "
          "audio=%(audio)d version=%(version)d hash=%(hash)08x encver=%(encver)02x "
          "iv=%(iv)s 64bit=%(as64)s loose=%(loose)d bad_audio=%(bad_audio)d" %
          dict(s, canvas=s.get("canvas", 0), loose=s.get("loose", 0),
               audio=s.get("audio", 0), bad_audio=s.get("bad_audio", 0)))


if __name__ == "__main__":
    sys.exit(main())
