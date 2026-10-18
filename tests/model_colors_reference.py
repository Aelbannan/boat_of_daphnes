"""wwrando's player model recolor in Python: the reference tests/model_colors_test.py compares
windows/src/model_colors.cpp against.

It reads a model folder (Link.arc, metadata.txt, color_masks/) and recolors its textures the way
wwrando's customizer.py change_player_custom_colors does (wwrando, MIT, Copyright (c) 2018
LagoLunatic; the color exchange is texture_utils.color_exchange, read from BetterWW's
wwlib/texture_utils.py:1161-1279, its pure Python path). The RARC reader, the texture decoders and
Pillow come from scripts/windows (swift_sail.py, link_colors.py), not from the C++ code under test.
"""
from __future__ import annotations

import colorsys
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts", "windows"))

import link_colors as L  # noqa: E402
import swift_sail as S  # noqa: E402
from PIL import Image  # noqa: E402

TEXTURES = (
    [("linktexS3TC", "bdl/cl.bdl"), ("hitomi", "bdl/cl.bdl")]
    + [("mouthS3TC.%d" % n, "bdl/cl.bdl") for n in range(1, 10)]
    + [("mayuh.%d" % n, "bdl/cl.bdl") for n in range(1, 7)]
    + [("handsS3TC", "bdl/hands.bdl"), ("katsuraS3TC", "bdl/katsura.bdl")]
)


def pick_color(outfit: int, index: int) -> tuple:
    """The colors the C++ test chooses (same formula as pick_color in model_colors_test.cpp)."""
    v = ((outfit * 131 + index * 977 + 12345) * 0x9E3779B1) & 0xFFFFFF
    return (v >> 16, (v >> 8) & 255, v & 255)


# ---------------------------------------------------------------------------
# metadata.txt, as customizer.py:44-144 reads it (without a YAML library).
# ---------------------------------------------------------------------------

def read_metadata(path: str) -> dict:
    with open(path, encoding="utf-8") as f:
        text = f.read()
    # The old format's bare hex colors get a 0x (customizer.py:62-73).
    old = re.search(r"^ +\S[^:]*: +\"?[0-9A-F]{6}\"?$", text, re.IGNORECASE | re.MULTILINE)
    new = re.search(r"^ +\S[^:]*: +0x[0-9A-F]{6}$", text, re.IGNORECASE | re.MULTILINE)
    if old and not new:
        text = re.sub(r"^( +\S[^:]*: +)\"?([0-9A-F]{6})\"?$", r"\g<1>0x\g<2>", text, flags=re.IGNORECASE | re.MULTILINE)
    text = text.replace("\t", "  ")
    root: dict = {}
    stack = [(-1, root)]
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip(" "))
        key, sep, value = line.strip().partition(":")
        if not sep:
            continue
        key, value = key.strip(), value.strip().strip("\"'")
        while stack[-1][0] >= indent:
            stack.pop()
        parent = stack[-1][1]
        if value == "":
            parent[key] = {}
            stack.append((indent, parent[key]))
        elif re.fullmatch(r"0x[0-9A-Fa-f]{6}", value):
            parent[key] = int(value, 16)
        elif value.lower() in ("true", "false"):
            parent[key] = value.lower() == "true"
        else:
            parent[key] = value
    return root


def rgb(v: int) -> tuple:
    return (v >> 16, (v >> 8) & 255, v & 255)


# ---------------------------------------------------------------------------
# texture_utils.color_exchange, pure Python path.
# ---------------------------------------------------------------------------

