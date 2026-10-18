"""Three recolourable materials for the Bunny Hood: fur, inner_ear, band.

Textures are greyscale (GX I8) cut from the prop's own colours; the colour comes from TEV colour registers the
Cosmetics Editor can overwrite at run time.  Logical C0 (body / band colour) is J3DMaterial tev colour index 1 =
GX_TEVREG1 and logical C1 (inner ear) is index 2 = GX_TEVREG2; in the MDL3 display lists these are BP registers
0xE4/0xE5 and 0xE6/0xE7.  NOT index 0 / GX_TEVREG0: the game's own lighting (dScnKy_env_light_c::setLightTevColorType)
rewrites every material's tev colour 0 with the scene's light colour on each draw, which turned the hood plain white
(11d).  Below, "C0" and "C1" mean the logical colours.

  fur       tex(I8 grey, mean 0.5) * C0 * 2, then * vertex shade        C0 = body colour (literal)
  band      tex(I8 grey, 0.5)      * C0 * 2, then * vertex shade        C0 = band colour (literal)
  inner_ear lerp(C0, C1, tex(I8 mask, 1 = inner)), then * vertex shade  C0 = body colour, C1 = inner-ear colour

So the colour REPLACES the prop's colour (the texture only adds detail: the eye dots on the fur), C0 of fur and of
inner_ear are both the body colour, and the editor writes them together.
"""
from __future__ import annotations
import copy
import io
import struct
import numpy as np
from PIL import Image

MATERIALS = ["fur", "inner_ear", "band", "eye"]  # material index = shape order in the model


def _tiles(arr, bw, bh):
    h, w = arr.shape[:2]
    out = bytearray()
    for ty in range(0, h, bh):
        for tx in range(0, w, bw):
            out += arr[ty:ty + bh, tx:tx + bw].tobytes()
    return bytes(out)


def encode_i8(grey: np.ndarray) -> bytes:
    assert grey.shape[0] % 4 == 0 and grey.shape[1] % 8 == 0
    return _tiles(grey.astype(np.uint8), 8, 4)


def bti_header(fmt, w, h, image_off, nearest=False):
    # format, alpha setting, w, h, wrap s/t (clamp), no palette (x4), mip off, min/mag filter linear, 1 mip
    return struct.pack(">BBHHBBBBHIBBBBBBbbBBhI", fmt, 0, w, h, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 if nearest else 1, 0 if nearest else 1, 0, 0, 1, 0, 0,
                       image_off)


def build_tex1(images):
    """images: list of (name, fmt, w, h, bytes) -> TEX1 chunk bytes."""
    n = len(images)
    hdrs = bytearray()
    data = bytearray()
    base = 0x20 + 0x20 * n
    offs = []
    for name, fmt, w, h, raw in images:
        offs.append(base + len(data))
        data += raw
        while len(data) % 32:
            data.append(0)
    for i, (name, fmt, w, h, raw) in enumerate(images):
        hdrs += bti_header(fmt, w, h, offs[i] - (0x20 + 0x20 * i), nearest=name == "hood_eye")
    from bmd_write import name_table, chunk
    names = name_table([x[0] for x in images])
    name_off = base + len(data)
    body = struct.pack(">HHII", n, 0xFFFF, 0x20 - 8 + 8, name_off) + b""
    out = bytearray(b"TEX1" + b"\0\0\0\0" + body)
    out += b"\0" * (0x20 - len(out))
    out += hdrs + data + names
    while len(out) % 32:
        out.append(0)
    out[4:8] = struct.pack(">I", len(out))
    return bytes(out)


def eye_texture(prm):
    """The eye decal: a clean black oval (the geometry is the oval) with one hard-edged white highlight, 32x32, no blur."""
    E = prm["dome"]["eye"]
    n = 32
    yy, xx = np.mgrid[0:n, 0:n]
    u, v = (xx + 0.5) / n, (yy + 0.5) / n
    img = np.zeros((n, n))
    hx, hy, hr = E["highlight"]
    img[(u - hx) ** 2 + (v - hy) ** 2 < hr ** 2] = 255.0
    return img


def regions(quarter: Image.Image, prm: dict):
    """From the prop's quarter (64x64 RGBA): the three textures' source data and the default colours."""
    q = np.asarray(quarter.convert("RGB"), np.float32)
    F = prm["dome"]["face_uv"]
    cx, cy = F["cream_texel"]
    fur_mean = q[int(cy) - 4:int(cy) + 4, int(cx) - 4:int(cx) + 4].reshape(-1, 3).mean(0)
    lum = lambda a: a @ np.array([0.299, 0.587, 0.114], np.float32)
    ref = max(lum(fur_mean), 1.0)
    grey = np.clip(lum(q) / (2 * ref) * 255.0, 0, 255)
    # inner-ear mask: distance from the ear's cream towards its purple, in the quarter's top area
    inner = prm["inner_ear"]
    cream = q[inner["cream_texel"][1], inner["cream_texel"][0]]
    purple = q[inner["purple_texel"][1], inner["purple_texel"][0]]
    v = purple - cream
    t = np.clip(((q - cream) @ v) / (v @ v), 0, 1)
    t = np.clip((t - inner["lo"]) / (inner["hi"] - inner["lo"]), 0, 1)
    band_c = q[int(prm["band_texel"][1]) - 3:int(prm["band_texel"][1]) + 3,
               int(prm["band_texel"][0]) - 3:int(prm["band_texel"][0]) + 3].reshape(-1, 3).mean(0)
    return dict(fur_grey=grey, ear_mask=t * 255.0, fur=fur_mean, inner=purple, band=band_c)


