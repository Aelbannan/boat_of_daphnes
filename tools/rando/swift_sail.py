#!/usr/bin/env python3
"""Build the three Swift Sail replacement textures from the player's own disc.

    python swift_sail.py <GZLE01 iso> <output folder>

The script reads the sail on the boat (Ship.arc tex/new_ho1.bti), the item icon
(itemicon.arc timg/sail_00.bti) and the item-get texture (Vho.arc bdl/vho.bdl,
texture "Vho") straight from the ISO, in memory. It checks that each one is the
vanilla texture (by the renderer's content hash), applies the recolour data in
swift_sail_data.json, and writes three PNGs named the way the renderer's
Dolphin-format texture replacement expects:

    tex1_<W>x<H>_<texhash>[_<tluthash>]_<GX format>.png

The data file holds only colour tables and a sparse pixel delta. It holds no
disc pixels and no copy of any image. The red Swift Sail look is the one the
Wind Waker Randomizer (wwrando, MIT, Copyright (c) 2018 LagoLunatic; textures
credited to Hypatia, graphic design to MelonSpeedruns) puts on the disc. The
original artwork belongs to Nintendo.

Standard library plus Pillow only.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys

DATA_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "swift_sail_data.json")


class SailError(Exception):
    pass


# ---------------------------------------------------------------------------
# XXH64 (seed 0), the renderer's texture hash.
# ---------------------------------------------------------------------------

_M = (1 << 64) - 1
_P1 = 11400714785074694791
_P2 = 14029467366897019727
_P3 = 1609587929392839161
_P4 = 9650029242287828579
_P5 = 2870177450012600261


def _rotl(x: int, r: int) -> int:
    return ((x << r) | (x >> (64 - r))) & _M


def _round(acc: int, lane: int) -> int:
    acc = (acc + lane * _P2) & _M
    return (_rotl(acc, 31) * _P1) & _M


def _merge(acc: int, val: int) -> int:
    acc ^= _round(0, val)
    return (acc * _P1 + _P4) & _M


def xxh64(data: bytes, seed: int = 0) -> int:
    n = len(data)
    p = 0
    if n >= 32:
        v1 = (seed + _P1 + _P2) & _M
        v2 = (seed + _P2) & _M
        v3 = seed & _M
        v4 = (seed - _P1) & _M
        while p + 32 <= n:
            a, b, c, d = struct.unpack_from("<4Q", data, p)
            v1, v2, v3, v4 = _round(v1, a), _round(v2, b), _round(v3, c), _round(v4, d)
            p += 32
        h = (_rotl(v1, 1) + _rotl(v2, 7) + _rotl(v3, 12) + _rotl(v4, 18)) & _M
        for v in (v1, v2, v3, v4):
            h = _merge(h, v)
    else:
        h = (seed + _P5) & _M
    h = (h + n) & _M
    while p + 8 <= n:
        (k,) = struct.unpack_from("<Q", data, p)
        h ^= _round(0, k)
        h = (_rotl(h, 27) * _P1 + _P4) & _M
        p += 8
    if p + 4 <= n:
        (k,) = struct.unpack_from("<I", data, p)
        h ^= (k * _P1) & _M
        h = (_rotl(h, 23) * _P2 + _P3) & _M
        p += 4
    while p < n:
        h ^= (data[p] * _P5) & _M
        h = (_rotl(h, 11) * _P1) & _M
        p += 1
    h ^= h >> 33
    h = (h * _P2) & _M
    h ^= h >> 29
    h = (h * _P3) & _M
    h ^= h >> 32
    return h


def xxh64_hex(data: bytes) -> str:
    return "%016x" % xxh64(data)


# ---------------------------------------------------------------------------
# GameCube disc (GCM) reader.
# ---------------------------------------------------------------------------

class Disc:
    """Reads files from a GameCube disc image by path, without extracting."""

    def __init__(self, path: str):
        self.path = path
        self.f = open(path, "rb")
        try:
            self._parse()
        except Exception:
            self.f.close()
            raise

    def _parse(self):
        head = self.f.read(0x440)
        if len(head) < 0x440 or head[0x1C:0x20] != b"\xC2\x33\x9F\x3D":
            raise SailError("not a GameCube disc image: " + self.path)
        self.game_id = head[:6].decode("ascii", "replace")
        self.revision = head[7]
        fst_off, fst_size = struct.unpack_from(">II", head, 0x424)
        self.f.seek(fst_off)
        fst = self.f.read(fst_size)
        count = struct.unpack_from(">I", fst, 8)[0]
        names = count * 12
        self.entries = {}
        stack = [("", count)]
        for i in range(1, count):
            flags_name, off, length = struct.unpack_from(">III", fst, i * 12)
            is_dir = flags_name >> 24
            name_off = flags_name & 0xFFFFFF
            end = fst.index(b"\0", names + name_off)
            name = fst[names + name_off:end].decode("shift_jis")
            while stack and i >= stack[-1][1]:
                stack.pop()
            parent = stack[-1][0] if stack else ""
            full = (parent + "/" + name) if parent else name
            if is_dir:
                stack.append((full, length))
            else:
                self.entries[full.lower()] = (off, length)

    def close(self):
        self.f.close()

    def read(self, path: str) -> bytes:
        key = path.lower().strip("/")
        if key not in self.entries:
            raise SailError("file not on disc: " + path)
        off, length = self.entries[key]
        self.f.seek(off)
        return self.f.read(length)


# ---------------------------------------------------------------------------
# Yaz0 and RARC.
# ---------------------------------------------------------------------------

def yaz0_decompress(data: bytes) -> bytes:
    if data[:4] != b"Yaz0":
        return data
    try:
        return _yaz0(data)
    except IndexError:
        raise SailError("truncated Yaz0 stream") from None


def _yaz0(data: bytes) -> bytes:
    size = struct.unpack_from(">I", data, 4)[0]
    out = bytearray()
    src = 16
    bits = 0
    code = 0
    n = len(data)
    while len(out) < size:
        if not bits:
            code = data[src]
            src += 1
            bits = 8
        if code & 0x80:
            out.append(data[src])
            src += 1
        else:
            a, b = data[src], data[src + 1]
            src += 2
            dist = ((a & 15) << 8) + b + 1
            count = a >> 4
            if count:
                count += 2
            else:
                count = data[src] + 18
                src += 1
            if dist > len(out):
                raise SailError("bad Yaz0 back reference")
            start = len(out) - dist
            for k in range(count):
                out.append(out[start + k])
        bits -= 1
        code = (code << 1) & 0xFF
        if src > n:
            raise SailError("truncated Yaz0 stream")
    return bytes(out[:size])


def rarc_files(data: bytes) -> dict:
    """Return {lower-case path inside the archive: file bytes}. Handles per-file Yaz0."""
    if data[:4] != b"RARC":
        raise SailError("not a RARC archive")
    base = struct.unpack_from(">I", data, 8)[0]
    payload = base + struct.unpack_from(">I", data, 12)[0]
    n_nodes, node_off, _n_files, file_off, _str_size, str_off = struct.unpack_from(">6I", data, base)
    nodes = base + node_off
    entries = base + file_off
    strings = base + str_off

    def name_at(o: int) -> str:
        s = strings + o
        return data[s:data.index(b"\0", s)].decode("shift_jis")

    out = {}

    def walk(index: int, prefix: str, depth: int):
        if depth > 16 or index >= n_nodes:
            raise SailError("bad RARC directory graph")
        node = nodes + index * 16
        count, first = struct.unpack_from(">HI", data, node + 10)
        for i in range(first, first + count):
            e = entries + i * 20
            flags = data[e + 4]
            name = name_at(struct.unpack_from(">H", data, e + 6)[0])
            off, size = struct.unpack_from(">II", data, e + 8)
            if name in (".", ".."):
                continue
            path = prefix + name
            if flags & 2:
                walk(off, path + "/", depth + 1)
            else:
                blob = data[payload + off:payload + off + size]
                if flags & 0x80 and blob[:4] == b"Yaz0":
                    blob = yaz0_decompress(blob)
                out[path.lower()] = blob

    walk(0, "", 0)
    return out


def open_archive(disc: Disc, path: str) -> dict:
    return rarc_files(yaz0_decompress(disc.read(path)))


# ---------------------------------------------------------------------------
# BTI header, palette and image decoding (the GX formats the sail uses).
# ---------------------------------------------------------------------------

FMT_C8 = 9
FMT_CMPR = 14
PAL_RGB565 = 1
PAL_RGB5A3 = 2

BLOCK = {9: (8, 4, 32), 14: (8, 8, 32)}


class Bti:
    """One BTI header plus its raw image and palette bytes (base level only)."""

    def __init__(self, blob: bytes, at: int = 0):
        h = blob[at:at + 32]
        if len(h) < 32:
            raise SailError("truncated BTI header")
        self.format = h[0]
        self.width, self.height = struct.unpack_from(">HH", h, 2)
        self.palette_format = h[9]
        self.num_colors = struct.unpack_from(">H", h, 10)[0]
        pal_off = struct.unpack_from(">I", h, 12)[0]
        img_off = struct.unpack_from(">I", h, 28)[0]
        if self.format not in BLOCK:
            raise SailError("unsupported GX format %d" % self.format)
        bw, bh, bs = BLOCK[self.format]
        size = ((self.width + bw - 1) // bw) * ((self.height + bh - 1) // bh) * bs
        self.image = blob[at + img_off:at + img_off + size]
        if len(self.image) != size:
            raise SailError("truncated BTI image")
        self.palette = b""
        if self.format == FMT_C8:
            self.palette = blob[at + pal_off:at + pal_off + self.num_colors * 2]
            if len(self.palette) != self.num_colors * 2:
                raise SailError("truncated BTI palette")

    def filename(self) -> str:
        """Dolphin-format replacement name from the guest bytes (renderer rules)."""
        parts = ["tex1_%dx%d" % (self.width, self.height), xxh64_hex(self.image)]
        if self.format == FMT_C8:
            used = self.indices()
            lo, hi = min(used), max(used)
            parts.append(xxh64_hex(self.palette[lo * 2:(hi + 1) * 2]))
        parts.append(str(self.format))
        return "_".join(parts) + ".png"

    def indices(self) -> list:
        """C8 indices in raster order (width x height)."""
        if self.format != FMT_C8:
            raise SailError("indices() needs a C8 texture")
        w, h = self.width, self.height
        out = [0] * (w * h)
        pos = 0
        for by in range(0, h, 4):
            for bx in range(0, w, 8):
                for y in range(4):
                    for x in range(8):
                        if bx + x < w and by + y < h:
                            out[(by + y) * w + bx + x] = self.image[pos]
                        pos += 1
        return out


def _e5(v): return (v << 3) | (v >> 2)
def _e6(v): return (v << 2) | (v >> 4)
def _e4(v): return (v << 4) | v
def _e3(v): return (v << 5) | (v << 2) | (v >> 1)


def rgb565_to_rgba(v: int) -> tuple:
    return (_e5((v >> 11) & 31), _e6((v >> 5) & 63), _e5(v & 31), 255)


def rgb5a3_to_rgba(v: int) -> tuple:
    if v & 0x8000:
        return (_e5((v >> 10) & 31), _e5((v >> 5) & 31), _e5(v & 31), 255)
    return (_e4((v >> 8) & 15), _e4((v >> 4) & 15), _e4(v & 15), _e3((v >> 12) & 7))


def palette_colors(raw: bytes, palette_format: int) -> list:
    conv = {PAL_RGB565: rgb565_to_rgba, PAL_RGB5A3: rgb5a3_to_rgba}.get(palette_format)
    if conv is None:
        raise SailError("unsupported palette format %d" % palette_format)
    return [conv(v) for (v,) in struct.iter_unpack(">H", raw)]


def decode_c8(bti: Bti, palette: list = None) -> list:
    """Raster list of RGBA tuples. `palette` overrides the BTI's own palette."""
    pal = palette if palette is not None else palette_colors(bti.palette, bti.palette_format)
    return [pal[i] for i in bti.indices()]


