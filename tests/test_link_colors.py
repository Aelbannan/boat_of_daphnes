"""Tests for scripts/windows/link_colors.py.

Run: python -m unittest tests.test_link_colors   (or: python tests/test_link_colors.py)

The format and key tests use small synthetic textures and need no files. The
disc tests need the real GZLE01 disc (BOD_DISC, scripts/bod_paths.py)
and skip when it is absent. The mask tests also need wwrando's
assets/link_color_masks folder (read at test time only, never copied) and skip
without it. The data they build goes to a temporary folder that is deleted.
"""
import json
import os
import re
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

import link_colors as L  # noqa: E402
import swift_sail as S  # noqa: E402

ISO = local_path("BOD_DISC")
MASKS = L.DEFAULT_MASKS

# Keys of the vanilla textures of the USA disc (GZLE01 rev 0). The texture and palette
# hashes were also computed with the XXH64 C source the renderer is built with
# (xxhash.c, seed 0, over the base level bytes and the referenced palette range),
# not only with the Python XXH64 in swift_sail.py.
KNOWN_KEYS = {
    "linktexS3TC": "tex1_160x96_6ae2a887601ba80b_14",
    "hitomi": "tex1_96x96_e2c7985cc000ca69_013cc228b37afa73_9",
    "linktexbci4": "tex1_160x96_61672031da4fcf8c_e5ab9895573ba2d7_8",
    "handsS3TC": "tex1_8x8_a841976ae1556ad6_14",
}

KEY_RE = re.compile(r"^tex1_\d+x\d+_[0-9a-f]{16}(_[0-9a-f]{16})?_\d+$")

# Default colours of wwrando's assets/link_metadata.txt (the base colour of each region).
BASE_COLORS = {
    ("hero", "Boots"): (0x94, 0x49, 0x08),
    ("hero", "Tunic"): (0x5A, 0xB2, 0x4A),
    ("hero", "Skin"): (0xF7, 0xDB, 0x9C),
    ("casual", "Shirt"): (0x4A, 0x75, 0xAD),
    ("casual", "Pants"): (0xFF, 0xA6, 0x08),
}