def color_exchange(pixels: list, base_color, replacement_color, mask=None, ignore_bright=False) -> list:
    """pixels: [(r, g, b, a)]; mask: [bool] or None. Returns a new list."""
    base_h, base_s, base_v = colorsys.rgb_to_hsv(base_color[0] / 255, base_color[1] / 255, base_color[2] / 255)
    base_h, base_s, base_v = int(base_h * 360), int(base_s * 100), int(base_v * 100)
    rep_h, rep_s, rep_v = colorsys.rgb_to_hsv(replacement_color[0] / 255, replacement_color[1] / 255, replacement_color[2] / 255)
    rep_h, rep_s, rep_v = int(rep_h * 360), int(rep_s * 100), int(rep_v * 100)
    s_change, v_change = rep_s - base_s, rep_v - base_v
    out = list(pixels)
    for i, (r, g, b, a) in enumerate(pixels):
        if mask is not None and not mask[i]:
            continue
        if ignore_bright and r > 128 and g > 128 and b > 128 and a == 0xFF:
            continue
        h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        h, s, v = int(h * 360), int(s * 100), int(v * 100)
        if s == 0:
            s = base_s
        new_h = rep_h % 360
        new_s = max(0, min(100, s + s_change))
        new_v = max(0, min(100, v + v_change))
        r, g, b = colorsys.hsv_to_rgb(new_h / 360, new_s / 100, new_v / 100)
        out[i] = (int(r * 255), int(g * 255), int(b * 255), a)
    return out


def load_mask(path: str, size: tuple) -> list:
    im = Image.open(path).convert("RGBA")
    if im.size != size:
        raise AssertionError("mask %s is %s, texture %s" % (path, im.size, size))
    raw = im.tobytes()
    return [tuple(raw[i * 4:i * 4 + 4]) == (255, 0, 0, 255) for i in range(im.width * im.height)]


def flat(pixels: list) -> bytes:
    return bytes(c for p in pixels for c in p)


# ---------------------------------------------------------------------------
# customizer.py change_player_custom_colors for one outfit.
# ---------------------------------------------------------------------------