def cmpr_endpoints_colors(c0: int, c1: int) -> list:
    r0, g0, b0, _ = rgb565_to_rgba(c0)
    r1, g1, b1, _ = rgb565_to_rgba(c1)
    if c0 > c1:
        return [(r0, g0, b0, 255), (r1, g1, b1, 255),
                ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255),
                ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255)]
    return [(r0, g0, b0, 255), (r1, g1, b1, 255),
            (r0 // 2 + r1 // 2, g0 // 2 + g1 // 2, b0 // 2 + b1 // 2, 255), (0, 0, 0, 0)]


def decode_cmpr(image: bytes, width: int, height: int) -> list:
    """Raster list of RGBA tuples from CMPR data (8x8 tiles of four 4x4 sub-blocks)."""
    out = [(0, 0, 0, 0)] * (width * height)
    pos = 0
    for ty in range(0, height, 8):
        for tx in range(0, width, 8):
            for sub in range(4):
                sx, sy = tx + (sub % 2) * 4, ty + (sub // 2) * 4
                c0, c1, bits = struct.unpack_from(">HHI", image, pos)
                pos += 8
                cols = cmpr_endpoints_colors(c0, c1)
                for i in range(16):
                    x, y = sx + i % 4, sy + i // 4
                    if x < width and y < height:
                        out[y * width + x] = cols[(bits >> ((15 - i) * 2)) & 3]
    return out


def decode_bti(bti: Bti) -> list:
    if bti.format == FMT_C8:
        return decode_c8(bti)
    return decode_cmpr(bti.image, bti.width, bti.height)


# ---------------------------------------------------------------------------
# J3D (BDL) TEX1 lookup.
# ---------------------------------------------------------------------------

def bdl_texture(blob: bytes, name: str) -> Bti:
    if blob[:4] != b"J3D2" or blob[4:8] not in (b"bdl4", b"bmd3", b"bmd2"):
        raise SailError("not a J3D2 model")
    count = struct.unpack_from(">I", blob, 12)[0]
    at = 32
    for _ in range(count):
        magic, size = struct.unpack_from(">4sI", blob, at)
        if size < 8:
            raise SailError("bad J3D chunk")
        if magic == b"TEX1":
            n, hdr, strs = struct.unpack_from(">HxxII", blob, at + 8)
            tbl = at + strs
            nn = struct.unpack_from(">H", blob, tbl)[0]
            for i in range(nn):
                so = struct.unpack_from(">H", blob, tbl + 6 + i * 4)[0]
                s = tbl + so
                if blob[s:blob.index(b"\0", s)].decode("shift_jis") == name:
                    return Bti(blob, at + hdr + i * 32)
        at += size
    raise SailError("texture %r not found in model" % name)


# ---------------------------------------------------------------------------
# Texture sources and the reproduction.
# ---------------------------------------------------------------------------

SOURCES = {
    "sail": ("res/Object/Ship.arc", "tex/new_ho1.bti", None),
    "icon": ("res/Msg/itemicon.arc", "timg/sail_00.bti", None),
    "itemget": ("res/Object/Vho.arc", "bdl/vho.bdl", "Vho"),
}

# What wwrando's image encoder leaves in the RGB of fully transparent pixels
# of the icon. Release 1.10.0 and older write 000000. Current master runs
# libimagequant first, which turns the transparent entry into 447744. The
# alpha is 0 either way, so only filtering at the icon's edge can show it.
TRANSPARENT_CURRENT = (0x44, 0x77, 0x44)
TRANSPARENT_LEGACY = (0, 0, 0)


def load_vanilla(disc: Disc, key: str) -> Bti:
    arc, member, tex = SOURCES[key]
    files = open_archive(disc, arc)
    if member not in files:
        raise SailError("%s has no member %s" % (arc, member))
    if tex:
        return bdl_texture(files[member], tex)
    return Bti(files[member])


def hex8(c) -> str:
    return "%02x%02x%02x%02x" % tuple(c)


def unhex8(s: str) -> tuple:
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), int(s[6:8], 16))


def keyed_raster(vanilla: Bti):
    """(keys, raster). A key is what the colour map is indexed by: the palette
    index for C8 textures, the decoded colour (hex) for CMPR textures."""
    if vanilla.format == FMT_C8:
        idx = vanilla.indices()
        pal = palette_colors(vanilla.palette, vanilla.palette_format)
        return [str(i) for i in idx], [pal[i] for i in idx]
    raster = decode_cmpr(vanilla.image, vanilla.width, vanilla.height)
    return [hex8(p) for p in raster], raster


def finish(pixels: list, transparent) -> list:
    """Set the RGB of every fully transparent pixel (the encoder's choice, see above)."""
    if transparent is None:
        return pixels
    t = (transparent[0], transparent[1], transparent[2], 0)
    return [t if p[3] == 0 else p for p in pixels]


def reproduce(tex: dict, vanilla: Bti, transparent=None) -> list:
    """Raster of RGBA tuples: vanilla pixels, recoloured by tex['map'], then tex['delta']."""
    colors = [unhex8(s) for s in tex["colors"]]
    cmap = tex["map"]
    keys, raster = keyed_raster(vanilla)
    out = [colors[cmap[k]] if k in cmap else p for k, p in zip(keys, raster)]
    pos = -1
    delta = tex["delta"]
    for j in range(0, len(delta), 2):
        pos += 1 + delta[j]
        out[pos] = colors[delta[j + 1]]
    return finish(out, transparent)


def to_png(pixels: list, width: int, height: int, path: str):
    from PIL import Image
    im = Image.frombytes("RGBA", (width, height), bytes(b for p in pixels for b in p))
    im.save(path, optimize=True)


def load_data(data_path: str = DATA_FILE) -> dict:
    with open(data_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if data.get("format") != 1:
        raise SailError("unsupported data file format")
    return data


def check_disc(disc: Disc, data: dict):
    """Per texture: returns ({key: vanilla Bti}, {key: refusal message})."""
    if disc.game_id != "GZLE01" or disc.revision != 0:
        raise SailError("expected GZLE01 rev 0 (USA), found %s rev %d" % (disc.game_id, disc.revision))
    ok, refused = {}, {}
    for key in SOURCES:
        arc, member, tex = SOURCES[key]
        label = "%s %s%s" % (arc, member, " (texture %s)" % tex if tex else "")
        try:
            vanilla = load_vanilla(disc, key)
            got = vanilla.filename()
        except SailError as e:
            refused[key] = "%s: cannot read it (%s)" % (label, e)
            continue
        want = data["textures"][key]["name"]
        if got != want:
            refused[key] = ("%s is not the vanilla texture (modded asset or other disc): "
                            "expected %s, found %s" % (label, want, got))
        else:
            ok[key] = vanilla
    return ok, refused


def build(iso_path: str, out_dir: str, data_path: str = DATA_FILE, legacy_transparent: bool = False):
    """Write the PNGs for every vanilla texture. Returns ({key: file name}, {key: refusal message})."""
    data = load_data(data_path)
    disc = Disc(iso_path)
    try:
        ok, refused = check_disc(disc, data)
    finally:
        disc.close()
    written = {}
    if ok:
        os.makedirs(out_dir, exist_ok=True)
    for key, vanilla in ok.items():
        tex = data["textures"][key]
        trgb = None
        if "transparent_rgb" in tex:
            trgb = TRANSPARENT_LEGACY if legacy_transparent else unhex8(tex["transparent_rgb"] + "00")[:3]
        pixels = reproduce(tex, vanilla, trgb)
        to_png(pixels, vanilla.width, vanilla.height, os.path.join(out_dir, tex["name"]))
        written[key] = tex["name"]
    return written, refused


# ---------------------------------------------------------------------------
# Maintainer part: derive swift_sail_data.json from wwrando's PNGs.
# Not used at run time. Needs the wwrando assets folder (MIT). The encoder
# below follows gclib's texture_utils.py (MIT, LagoLunatic): the picture the
# game shows after wwrando writes a PNG into a BTI is decode(encode(PNG)).
# ---------------------------------------------------------------------------

WWRANDO_PNG = {
    "sail": "swift sail texture.png",
    "icon": "swift sail icon.png",
    "itemget": "swift sail item get texture.png",
}


def enc_rgb565(c) -> int:
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def enc_rgb5a3(c) -> int:
    r, g, b, a = c
    if a != 255:
        return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
    return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)


def _l1(a, b) -> int:
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])


