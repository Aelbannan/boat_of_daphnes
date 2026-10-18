"""Tests for scripts/windows/swift_sail.py.

Run: python -m unittest tests.test_swift_sail   (or: python tests/test_swift_sail.py)

Most tests need no disc. They use small synthetic textures, archives and a
synthetic disc image. Two test classes need local files and skip when absent:
  - the real GZLE01 disc (BOD_DISC, scripts/bod_paths.py), and
  - wwrando's assets folder (read at test time only, never copied).
"""
import json
import os
import random
import struct
import sys
import tempfile
import unittest
import sys as _bod_sys
from pathlib import Path as _BodPath
_bod_sys.path.insert(0, str(_BodPath(__file__).resolve().parents[1] / 'scripts'))
try:
    from bod_paths import local_path, scratch  # noqa: E402  (scripts/bod_paths.py)
except ImportError:  # a release's copy of this script, without the repository's scripts/ beside it
    import os as _bod_os

    def local_path(name, fallback=""):
        return _bod_os.environ.get(name) or fallback

    def scratch():
        return _BodPath(local_path("BOD_SCRATCH", "scratch"))

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts", "windows"))

import swift_sail as S  # noqa: E402

ISO = local_path("BOD_DISC")
WWRANDO_ASSETS = str(Path(local_path("BOD_WWRANDO")) / "assets")

# Section 2 of docs/fork/research/swift-sail-textures.md.
DOC_NAMES = {
    "sail": "tex1_128x128_3f34314685b4cb4d_e9a9f7e04ef9893f_9.png",
    "icon": "tex1_48x48_22e7a81247c4caa5_fe1212280187026e_9.png",
    "itemget": "tex1_128x128_301e8acf5dcf785d_14.png",
}


# ---------------------------------------------------------------------------
# Builders for synthetic data.
# ---------------------------------------------------------------------------

def yaz0_literal(data: bytes) -> bytes:
    out = bytearray(b"Yaz0" + struct.pack(">I", len(data)) + bytes(8))
    for i in range(0, len(data), 8):
        chunk = data[i:i + 8]
        out.append(0xFF >> (8 - len(chunk)) << (8 - len(chunk)) & 0xFF)
        out += chunk
    return bytes(out)


def make_bti_header(fmt, w, h, pal_fmt, ncolors, pal_off, img_off) -> bytes:
    hdr = bytearray(32)
    hdr[0] = fmt
    struct.pack_into(">HH", hdr, 2, w, h)
    hdr[8] = 1 if ncolors else 0
    hdr[9] = pal_fmt
    struct.pack_into(">HI", hdr, 10, ncolors, pal_off)
    hdr[24] = 1
    struct.pack_into(">I", hdr, 28, img_off)
    return bytes(hdr)


def c8_blocks(indices, w, h) -> bytes:
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 8):
            for y in range(4):
                for x in range(8):
                    out.append(indices[(by + y) * w + bx + x] if bx + x < w and by + y < h else 0)
    return bytes(out)


def make_c8_bti(indices, w, h, palette_words, pal_fmt) -> bytes:
    img = c8_blocks(indices, w, h)
    pal = b"".join(struct.pack(">H", v) for v in palette_words)
    return make_bti_header(S.FMT_C8, w, h, pal_fmt, len(palette_words), 0x20 + len(img), 0x20) + img + pal


def make_bdl(textures) -> bytes:
    """textures: list of (name, cmpr bytes, w, h). Returns a J3D2 bdl4 with one TEX1 chunk."""
    n = len(textures)
    headers_at = 0x20
    names_at = headers_at + 32 * n
    names = b"".join(t[0].encode() + b"\0" for t in textures)
    table = struct.pack(">HH", n, 0xFFFF)
    off = 4 + 4 * n
    for t in textures:
        table += struct.pack(">HH", 0, off)
        off += len(t[0]) + 1
    table += names
    data_at = names_at + len(table)
    data_at += -data_at % 32
    hdrs, blob = b"", b""
    for i, (name, img, w, h) in enumerate(textures):
        img_off = data_at + len(blob) - (headers_at + 32 * i)
        hdrs += make_bti_header(S.FMT_CMPR, w, h, 0, 0, 0, img_off)
        blob += img
    chunk = bytearray(b"TEX1" + struct.pack(">I", 0) + struct.pack(">HH", n, 0) + struct.pack(">II", headers_at, names_at))
    chunk += bytes(0x20 - len(chunk))
    chunk += hdrs + table
    chunk += bytes(data_at - len(chunk)) + blob
    struct.pack_into(">I", chunk, 4, len(chunk))
    return b"J3D2bdl4" + struct.pack(">II", 32 + len(chunk), 1) + bytes(16) + bytes(chunk)