class Model:
    def __init__(self, folder: str):
        self.folder = folder
        self.meta = read_metadata(os.path.join(folder, "metadata.txt"))
        with open(os.path.join(folder, "Link.arc"), "rb") as f:
            self.files = S.rarc_files(f.read())
        self.masks = os.path.join(folder, "color_masks")
        self.tex = {}  # name -> (width, height, [rgba pixels])
        for name, member in TEXTURES:
            if name.startswith("mayuh") and not self.meta.get("has_colored_eyebrows"):
                continue
            t = L.bdl_headers(self.files[member], name)[0]
            self.tex[name] = (t.width, t.height, self._pixels(L.decode_rgba(t)))
        t = L.Tex(self.files["tex/linktexbci4.bti"])
        self.tex["linktexbci4"] = (t.width, t.height, self._pixels(L.decode_rgba(t)))

    @staticmethod
    def _pixels(raw: bytes) -> list:
        return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]

    def mask(self, relative: str, size: tuple):
        path = os.path.join(self.masks, relative)
        return load_mask(path, size) if os.path.isfile(path) else None

    def colors(self, prefix: str) -> list:
        return [(n, rgb(v)) for n, v in self.meta.get(prefix + "_custom_colors", {}).items()]

    def recolor_outfit(self, prefix: str, chosen: dict) -> dict:
        """{texture name: pixels} for what wwrando changes with these colors ({color name: (r, g, b)}).
        The body texture is named linktexS3TC (hero) or linktexbci4 (casual)."""
        m = self.meta
        colors = self.colors(prefix)
        body = "linktexS3TC" if prefix == "hero" else "linktexbci4"
        images = {n: list(t[2]) for n, t in self.tex.items()}
        sizes = {n: (t[0], t[1]) for n, t in self.tex.items()}
        changed = set()
        hands_name = m.get(prefix + "_hands_color_name", "Skin")
        mouth_name = m.get(prefix + "_mouth_color_name", "Skin")
        hitomi_name = m.get(prefix + "_hitomi_color_name", "Eyes")
        eyebrow_name = m.get(prefix + "_eyebrow_color_name", "Hair")
        hair_name = m.get("casual_hair_color_name", "Hair")
        for cname, _base in colors:
            for i in range(1, 10):
                if self.mask("mouths/mouthS3TC.%d_%s.png" % (i, cname), sizes["mouthS3TC.%d" % i]) is not None:
                    mouth_name = None
            if self.mask("hands_%s_%s.png" % (prefix, cname), sizes["handsS3TC"]) is not None:
                hands_name = None
            if self.mask("hitomi_%s_%s.png" % (prefix, cname), sizes["hitomi"]) is not None:
                hitomi_name = None
        for cname, base in colors:
            custom = chosen.get(cname)
            if custom is None or custom == base:
                continue
            pupils = False
            hmask = self.mask("hitomi_%s_%s.png" % (prefix, cname), sizes["hitomi"])
            if hmask is not None or cname == hitomi_name:
                pupils = True
                if hmask is not None:
                    images["hitomi"] = color_exchange(images["hitomi"], base, custom, hmask)
                else:
                    images["hitomi"] = color_exchange(images["hitomi"], base, custom, None, ignore_bright=True)
                changed.add("hitomi")
            bmask = self.mask("%s_%s.png" % (prefix, cname), sizes[body])
            if bmask is None and pupils:
                pass
            elif bmask is None:
                raise AssertionError("color %s has no mask" % cname)
            else:
                images[body] = color_exchange(images[body], base, custom, bmask)
                changed.add(body)
            if m.get("has_colored_eyebrows") and cname == eyebrow_name:
                for i in range(1, 7):
                    images["mayuh.%d" % i] = color_exchange(images["mayuh.%d" % i], base, custom)
                    changed.add("mayuh.%d" % i)
            if prefix == "casual" and cname == hair_name:
                w, h, _ = self.tex["katsuraS3TC"]
                im = Image.frombytes("RGBA", (w, h), flat(images["katsuraS3TC"]))
                im.paste(custom, [0, 0, 8, 8])
                images["katsuraS3TC"] = self._pixels(im.tobytes())
                changed.add("katsuraS3TC")
            for i in range(1, 10):
                name = "mouthS3TC.%d" % i
                mm = self.mask("mouths/mouthS3TC.%d_%s.png" % (i, cname), sizes[name])
                if mm is not None:
                    images[name] = color_exchange(images[name], base, custom, mm)
                    changed.add(name)
                elif cname == mouth_name:
                    images[name] = color_exchange(images[name], base, custom)
                    changed.add(name)
            hm = self.mask("hands_%s_%s.png" % (prefix, cname), sizes["handsS3TC"])
            if hm is not None:
                images["handsS3TC"] = color_exchange(images["handsS3TC"], base, custom, hm)
                changed.add("handsS3TC")
            elif cname == hands_name:
                images["handsS3TC"] = color_exchange(images["handsS3TC"], base, custom)
                changed.add("handsS3TC")
        return {n: images[n] for n in changed}

    def scenario(self, name: str) -> dict:
        """What model_colors_test recolor writes: {texture name: (width, height, RGBA bytes)}.
        Scenario "both" changes every color of both outfits, "casual" only the Outset ones."""
        picks = {}
        for o, prefix in enumerate(("hero", "casual")):
            if o == 0 and name != "both":
                continue
            picks[prefix] = {cname: pick_color(o, i) for i, (cname, _b) in enumerate(self.colors(prefix))}
        out = {}
        hero = self.recolor_outfit("hero", picks.get("hero", {}))
        casual = self.recolor_outfit("casual", picks.get("casual", {}))
        # Hero's body; the Outset body and back hair; everything shared from the Hero run unless only
        # Outset colors changed.
        shared = casual if (picks.get("casual") and not picks.get("hero")) else hero
        for n, px in hero.items():
            if n == "linktexS3TC":
                out[n] = px
        for n, px in casual.items():
            if n in ("linktexbci4", "katsuraS3TC"):
                out[n] = px
        for n, px in shared.items():
            if n not in ("linktexS3TC", "linktexbci4", "katsuraS3TC"):
                out[n] = px
        return {n: (self.tex[n][0], self.tex[n][1], flat(px)) for n, px in out.items()}


def read_dump(path: str) -> dict:
    """The file model_colors_test recolor wrote: {texture name: (width, height, RGBA bytes)}."""
    import struct
    with open(path, "rb") as f:
        data = f.read()
    (n,) = struct.unpack_from("<I", data, 0)
    at = 4
    out = {}
    for _ in range(n):
        (ln,) = struct.unpack_from("<I", data, at)
        name = data[at + 4:at + 4 + ln].decode()
        at += 4 + ln
        w, h = struct.unpack_from("<II", data, at)
        at += 8
        out[name] = (w, h, data[at:at + w * h * 4])
        at += w * h * 4
    return out