def _cmpr_key_colors(cols: list):
    best, c1, c2 = -1, None, None
    for i in range(len(cols)):
        for j in range(i + 1, len(cols)):
            d = _l1(cols[i], cols[j])
            if d > best:
                best, c1, c2 = d, cols[i], cols[j]
    if best == -1:
        return (0, 0, 0, 255), (255, 255, 255, 255)
    c1 = (c1[0], c1[1], c1[2], 255)
    c2 = (c2[0], c2[1], c2[2], 255)
    if (c1[0] >> 3) == (c2[0] >> 3) and (c1[1] >> 2) == (c2[1] >> 2) and (c1[2] >> 3) == (c2[2] >> 3):
        black = (c1[0] >> 3) == 0 and (c1[1] >> 2) == 0 and (c1[2] >> 3) == 0
        c2 = (255, 255, 255, 255) if black else (0, 0, 0, 255)
    return c1, c2


def _nearest(color, palette):
    if color in palette:
        return color
    if color[3] < 16:
        for p in palette:
            if p[3] == 0:
                return p
    best, best_d = palette[0], 0x7FFFFFFF
    for p in palette:
        d = _l1(color, p)
        if d < best_d:
            if d == 0:
                return p
            best, best_d = p, d
    return best


def encode_cmpr_like_wwrando(pixels: list, width: int, height: int) -> bytes:
    out = bytearray()
    for ty in range(0, height, 8):
        for tx in range(0, width, 8):
            for sub in range(4):
                sx, sy = tx + (sub % 2) * 4, ty + (sub // 2) * 4
                cols, needs_transparent = [], False
                for i in range(16):
                    x, y = sx + i % 4, sy + i // 4
                    if x >= width or y >= height:
                        continue
                    p = pixels[y * width + x]
                    if p[3] < 16:
                        needs_transparent = True
                    else:
                        cols.append(p)
                c0, c1 = _cmpr_key_colors(cols)
                q0, q1 = enc_rgb565(c0), enc_rgb565(c1)
                if (needs_transparent and q0 > q1) or (not needs_transparent and q0 < q1):
                    q0, q1, c0, c1 = q1, q0, c1, c0
                palette = cmpr_endpoints_colors(q0, q1)
                palette[0], palette[1] = c0, c1
                bits = 0
                for i in range(16):
                    x, y = sx + i % 4, sy + i // 4
                    if x >= width or y >= height:
                        continue
                    p = pixels[y * width + x]
                    n = palette.index(p) if p in palette else palette.index(_nearest(p, palette))
                    bits |= n << ((15 - i) * 2)
                out += struct.pack(">HHI", q0, q1, bits)
    return bytes(out)


def wwrando_result(rgba: list, width: int, height: int, vanilla: Bti, transparent=None) -> list:
    """RGBA the game shows after wwrando re-encodes `rgba` into the vanilla texture's format.
    `transparent` is the RGB the encoder leaves in fully transparent pixels (None: keep as decoded)."""
    if vanilla.format == FMT_CMPR:
        return decode_cmpr(encode_cmpr_like_wwrando(rgba, width, height), width, height)
    if vanilla.palette_format == PAL_RGB565:
        enc, dec = enc_rgb565, rgb565_to_rgba
    else:
        enc, dec = enc_rgb5a3, rgb5a3_to_rgba
    cache = {}
    out = []
    for p in rgba:
        if p not in cache:
            cache[p] = dec(enc(p))
        out.append(cache[p])
    return finish(out, transparent)


def read_png_rgba(path: str):
    from PIL import Image
    im = Image.open(path).convert("RGBA")
    raw = im.tobytes()
    return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)], im.width, im.height