def make_tex(fmt, w, h, image, palette=b"", pal_fmt=1) -> L.Tex:
    """A BTI (header, then image, then palette) parsed by link_colors.Tex."""
    hdr = bytearray(32)
    hdr[0] = fmt
    struct.pack_into(">HH", hdr, 2, w, h)
    hdr[9] = pal_fmt
    struct.pack_into(">HI", hdr, 10, len(palette) // 2, 32 + len(image))
    struct.pack_into(">I", hdr, 28, 32)
    return L.Tex(bytes(hdr) + image + palette)


class TestFormats(unittest.TestCase):
    def test_i4_nibbles_high_first(self):
        # One 8x8 tile, texel 0 = 0xF, texel 1 = 0x0, the rest 0x8.
        img = bytes([0xF0] + [0x88] * 31)
        px = L.decode_rgba(make_tex(L.FMT_I4, 8, 8, img))
        self.assertEqual(px[0:4], bytes([255, 255, 255, 255]))
        self.assertEqual(px[4:8], bytes([0, 0, 0, 0]))
        self.assertEqual(px[8:12], bytes([0x88] * 4))

    def test_ia8_and_rgba8(self):
        img = bytes([0x40, 0x80] + [0, 0] * 15)  # alpha first, then intensity
        px = L.decode_rgba(make_tex(L.FMT_IA8, 4, 4, img))
        self.assertEqual(px[0:4], bytes([0x80, 0x80, 0x80, 0x40]))
        # RGBA8: a 4x4 tile holds all the A,R pairs, then all the G,B pairs.
        img = bytes([10, 20] + [0] * 30 + [30, 40] + [0] * 30)
        px = L.decode_rgba(make_tex(L.FMT_RGBA8, 4, 4, img))
        self.assertEqual(px[0:4], bytes([20, 30, 40, 10]))

    def test_rgb565_and_rgb5a3(self):
        img = struct.pack(">H", 0xF800) + bytes(30)
        self.assertEqual(L.decode_rgba(make_tex(L.FMT_RGB565, 4, 4, img))[0:4], bytes([255, 0, 0, 255]))
        img = struct.pack(">H", 0x7F00) + bytes(30)  # alpha 3 bits = 7, r = 0xF
        self.assertEqual(L.decode_rgba(make_tex(L.FMT_RGB5A3, 4, 4, img))[0:4], bytes([255, 0, 0, 255]))

    def test_c4_decode_and_edge_clipping(self):
        # 12x8 C4 texture: two 8x8 tiles, the second one is cut at x = 12.
        pal = struct.pack(">16H", *([0xF800] + [0x07E0] + [0x001F] * 14))
        tile0 = bytes([0x01] * 32)   # texels alternate palette 0, 1
        tile1 = bytes([0x11] * 32)   # all palette 1
        px = L.decode_rgba(make_tex(L.FMT_C4, 12, 8, tile0 + tile1, pal))
        self.assertEqual(len(px), 12 * 8 * 4)
        self.assertEqual(px[0:4], bytes([255, 0, 0, 255]))          # index 0 = red
        self.assertEqual(px[4:8], bytes([0, 255, 0, 255]))          # index 1 = green
        self.assertEqual(px[8 * 4:8 * 4 + 4], bytes([0, 255, 0, 255]))  # first texel of tile 1

    def test_c4_key_uses_only_referenced_palette_range(self):
        pal = bytes(range(32))
        img = bytes([0x23] * 32)  # indices 2 and 3 only
        t = make_tex(L.FMT_C4, 8, 8, img, pal)
        self.assertEqual(t.used_indices(), (2, 3))
        want = "tex1_8x8_%s_%s_8" % (S.xxh64_hex(img), S.xxh64_hex(pal[4:8]))
        self.assertEqual(t.key(), want)

    def test_c8_key_and_nonpalette_key(self):
        pal = bytes(range(64))
        img = bytes([5, 9] * 16)
        t = make_tex(L.FMT_C8, 8, 4, img, pal)
        self.assertEqual(t.key(), "tex1_8x4_%s_%s_9" % (S.xxh64_hex(img), S.xxh64_hex(pal[10:20])))
        cmpr = bytes(32)
        self.assertEqual(make_tex(L.FMT_CMPR, 8, 8, cmpr).key(), "tex1_8x8_%s_14" % S.xxh64_hex(cmpr))

    def test_palette_too_short_is_an_error(self):
        with self.assertRaises(S.SailError):
            make_tex(L.FMT_C4, 8, 8, bytes([0xF0] * 32), bytes(8)).key()

    def test_unsupported_format_is_refused(self):
        hdr = bytearray(32)
        hdr[0] = 7
        with self.assertRaises(S.SailError):
            L.Tex(bytes(hdr))


@unittest.skipUnless(os.path.isfile(ISO), "GZLE01 disc not available")
class TestBuiltFromDisc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.warnings = []
        masks = MASKS if os.path.isdir(MASKS) else None
        cls.have_masks = masks is not None
        cls.built = L.build(ISO, cls.tmp.name, masks, warn=cls.warnings.append)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def read(self, rel):
        with open(os.path.join(self.tmp.name, rel), "rb") as f:
            data = f.read()
        w, h = struct.unpack_from("<II", data)
        return w, h, data[8:]

    def test_index_loads_and_lists_every_texture(self):
        with open(os.path.join(self.tmp.name, "index.json"), encoding="utf-8") as f:
            index = json.load(f)
        self.assertEqual(index["version"], 1)
        names = [t["name"] for t in index["textures"]]
        want = ["linktexS3TC", "hitomi"] + ["mouthS3TC.%d" % n for n in range(1, 10)] + \
               ["handsS3TC", "katsuraS3TC", "linktexbci4"]
        self.assertEqual(names, want)
        self.assertEqual(len({t["key"] for t in index["textures"]}), len(want))
        self.assertEqual(index, self.built)

    def test_textures_decode_to_the_right_size_and_keys_have_the_right_shape(self):
        for t in self.built["textures"]:
            self.assertRegex(t["key"], KEY_RE)
            self.assertTrue(t["key"].startswith("tex1_%dx%d_" % (t["width"], t["height"])))
            w, h, px = self.read(t["file"])
            self.assertEqual((w, h), (t["width"], t["height"]))
            self.assertEqual(len(px), w * h * 4)
            fmt = int(t["key"].rsplit("_", 1)[1])
            # palette formats carry a second hash, the others do not
            self.assertEqual(t["key"].count("_"), 4 if fmt in L.PALETTE_FORMATS else 3, t["key"])

    def test_known_keys(self):
        keys = {t["name"]: t["key"] for t in self.built["textures"]}
        for name, key in KNOWN_KEYS.items():
            self.assertEqual(keys[name], key)

    def test_sizes_and_formats(self):
        by = {t["name"]: t for t in self.built["textures"]}
        self.assertEqual((by["linktexS3TC"]["width"], by["linktexS3TC"]["height"]), (160, 96))
        self.assertEqual((by["linktexbci4"]["width"], by["linktexbci4"]["height"]), (160, 96))
        self.assertEqual((by["hitomi"]["width"], by["hitomi"]["height"]), (96, 96))
        self.assertEqual((by["mouthS3TC.5"]["width"], by["mouthS3TC.5"]["height"]), (192, 96))
        self.assertTrue(by["linktexbci4"]["key"].endswith("_8"))   # C4
        self.assertTrue(by["hitomi"]["key"].endswith("_9"))        # C8
        self.assertTrue(by["linktexS3TC"]["key"].endswith("_14"))  # CMPR

    def test_masks_match_their_texture_size(self):
        if not self.have_masks:
            self.skipTest("wwrando link_color_masks folder not available")
        sizes = {t["name"]: (t["width"], t["height"]) for t in self.built["textures"]}
        self.assertEqual(self.warnings, [])
        self.assertGreater(len(self.built["masks"]), 0)
        for m in self.built["masks"]:
            w, h, data = self.read(m["file"])
            self.assertEqual((w, h), sizes[m["texture"]], m["file"])
            self.assertEqual(len(data), w * h)
            self.assertLessEqual(set(data), {0, 1})
            self.assertIn(m["outfit"], ("hero", "casual"))

    def test_mask_mapping(self):
        if not self.have_masks:
            self.skipTest("wwrando link_color_masks folder not available")
        got = {(m["outfit"], m["color"]): m["texture"] for m in self.built["masks"]
               if not m["texture"].startswith("mouth") and m["texture"] != "handsS3TC"}
        self.assertEqual(got[("hero", "Tunic")], "linktexS3TC")
        self.assertEqual(got[("casual", "Shirt")], "linktexbci4")
        self.assertEqual(got[("hero", "Eyes")], "hitomi")
        self.assertEqual(got[("casual", "Sclera")], "hitomi")
        self.assertEqual(got[("hero", "Skin")], "linktexS3TC")
        outfit_mouths = {(m["outfit"], m["texture"]) for m in self.built["masks"] if m["texture"].startswith("mouth")}
        for outfit in ("hero", "casual"):
            for n in range(1, 10):
                self.assertIn((outfit, "mouthS3TC.%d" % n), outfit_mouths)
        hands = {(m["outfit"], m["color"]) for m in self.built["masks"] if m["texture"] == "handsS3TC"}
        self.assertEqual(hands, {("hero", "Skin"), ("casual", "Skin")})
        self.assertIn(("casual", "Hair"), {(m["outfit"], m["color"]) for m in self.built["masks"]
                                           if m["texture"] == "katsuraS3TC"})

    def test_masked_pixels_have_the_documented_base_colour(self):
        """The vanilla pixels under a mask average to wwrando's base colour for that region.
        This ties the decode, the mask loader and the mapping together."""
        if not self.have_masks:
            self.skipTest("wwrando link_color_masks folder not available")
        textures = {t["name"]: self.read(t["file"]) for t in self.built["textures"]}
        checked = 0
        for m in self.built["masks"]:
            want = BASE_COLORS.get((m["outfit"], m["color"]))
            if want is None or m["texture"] not in ("linktexS3TC", "linktexbci4"):
                continue
            w, h, px = textures[m["texture"]]
            _, _, mask = self.read(m["file"])
            idx = [i for i in range(w * h) if mask[i]]
            self.assertTrue(idx, m["color"])
            avg = [sum(px[i * 4 + c] for i in idx) / len(idx) for c in range(3)]
            for a, b in zip(avg, want):
                self.assertLess(abs(a - b), 16, "%s %s: %s vs %s" % (m["outfit"], m["color"], avg, want))
            checked += 1
        self.assertEqual(checked, len(BASE_COLORS))


if __name__ == "__main__":
    unittest.main()