def material_chunks(prop_bdl: bytes, material_index: int, quarter: Image.Image, prm: dict):
    """-> (mat3, mdl3, tex1 bytes, defaults dict name -> (r, g, b) 0..255)"""
    import gclib.gx_enums as GX
    from gclib.j3d import J3D
    from gclib.j3d_chunks.tex1 import TEX1
    import gclib.fs_helpers as fs

    rg = regions(quarter, prm)
    images = [("hood_fur", 1, 64, 64, encode_i8(rg["fur_grey"])),
              ("hood_inner_ear", 1, 64, 64, encode_i8(rg["ear_mask"])),
              ("hood_band", 1, 8, 8, encode_i8(np.full((8, 8), 128.0))),
              ("hood_eye", 1, 32, 32, encode_i8(eye_texture(prm)))]
    tex_bytes = build_tex1(images)
    tex1 = TEX1(io.BytesIO(tex_bytes))
    tex1.read(0)

    j = J3D(io.BytesIO(prop_bdl))
    mat3, mdl3 = j.mat3, j.mdl3
    tmpl = mat3.materials[material_index]
    ind = mat3.indirects[material_index]
    mdl_entry = mdl3.entries[material_index]

    def make(tex_index, name):
        m = tmpl.copy()
        m.textures = list(m.textures)
        m.cull_mode = GX.CullMode.Cull_None
        m.textures[0] = tex_index
        m.num_tev_stages = 2
        orders = list(m.tev_orders)
        orders[0] = type(orders[0])(tex_coord_id=GX.TexCoordID.TEXCOORD0, tex_map_id=GX.TexMapID.TEXMAP0,
                                    channel_id=GX.ColorChannelID.COLOR0A0)
        orders[1] = type(orders[0])(tex_coord_id=GX.TexCoordID.TEXCOORD_NULL, tex_map_id=GX.TexMapID.TEXMAP_NULL,
                                    channel_id=GX.ColorChannelID.COLOR0A0)
        m.tev_orders = orders
        st = list(m.tev_stages)
        S = type(st[0])

        def stage(ca, cb, cc, cd, scale, aa, ab, ac, ad):
            return S(tev_mode=255, color_in_a=ca, color_in_b=cb, color_in_c=cc, color_in_d=cd, color_op=GX.TevOp.ADD,
                     color_bias=GX.TevBias.ZERO, color_scale=scale, color_clamp=True, color_reg_id=GX.Register.PREV,
                     alpha_in_a=aa, alpha_in_b=ab, alpha_in_c=ac, alpha_in_d=ad, alpha_op=GX.TevOp.ADD,
                     alpha_bias=GX.TevBias.ZERO, alpha_scale=GX.TevScale.SCALE_1, alpha_clamp=True,
                     alpha_reg_id=GX.Register.PREV, _padding_1=255)

        CC, CA = GX.CombineColor, GX.CombineAlpha
        if name == "eye":
            m.num_tev_stages = 1
            st0 = stage(CC.ZERO, CC.ZERO, CC.ZERO, CC.TEXC, GX.TevScale.SCALE_1, CA.ZERO, CA.ZERO, CA.ZERO, CA.RASA)
            st[0] = st0
            m.tev_stages = st
            return m
        if name == "inner_ear":
            s0 = stage(CC.C1, CC.C2, CC.TEXC, CC.ZERO, GX.TevScale.SCALE_1, CA.ZERO, CA.ZERO, CA.ZERO, CA.RASA)
        else:
            s0 = stage(CC.ZERO, CC.TEXC, CC.C1, CC.ZERO, GX.TevScale.SCALE_2, CA.ZERO, CA.ZERO, CA.ZERO, CA.RASA)
        s1 = stage(CC.ZERO, CC.CPREV, CC.RASC, CC.ZERO, GX.TevScale.SCALE_1, CA.ZERO, CA.ZERO, CA.ZERO, CA.APREV)
        st[0], st[1] = s0, s1
        m.tev_stages = st
        tc = list(m.tev_colors)
        defaults = {"fur": rg["fur"], "inner_ear": rg["fur"], "band": rg["band"]}[name]

        def colour(src, rgb):
            e = src.copy()
            e.r, e.g, e.b, e.a = int(round(rgb[0])), int(round(rgb[1])), int(round(rgb[2])), 255
            return e

        tc[1] = colour(tc[1], defaults)  # index 1 = GX_TEVREG1 = logical C0 (in the MDL3: BP 0xE4/0xE5)
        tc[2] = colour(tc[2], rg["inner"])  # index 2 = GX_TEVREG2 = logical C1 (BP 0xE6/0xE7)
        m.tev_colors = tc
        return m

    mats = [make(i, n) for i, n in enumerate(MATERIALS)]
    mat3.materials = mats
    mat3.indirects = [ind.copy() for _ in mats]
    mat3.mat_names = list(MATERIALS)
    mat3.material_count = len(mats)
    mdl3.entries = []
    for _ in mats:
        e = mdl_entry.copy()
        e.bp_commands, e.xf_commands = [], []  # a shallow copy would share (and clear) the same lists
        mdl3.entries.append(e)
    mdl3.mat_names = list(MATERIALS)
    j.tex1 = tex1
    mat3.save()
    fs.write_u16(mat3.data, 8, len(mats))
    tex1.save()
    mdl3.generate_from_mat3(mat3, tex1)
    fs.write_u16(mdl3.data, 8, len(mats))
    defaults = {"fur": tuple(int(round(x)) for x in rg["fur"]), "inner_ear": tuple(int(round(x)) for x in rg["inner"]),
                "band": tuple(int(round(x)) for x in rg["band"])}
    return mat3.data.getvalue(), mdl3.data.getvalue(), tex1.data.getvalue(), defaults