def derive_texture(vanilla: Bti, target: list, transparent=None) -> dict:
    """Colour map (per palette index or per decoded colour) plus a sparse delta that give `target`."""
    from collections import Counter, defaultdict
    keys, raster = keyed_raster(vanilla)
    tr = (0, 0, 0) if transparent else None
    tgt = finish(target, tr)
    van = finish(raster, tr)
    votes = defaultdict(Counter)
    vanilla_of = {}
    for k, v, t in zip(keys, van, tgt):
        votes[k][t] += 1
        vanilla_of[k] = v
    table, cmap, chosen = [], {}, {}

    def color_index(c):
        s = hex8(c)
        if s not in table:
            table.append(s)
        return table.index(s)

    for k in sorted(votes, key=lambda s: (len(s), s)):
        top = sorted(votes[k].items(), key=lambda kv: (-kv[1], kv[0]))[0][0]
        chosen[k] = top
        if top != vanilla_of[k]:
            cmap[k] = color_index(top)
    delta, last = [], -1
    for i, (k, t) in enumerate(zip(keys, tgt)):
        if chosen[k] != t:
            delta += [i - last - 1, color_index(t)]
            last = i
    tex = {"name": vanilla.filename(), "format": "C8" if vanilla.format == FMT_C8 else "CMPR",
           "size": [vanilla.width, vanilla.height],
           "key": "palette index" if vanilla.format == FMT_C8 else "decoded colour",
           "colors": table, "map": cmap, "delta": delta}
    if transparent:
        tex["transparent_rgb"] = "%02x%02x%02x" % tuple(transparent)
    return tex