def make_rarc(tree: dict, yaz0_members=()) -> bytes:
    """tree: {name: bytes | dict}. yaz0_members: names of files stored Yaz0-compressed (flag 0x80)."""
    nodes, entries, strings, payload, names = [], [], bytearray(), bytearray(), {}

    def name_off(s):
        if s not in names:
            names[s] = len(strings)
            strings.extend(s.encode() + b"\0")
        return names[s]

    def build(label, content):
        idx = len(nodes)
        nodes.append(None)
        first = len(entries)
        entries.extend([None] * len(content))  # one contiguous run per directory
        for slot, (name, val) in enumerate(content.items(), first):
            entries[slot] = ("dir", name, build(name, val)) if isinstance(val, dict) else ("file", name, val)
        nodes[idx] = (label, first, len(content))
        return idx

    build("ROOT", tree)
    for n in nodes:
        name_off(n[0])
    for e in entries:
        name_off(e[1])
    node_off = 0x20
    ent_off = node_off + 16 * len(nodes)
    ent_off += -ent_off % 32
    str_off = ent_off + 20 * len(entries)
    str_len = len(strings) + (-len(strings) % 32)
    data_off = str_off + str_len
    ent_bytes = bytearray()
    for i, e in enumerate(entries):
        if e[0] == "dir":
            ent_bytes += struct.pack(">HHBBHII", 0xFFFF, 0, 2, 0, name_off(e[1]), e[2], 16) + bytes(4)
        else:
            blob, flags = e[2], 0x11
            if e[1] in yaz0_members:
                blob, flags = yaz0_literal(blob), flags | 0x80
            ent_bytes += struct.pack(">HHBBHII", i, 0, flags, 0, name_off(e[1]), len(payload), len(blob)) + bytes(4)
            payload += blob + bytes(-len(blob) % 32)
    node_bytes = b"".join(struct.pack(">4sIHHI", n[0][:4].upper().ljust(4).encode(), name_off(n[0]), 0, n[2], n[1])
                          for n in nodes)
    body = bytearray(struct.pack(">6I", len(nodes), node_off, len(entries), ent_off, str_len, str_off) + bytes(8))
    body += node_bytes
    body += bytes(ent_off - len(body))
    body += ent_bytes
    body += bytes(str_off - len(body))
    body += strings + bytes(str_len - len(strings))
    body += payload
    head = b"RARC" + struct.pack(">IIII", 0x20 + len(body), 0x20, data_off, len(payload)) + bytes(12)
    return head + bytes(body)


def make_gcm(files: dict, game_id=b"GZLE01", revision=0) -> bytes:
    """files: nested dict of directories and file bytes. Returns a minimal GameCube disc image."""
    entries = []  # (is_dir, name, offset_or_parent, length_or_next)
    names = bytearray()
    blobs = []

    def walk(tree, parent):
        for name, val in tree.items():
            pos = len(entries)
            noff = len(names)
            names.extend(name.encode() + b"\0")
            if isinstance(val, dict):
                entries.append([1, noff, parent, 0])
                walk(val, pos + 1)
                entries[pos][3] = len(entries) + 1
            else:
                entries.append([0, noff, 0, len(val)])
                blobs.append((pos, val))

    walk(files, 0)
    fst_off = 0x440
    fst_len = 12 * (len(entries) + 1) + len(names)
    cursor = fst_off + fst_len
    cursor += -cursor % 32
    body = bytearray()
    for pos, val in blobs:
        entries[pos][2] = cursor + len(body)
        body += val + bytes(-len(val) % 32)
    fst = struct.pack(">III", 1 << 24, 0, len(entries) + 1)
    for e in entries:
        fst += struct.pack(">III", (e[0] << 24) | e[1], e[2], e[3])
    fst += bytes(names)
    head = bytearray(0x440)
    head[0:6] = game_id
    head[7] = revision
    head[0x1C:0x20] = b"\xC2\x33\x9F\x3D"
    struct.pack_into(">II", head, 0x424, fst_off, len(fst))
    return bytes(head) + fst + bytes(cursor - fst_off - len(fst)) + bytes(body)


