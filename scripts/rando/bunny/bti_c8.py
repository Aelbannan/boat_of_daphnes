"""Encode a 48x48 RGBA image as a GX C8 BTI with an RGB5A3 palette, laid out like WW's item icons
(header copied from the vanilla icon layout: image at 0x20, palette after it)."""
from __future__ import annotations
import struct
import numpy as np
from PIL import Image


def rgb5a3(r, g, b, a):
    if a >= 0xE0:
        return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)


def quantize(img: Image.Image, colors: int = 240):
    """Index 0 = fully transparent; the rest from an adaptive palette (alpha is 0 or 255)."""
    rgba = np.asarray(img.convert("RGBA"), np.uint8).copy()
    opaque = rgba[:, :, 3] >= 128
    rgb = Image.fromarray(rgba[:, :, :3], "RGB")
    q = rgb.quantize(colors=colors - 1, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.asarray(q.getpalette(), np.uint8).reshape(-1, 3)[:colors - 1]
    idx = np.asarray(q, np.uint8).astype(int) + 1
    idx[~opaque] = 0
    palette = [(0, 0, 0, 0)] + [(int(p[0]), int(p[1]), int(p[2]), 255) for p in pal]
    return idx, palette


def encode(img: Image.Image) -> bytes:
    assert img.size == (48, 48)
    idx, palette = quantize(img)
    palette = palette + [(0, 0, 0, 0)] * (240 - len(palette))  # always 240 entries: the game's GX objects keep the vanilla count
    out = bytearray(0x20)
    tiles = bytearray()
    for ty in range(0, 48, 4):
        for tx in range(0, 48, 8):
            tiles += bytes(idx[ty:ty + 4, tx:tx + 8].reshape(-1).astype(np.uint8))
    pal_off = 0x20 + len(tiles)
    out[0:0x20] = bytes.fromhex("09 02 00 30 00 30 00 00 01 02 00 00 00 00 00 00 00 00 00 00 01 01 00 00 01 00 00 00 00 00 00 20")
    struct.pack_into(">HI", out, 0x0A, len(palette), pal_off)
    out += tiles
    for r, g, b, a in palette:
        out += struct.pack(">H", rgb5a3(r, g, b, a))
    return bytes(out)