def write_data(data: dict, path: str):
    """One line per field so a diff of the data file stays readable."""
    def dumps(v):
        return json.dumps(v, separators=(",", ":"))
    lines = ["{", ' "format": 1,', ' "credit": %s,' % dumps(data["credit"]), ' "textures": {']
    keys = list(data["textures"])
    for n, k in enumerate(keys):
        t = data["textures"][k]
        lines.append('  "%s": {' % k)
        fields = list(t)
        for m, f in enumerate(fields):
            lines.append('   "%s": %s%s' % (f, dumps(t[f]), "," if m + 1 < len(fields) else ""))
        lines.append("  }%s" % ("," if n + 1 < len(keys) else ""))
    lines += [" }", "}", ""]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


CREDIT = ("Colour tables and pixel deltas that turn the vanilla sail textures into the red Swift Sail "
          "textures of the Wind Waker Randomizer (wwrando, MIT, Copyright (c) 2018 LagoLunatic; the "
          "randomizer credits Hypatia for textures and MelonSpeedruns for graphic design). "
          "Original artwork: Nintendo. This file holds no disc pixels and no image.")


def derive(iso_path: str, assets_dir: str, out_json: str) -> dict:
    disc = Disc(iso_path)
    try:
        if disc.game_id != "GZLE01" or disc.revision != 0:
            raise SailError("expected GZLE01 rev 0 (USA)")
        textures = {}
        for key in SOURCES:
            vanilla = load_vanilla(disc, key)
            rgba, w, h = read_png_rgba(os.path.join(assets_dir, WWRANDO_PNG[key]))
            if (w, h) != (vanilla.width, vanilla.height):
                raise SailError("%s: size differs from the vanilla texture" % key)
            transparent = TRANSPARENT_CURRENT if any(p[3] < 255 for p in rgba) else None
            target = wwrando_result(rgba, w, h, vanilla, transparent)
            textures[key] = derive_texture(vanilla, target, transparent)
    finally:
        disc.close()
    data = {"format": 1, "credit": CREDIT, "textures": textures}
    write_data(data, out_json)
    return data


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Build the Swift Sail replacement textures from your own disc.")
    ap.add_argument("iso", help="path to the GZLE01 (USA) disc image")
    ap.add_argument("out", help="output folder for the three PNGs (with --derive: the data file to write)")
    ap.add_argument("--legacy-transparent", action="store_true",
                    help="write 000000 into the transparent pixels of the icon (wwrando 1.10.0 and older)")
    ap.add_argument("--data", default=DATA_FILE, help=argparse.SUPPRESS)
    ap.add_argument("--derive", metavar="WWRANDO_ASSETS",
                    help="maintainers: rebuild the data file from wwrando's assets folder")
    args = ap.parse_args(argv)
    try:
        if args.derive:
            derive(args.iso, args.derive, args.out)
            print("wrote " + args.out)
            return 0
        written, refused = build(args.iso, args.out, args.data, args.legacy_transparent)
    except SailError as e:
        print("swift_sail: " + str(e), file=sys.stderr)
        return 2
    except OSError as e:
        print("swift_sail: " + str(e), file=sys.stderr)
        return 1
    for key, name in written.items():
        print("%-8s %s" % (key, name))
    for key, msg in refused.items():
        print("swift_sail: refused %s: %s" % (key, msg), file=sys.stderr)
    return 3 if refused else 0


if __name__ == "__main__":
    sys.exit(main())