# Synthetic stand-ins for the three textures.
W, H = 16, 8


def synth_sail(tweak=False):
    idx = [(x + 2 * y) % 5 for y in range(H) for x in range(W)]
    pal = [0xFFFF, 0x07E0, 0xF800, 0x001F, 0x8410]
    if tweak:
        pal[4] = 0x1234
    return make_c8_bti(idx, W, H, pal, S.PAL_RGB565)


def synth_icon():
    idx = [0 if x < 4 else 1 + (x + y) % 3 for y in range(H) for x in range(W)]
    pal = [0x0000, 0x8000 | (31 << 10), 0x7000 | 0x0F0F, 0x8000 | 31]
    return make_c8_bti(idx, W, H, pal, S.PAL_RGB5A3)


def synth_cmpr():
    out = bytearray()
    rnd = random.Random(5)
    for _ in range(W * W // 16):
        c0, c1 = sorted((rnd.randrange(65536), rnd.randrange(65536)), reverse=True)
        out += struct.pack(">HHI", c0, c1, rnd.getrandbits(32))
    return bytes(out)


def _write(path, blob):
    with open(path, "wb") as f:
        f.write(blob)


def synth_iso(path, tweak_sail=False):
    sail = synth_sail(tweak_sail)
    ship = yaz0_literal(make_rarc({"tex": {"new_ho1.bti": sail}, "other": {"x.bin": b"abc"}}))
    icons = make_rarc({"timg": {"sail_00.bti": synth_icon(), "sail_01.bti": synth_icon()}},
                      yaz0_members=("sail_00.bti",))
    bdl = make_bdl([("ZAtoon", bytes(32), 8, 8), ("Vho", synth_cmpr(), W, W)])
    vho = make_rarc({"bdl": {"vho.bdl": bdl}})
    iso = make_gcm({"res": {"Object": {"Ship.arc": ship, "Vho.arc": vho}, "Msg": {"itemicon.arc": icons}}})
    _write(path, iso)


# ---------------------------------------------------------------------------
# Unit tests, no disc.
# ---------------------------------------------------------------------------

class TestHashAndCodecs(unittest.TestCase):
    def test_xxh64_vectors(self):
        self.assertEqual(S.xxh64(b""), 0xEF46DB3751D8E999)
        self.assertEqual(S.xxh64(b"a"), 0xD24EC4F1A98C6E5B)
        self.assertEqual(S.xxh64(b"abc"), 0x44BC2CF5AD770999)
        # 43 bytes: crosses the 32-byte stripe path.
        self.assertEqual(S.xxh64(b"The quick brown fox jumps over the lazy dog"), 0x0B242D361FDA71BC)
        # 200 bytes, value checked against the xxhash package.
        self.assertEqual(S.xxh64(bytes(range(200))), 0x50DC1079B99E879C)
        self.assertEqual(S.xxh64_hex(b""), "ef46db3751d8e999")

    def test_yaz0_literals_and_back_references(self):
        self.assertEqual(S.yaz0_decompress(b"not yaz0"), b"not yaz0")
        self.assertEqual(S.yaz0_decompress(yaz0_literal(b"hello world, hello")), b"hello world, hello")
        # literals 'a','b', then copy 6 bytes from distance 2: code bits 1,1,0.
        stream = b"Yaz0" + struct.pack(">I", 8) + bytes(8) + bytes([0xC0]) + b"ab" + bytes([0x40, 0x01])
        self.assertEqual(S.yaz0_decompress(stream), b"abababab")
        # long form: count nibble 0, extra byte + 18. 'x' then copy 20 from distance 1.
        stream = b"Yaz0" + struct.pack(">I", 21) + bytes(8) + bytes([0x80]) + b"x" + bytes([0x00, 0x00, 2])
        self.assertEqual(S.yaz0_decompress(stream), b"x" * 21)

    def test_yaz0_rejects_bad_reference(self):
        stream = b"Yaz0" + struct.pack(">I", 4) + bytes(8) + bytes([0x00]) + bytes([0x10, 0x05])
        with self.assertRaises(S.SailError):
            S.yaz0_decompress(stream)

    def test_rarc_paths_and_member_compression(self):
        arc = make_rarc({"tex": {"A.bti": b"AAAA"}, "top.bin": b"T" * 40, "z": {"deep": {"c.bin": b"zz"}}},
                        yaz0_members=("c.bin",))
        files = S.rarc_files(arc)
        self.assertEqual(files["tex/a.bti"], b"AAAA")
        self.assertEqual(files["top.bin"], b"T" * 40)
        self.assertEqual(files["z/deep/c.bin"], b"zz")
        with self.assertRaises(S.SailError):
            S.rarc_files(b"JUNK" + bytes(60))

    def test_gcm_reads_nested_files(self):
        iso = make_gcm({"a": {"b": {"c.txt": b"hello"}}, "d.bin": b"x" * 100, "e": {}})
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "x.iso")
            _write(p, iso)
            d = S.Disc(p)
            try:
                self.assertEqual(d.game_id, "GZLE01")
                self.assertEqual(d.read("a/b/c.txt"), b"hello")
                self.assertEqual(d.read("D.BIN"), b"x" * 100)
                with self.assertRaises(S.SailError):
                    d.read("e/none")
            finally:
                d.close()

    def test_color_expansion(self):
        self.assertEqual(S.rgb565_to_rgba(0xFFFF), (255, 255, 255, 255))
        self.assertEqual(S.rgb565_to_rgba(0xF800), (255, 0, 0, 255))
        self.assertEqual(S.rgb565_to_rgba(0x07E0), (0, 255, 0, 255))
        self.assertEqual(S.rgb565_to_rgba(0x8410), (132, 130, 132, 255))
        self.assertEqual(S.rgb5a3_to_rgba(0x8000 | (31 << 10)), (255, 0, 0, 255))
        self.assertEqual(S.rgb5a3_to_rgba(0x7F00), (255, 0, 0, 255 * 7 // 7))
        self.assertEqual(S.rgb5a3_to_rgba(0x0000), (0, 0, 0, 0))
        self.assertEqual(S.rgb5a3_to_rgba(0x2123)[3], (2 << 5) | (2 << 2) | (2 >> 1))

    def test_c8_block_order_and_palette(self):
        idx = [(x * 3 + y * 7) % 5 for y in range(H) for x in range(W)]
        pal = [0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000]
        bti = S.Bti(make_c8_bti(idx, W, H, pal, S.PAL_RGB565))
        self.assertEqual((bti.format, bti.width, bti.height, bti.num_colors), (S.FMT_C8, W, H, 5))
        self.assertEqual(bti.indices(), idx)
        rgba = S.decode_bti(bti)
        self.assertEqual(rgba[0], S.rgb565_to_rgba(pal[idx[0]]))
        self.assertEqual(rgba[3 * W + 9], S.rgb565_to_rgba(pal[idx[3 * W + 9]]))

    def test_c8_partial_block_edge(self):
        w, h = 12, 6
        idx = [(x + y) % 3 for y in range(h) for x in range(w)]
        bti = S.Bti(make_c8_bti(idx, w, h, [1, 2, 3], S.PAL_RGB565))
        self.assertEqual(bti.indices(), idx)

    def test_cmpr_four_and_three_color_modes(self):
        c0, c1 = 0xF800, 0x001F  # c0 > c1: four colours
        cols = S.cmpr_endpoints_colors(c0, c1)
        self.assertEqual(cols[0], (255, 0, 0, 255))
        self.assertEqual(cols[1], (0, 0, 255, 255))
        self.assertEqual(cols[2], ((2 * 255 + 0) // 3, 0, (0 + 255) // 3, 255))
        self.assertEqual(cols[3], ((255) // 3, 0, (2 * 255) // 3, 255))
        three = S.cmpr_endpoints_colors(0x001F, 0xF800)  # c0 < c1: transparent fourth colour
        self.assertEqual(three[2], (0 // 2 + 255 // 2, 0, 255 // 2 + 0, 255))
        self.assertEqual(three[3], (0, 0, 0, 0))

    def test_cmpr_tile_layout(self):
        # Four 4x4 sub-blocks of an 8x8 tile in order: top-left, top-right, bottom-left, bottom-right.
        blocks = b""
        for n in range(4):
            c0 = (n + 1) << 11 | 0x7FF
            idx = 0
            for i in range(16):
                idx |= (n % 2) << ((15 - i) * 2)  # all pixels use colour 0 or 1
            blocks += struct.pack(">HHI", 0xFFFF, c0 & 0x7FFF, idx)
        px = S.decode_cmpr(blocks, 8, 8)
        white = (255, 255, 255, 255)
        self.assertEqual(px[0], white)  # sub-block 0, index 0 -> colour 0 (0xFFFF)
        self.assertEqual(px[4], S.rgb565_to_rgba(((2) << 11 | 0x7FF) & 0x7FFF))  # sub-block 1, index 1
        self.assertEqual(px[4 * 8], white)  # sub-block 2
        self.assertEqual(px[4 * 8 + 4], S.rgb565_to_rgba(((4) << 11 | 0x7FF) & 0x7FFF))

    def test_cmpr_encoder_roundtrip_on_two_color_blocks(self):
        a, b = (200, 40, 40, 255), (230, 220, 190, 255)
        pixels = [a if (x // 2 + y // 2) % 2 else b for y in range(16) for x in range(16)]
        data = S.encode_cmpr_like_wwrando(pixels, 16, 16)
        out = S.decode_cmpr(data, 16, 16)
        q = lambda c: S.rgb565_to_rgba(S.enc_rgb565(c))  # noqa: E731
        self.assertEqual(set(out), {q(a), q(b)})
        # And the encoding is always the four-colour mode for opaque blocks.
        for i in range(0, len(data), 8):
            c0, c1, _ = struct.unpack_from(">HHI", data, i)
            self.assertGreaterEqual(c0, c1)

    def test_cmpr_encoder_handles_flat_and_black_blocks(self):
        flat = S.decode_cmpr(S.encode_cmpr_like_wwrando([(10, 20, 30, 255)] * 64, 8, 8), 8, 8)
        self.assertEqual(set(flat), {S.rgb565_to_rgba(S.enc_rgb565((10, 20, 30, 255)))})
        black = S.decode_cmpr(S.encode_cmpr_like_wwrando([(0, 0, 0, 255)] * 64, 8, 8), 8, 8)
        self.assertEqual(set(black), {(0, 0, 0, 255)})

    def test_bdl_texture_lookup(self):
        img = bytes(range(32))
        bdl = make_bdl([("ZAtoon", bytes(32), 8, 8), ("Vho", img * 4, 16, 16)])
        t = S.bdl_texture(bdl, "Vho")
        self.assertEqual((t.format, t.width, t.height), (S.FMT_CMPR, 16, 16))
        self.assertEqual(t.image, img * 4)
        self.assertEqual(S.bdl_texture(bdl, "ZAtoon").width, 8)
        with self.assertRaises(S.SailError):
            S.bdl_texture(bdl, "Nope")
        with self.assertRaises(S.SailError):
            S.bdl_texture(b"RARC" + bytes(60), "Vho")

    def test_filename_rules(self):
        sail = S.Bti(synth_sail())
        used = sail.indices()
        pal = sail.palette[min(used) * 2:(max(used) + 1) * 2]
        want = "tex1_%dx%d_%s_%s_9.png" % (W, H, S.xxh64_hex(sail.image), S.xxh64_hex(pal))
        self.assertEqual(sail.filename(), want)
        # CMPR names carry no palette hash.
        t = S.bdl_texture(make_bdl([("Vho", synth_cmpr(), W, W)]), "Vho")
        self.assertEqual(t.filename(), "tex1_%dx%d_%s_14.png" % (W, W, S.xxh64_hex(t.image)))

    def test_palette_hash_covers_only_used_entries(self):
        a = S.Bti(make_c8_bti([1, 2] * 16 * 4, W, H, [0xAAAA, 1, 2, 0xBBBB], S.PAL_RGB565))
        b = S.Bti(make_c8_bti([1, 2] * 16 * 4, W, H, [0x1111, 1, 2, 0x2222], S.PAL_RGB565))
        self.assertEqual(a.filename(), b.filename())


class TestDeltaAndMap(unittest.TestCase):
    def test_reproduce_applies_map_then_delta(self):
        bti = S.Bti(synth_sail())
        keys, raster = S.keyed_raster(bti)
        tex = {"colors": ["ff0000ff", "00ff00ff"], "map": {"0": 0}, "delta": [2, 1, 0, 1, 5, 0]}
        out = S.reproduce(tex, bti)
        want = list(raster)
        for i, k in enumerate(keys):
            if k == "0":
                want[i] = (255, 0, 0, 255)
        for pos, c in ((2, (0, 255, 0, 255)), (3, (0, 255, 0, 255)), (9, (255, 0, 0, 255))):
            want[pos] = c
        self.assertEqual(out, want)

    def test_reproduce_transparent_rgb(self):
        bti = S.Bti(synth_icon())
        out = S.reproduce({"colors": [], "map": {}, "delta": []}, bti, (1, 2, 3))
        self.assertTrue(all(p[:3] == (1, 2, 3) for p in out if p[3] == 0))
        self.assertTrue(any(p[3] == 0 for p in out))
        self.assertEqual(S.reproduce({"colors": [], "map": {}, "delta": []}, bti, None)[0], (0, 0, 0, 0))

    def test_derive_then_reproduce_is_exact(self):
        rnd = random.Random(3)
        for maker, transparent in ((synth_sail, None), (synth_icon, (0x44, 0x77, 0x44))):
            bti = S.Bti(maker())
            _, raster = S.keyed_raster(bti)
            target = list(raster)
            for i in range(0, len(target), 3):
                target[i] = (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256), 255)
            for i in range(1, len(target), 7):
                target[i] = (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256), 0 if transparent else 255)
            tex = S.derive_texture(bti, target, transparent)
            out = S.reproduce(tex, bti, transparent)
            self.assertEqual(out, S.finish(target, transparent))

    def test_derive_cmpr_is_exact(self):
        bti = S.bdl_texture(make_bdl([("Vho", synth_cmpr(), W, W)]), "Vho")
        _, raster = S.keyed_raster(bti)
        rnd = random.Random(9)
        target = [(p[0] ^ 8, p[1], p[2], 255) if rnd.random() < 0.7 else p for p in raster]
        tex = S.derive_texture(bti, target)
        self.assertEqual(S.reproduce(tex, bti), target)

    def test_data_file_is_small_and_holds_no_images(self):
        path = S.DATA_FILE
        self.assertLess(os.path.getsize(path), 64 * 1024)
        data = S.load_data(path)
        for key, tex in data["textures"].items():
            self.assertEqual(tex["name"], DOC_NAMES[key])
            self.assertEqual(len(tex["delta"]) % 2, 0)
            self.assertLess(len(tex["delta"]) // 2, tex["size"][0] * tex["size"][1] // 3)
            self.assertNotIn("pixels", tex)
            self.assertNotIn("image", tex)

    def test_wwrando_result_palette_snapping(self):
        bti = S.Bti(synth_sail())
        out = S.wwrando_result([(250, 3, 4, 255), (1, 2, 3, 255)] * 64, W, H, bti)
        self.assertEqual(out[0], S.rgb565_to_rgba(S.enc_rgb565((250, 3, 4, 255))))
        icon = S.Bti(synth_icon())
        out = S.wwrando_result([(250, 3, 4, 128), (9, 9, 9, 0)] * 64, W, H, icon, (0x44, 0x77, 0x44))
        self.assertEqual(out[1], (0x44, 0x77, 0x44, 0))
        self.assertEqual(out[0][3], (4 << 5) | (4 << 2) | (4 >> 1))  # 128 >> 5 = 4


# ---------------------------------------------------------------------------
# Build and refusal on a synthetic disc.
# ---------------------------------------------------------------------------

class TestBuildOnSyntheticDisc(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.iso = os.path.join(self.tmp.name, "synth.iso")
        synth_iso(self.iso)
        # Data file for the synthetic textures, derived the same way as the real one.
        disc = S.Disc(self.iso)
        try:
            textures = {}
            rnd = random.Random(11)
            for key in S.SOURCES:
                v = S.load_vanilla(disc, key)
                _, raster = S.keyed_raster(v)
                target = [(p[0], p[1] ^ 16, rnd.randrange(256), p[3]) if rnd.random() < 0.4 else p for p in raster]
                transparent = (0x44, 0x77, 0x44) if key == "icon" else None
                textures[key] = S.derive_texture(v, target, transparent)
                textures[key]["_target"] = S.finish(target, transparent)
        finally:
            disc.close()
        self.targets = {k: t.pop("_target") for k, t in textures.items()}
        self.data_path = os.path.join(self.tmp.name, "data.json")
        S.write_data({"format": 1, "credit": "test", "textures": textures}, self.data_path)

    def _read(self, path):
        from PIL import Image
        im = Image.open(path).convert("RGBA")
        raw = im.tobytes()
        return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]

    def test_build_writes_exact_pngs_with_expected_names(self):
        out = os.path.join(self.tmp.name, "out")
        written, refused = S.build(self.iso, out, self.data_path)
        self.assertEqual(refused, {})
        data = S.load_data(self.data_path)
        for key in S.SOURCES:
            self.assertEqual(written[key], data["textures"][key]["name"])
            self.assertEqual(self._read(os.path.join(out, written[key])), self.targets[key])

    def test_legacy_transparent_flag(self):
        out = os.path.join(self.tmp.name, "legacy")
        written, _ = S.build(self.iso, out, self.data_path, legacy_transparent=True)
        px = self._read(os.path.join(out, written["icon"]))
        self.assertTrue(all(p[:3] == (0, 0, 0) for p in px if p[3] == 0))
        self.assertTrue(any(p[3] == 0 for p in px))

    def test_modded_texture_is_refused_per_texture(self):
        modded = os.path.join(self.tmp.name, "modded.iso")
        synth_iso(modded, tweak_sail=True)
        out = os.path.join(self.tmp.name, "out2")
        written, refused = S.build(modded, out, self.data_path)
        self.assertEqual(set(refused), {"sail"})
        self.assertIn("not the vanilla texture", refused["sail"])
        self.assertEqual(set(written), {"icon", "itemget"})
        self.assertEqual(sorted(os.listdir(out)), sorted(written.values()))

    def test_cli_exit_codes(self):
        modded = os.path.join(self.tmp.name, "modded.iso")
        synth_iso(modded, tweak_sail=True)
        d = ["--data", self.data_path]
        self.assertEqual(S.main([self.iso, os.path.join(self.tmp.name, "a")] + d), 0)
        self.assertEqual(S.main([modded, os.path.join(self.tmp.name, "b")] + d), 3)
        self.assertEqual(S.main([os.path.join(self.tmp.name, "missing.iso"), os.path.join(self.tmp.name, "c")] + d), 1)
        bad = os.path.join(self.tmp.name, "bad.iso")
        _write(bad, bytes(0x1000))
        self.assertEqual(S.main([bad, os.path.join(self.tmp.name, "d")] + d), 2)

    def test_wrong_disc_is_an_error(self):
        bad = os.path.join(self.tmp.name, "bad.iso")
        _write(bad, bytes(0x1000))
        with self.assertRaises(S.SailError):
            S.build(bad, os.path.join(self.tmp.name, "o"), self.data_path)
        other = os.path.join(self.tmp.name, "pal.iso")
        _write(other, make_gcm({"x": b"1"}, game_id=b"GZLP01"))
        with self.assertRaises(S.SailError):
            S.build(other, os.path.join(self.tmp.name, "o"), self.data_path)


# ---------------------------------------------------------------------------
# Integration against the real disc and wwrando's assets (skipped when absent).
# ---------------------------------------------------------------------------

@unittest.skipUnless(os.path.isfile(ISO), "GZLE01 disc not available")
class TestRealDisc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.disc = S.Disc(ISO)
        cls.vanilla = {k: S.load_vanilla(cls.disc, k) for k in S.SOURCES}

    @classmethod
    def tearDownClass(cls):
        cls.disc.close()

    def test_vanilla_names_match_the_research_doc(self):
        for key, bti in self.vanilla.items():
            self.assertEqual(bti.filename(), DOC_NAMES[key])

    def test_vanilla_formats(self):
        v = self.vanilla
        self.assertEqual((v["sail"].format, v["sail"].palette_format, v["sail"].num_colors, v["sail"].width), (9, 1, 96, 128))
        self.assertEqual((v["icon"].format, v["icon"].palette_format, v["icon"].num_colors, v["icon"].width), (9, 2, 128, 48))
        self.assertEqual((v["itemget"].format, v["itemget"].width, v["itemget"].height), (14, 128, 128))

    def test_the_two_spare_icons_are_identical(self):
        files = S.open_archive(self.disc, "res/Msg/itemicon.arc")
        self.assertEqual(S.Bti(files["timg/sail_01.bti"]).filename(), DOC_NAMES["icon"])
        self.assertEqual(S.Bti(files["timg/sail_02.bti"]).filename(), DOC_NAMES["icon"])

    def test_modded_asset_is_refused(self):
        data = S.load_data()
        data["textures"]["sail"]["name"] = data["textures"]["sail"]["name"].replace("3f34", "0000")
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "d.json")
            _write(p, json.dumps(data).encode())
            written, refused = S.build(ISO, os.path.join(t, "o"), p)
        self.assertEqual(set(refused), {"sail"})
        self.assertEqual(set(written), {"icon", "itemget"})


@unittest.skipUnless(os.path.isfile(ISO) and os.path.isdir(WWRANDO_ASSETS), "disc or wwrando assets not available")
class TestEqualsWwrando(unittest.TestCase):
    """The produced images equal what wwrando's patch leaves in the game.

    wwrando's result is decode(encode(its PNG)) in the vanilla texture's format.
    The wwrando PNGs are read here only, from your wwrando checkout (BOD_WWRANDO).
    """

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = os.path.join(cls.tmp.name, "out")
        cls.written, cls.refused = S.build(ISO, cls.out)
        cls.legacy_out = os.path.join(cls.tmp.name, "legacy")
        S.build(ISO, cls.legacy_out, legacy_transparent=True)
        cls.disc = S.Disc(ISO)

    @classmethod
    def tearDownClass(cls):
        cls.disc.close()
        cls.tmp.cleanup()

    def _png(self, folder, name):
        from PIL import Image
        im = Image.open(os.path.join(folder, name)).convert("RGBA")
        raw = im.tobytes()
        return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]

    def _expected(self, key, transparent):
        vanilla = S.load_vanilla(self.disc, key)
        rgba, w, h = S.read_png_rgba(os.path.join(WWRANDO_ASSETS, S.WWRANDO_PNG[key]))
        return S.wwrando_result(rgba, w, h, vanilla, transparent)

    def test_nothing_refused_and_names(self):
        self.assertEqual(self.refused, {})
        self.assertEqual(self.written, DOC_NAMES)

    def test_sail_is_pixel_exact(self):
        self.assertEqual(self._png(self.out, DOC_NAMES["sail"]), self._expected("sail", None))

    def test_itemget_is_pixel_exact(self):
        self.assertEqual(self._png(self.out, DOC_NAMES["itemget"]), self._expected("itemget", None))

    def test_icon_is_pixel_exact(self):
        self.assertEqual(self._png(self.out, DOC_NAMES["icon"]), self._expected("icon", S.TRANSPARENT_CURRENT))

    def test_icon_legacy_is_pixel_exact(self):
        self.assertEqual(self._png(self.legacy_out, DOC_NAMES["icon"]), self._expected("icon", S.TRANSPARENT_LEGACY))

    def test_outputs_differ_from_vanilla(self):
        for key in S.SOURCES:
            v = S.load_vanilla(self.disc, key)
            _, raster = S.keyed_raster(v)
            changed = sum(a != b for a, b in zip(raster, self._png(self.out, DOC_NAMES[key])))
            self.assertGreater(changed, 500)

    def test_icon_transparent_colour_comes_from_imagequant(self):
        try:
            import imagequant  # noqa: F401
        except Exception:
            self.skipTest("imagequant not installed")
        from PIL import Image
        im = Image.open(os.path.join(WWRANDO_ASSETS, S.WWRANDO_PNG["icon"])).convert("RGBA")
        q = imagequant.quantize_pil_image(im, dithering_level=1.0, max_colors=256, min_quality=0, max_quality=100).convert("RGBA")
        raw = q.tobytes()
        px = [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]
        colors = {S.rgb5a3_to_rgba(S.enc_rgb5a3(p))[:3] for p in px if p[3] == 0}
        self.assertEqual(colors, {S.TRANSPARENT_CURRENT})


if __name__ == "__main__":
    unittest.main()
