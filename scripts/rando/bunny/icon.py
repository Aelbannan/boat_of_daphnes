"""The Bunny Hood's item icon: the hood model drawn like WW's own 48x48 item icons (3/4 view from the front left,
three cel-shaded tones, a dark outline), then encoded as PNG and as a C8 BTI."""
from __future__ import annotations
import numpy as np
from PIL import Image, ImageFilter
import softrender as SR
import refmodel as R


def _project_setup(meshes, P):
    d = np.array(P["view"], float)
    d /= np.linalg.norm(d)
    allp = np.concatenate([me["pos"].reshape(-1, 3) for me in meshes])
    c = (allp.min(0) + allp.max(0)) / 2
    V = SR.look_at(c + d * 300, c, up=tuple(P["up"]))
    pv = (V[:3, :3] @ allp.T).T
    ext = max(np.ptp(pv[:, 0]), np.ptp(pv[:, 1]))
    ctr_xy = (pv[:, :2].min(0) + pv[:, :2].max(0)) / 2
    V2 = V.copy()
    V2[0, 3] = -ctr_xy[0]
    V2[1, 3] = -ctr_xy[1]
    V2[2, 3] = -pv[:, 2].max() - 10.0
    return V2, P["size"] * P["fill"] / ext


def render_icon(bdl: bytes, prm: dict) -> Image.Image:
    """MM-style gold ramp: the hood's own albedo graded by a 4 step cel ramp (cool purple-brown shadow, mid, lit, a pale
    highlight band), the eyes and ear colours kept saturated, a thin dark outline, a one pixel drop shadow."""
    m, j, meshes = R.load_meshes(bdl)
    P = prm["icon"]
    V2, scale = _project_setup(meshes, P)
    size, ss = P["size"], P["supersample"]
    L = np.array(P["light"], float)
    # pass 1: albedo (unlit); pass 2: the cel tone map (white material, toon-quantised light)
    ra = SR.Renderer(size, size, bg=(0, 0, 0, 0), ss=ss)
    ra.color[:] = 0
    rt = SR.Renderer(size, size, bg=(0, 0, 0, 0), ss=ss)
    rt.color[:] = 0
    for me in meshes:
        me = dict(me, vcolor=False)
        ra.draw(me, V2, scale, unlit=True)
        rt.draw(me, V2, scale, light=L, ambient=0.0, toon=(P["tones"], P["thresholds"]), unlit=False, flat_color=(1, 1, 1, 1))
    ia, it = np.asarray(ra.image(), np.float32), np.asarray(rt.image(), np.float32)
    alpha = it[:, :, 3] / 255.0
    mask = alpha > 0.5
    alb = np.clip(ia[:, :, :3] / np.maximum(ia[:, :, 3:4] / 255.0, 1e-3), 0, 255) / 255.0
    tone = np.clip(it[:, :, 0] / np.maximum(it[:, :, 3] / 255.0, 1e-3), 0, 400) / 255.0  # the toon level of each pixel
    # saturation boost on the albedo (MM's icon is rich gold, not khaki)
    grey = alb @ np.array([0.299, 0.587, 0.114], np.float32)
    alb = np.clip(grey[:, :, None] + (alb - grey[:, :, None]) * P["saturation"], 0, 1)
    # ramp: colour = albedo * shadow tint at the low tone, * 1 at mid, towards a pale highlight at the top
    sh_tint = np.array(P["shadow_tint"], np.float32)
    hi_col = np.array(P["highlight_rgb"], np.float32) / 255.0
    lv = np.array(P["tones"], np.float32)
    lo_t = np.clip((lv[1] - tone) / max(lv[1] - lv[0], 1e-3), 0, 1)[:, :, None]
    hi_t = np.clip((tone - lv[2]) / max(lv[3] - lv[2], 1e-3), 0, 1)[:, :, None]
    col = alb * ((1 - lo_t) + lo_t * sh_tint)
    col = col * (1 - hi_t) + (alb * 0.45 + hi_col * 0.55) * hi_t
    # eyes/darks stay dark: pixels whose albedo is nearly black are not lifted by the highlight
    dark = (grey < 0.18)[:, :, None]
    col = np.where(dark, alb * (0.9 + 0.1 * tone[:, :, None]), col)
    out = np.zeros((size, size, 4), np.uint8)
    out[mask, :3] = (np.clip(col[mask], 0, 1) * 255).astype(np.uint8)
    out[mask, 3] = 255
    m_img = Image.fromarray((mask * 255).astype(np.uint8))
    # thin outline: one pixel ring, a dark brown, ONLY where the neighbouring inside pixel is not already dark
    dil = np.asarray(m_img.filter(ImageFilter.MaxFilter(3))) > 0
    ring = dil & ~mask
    ol = np.array(P["outline"], np.uint8)
    out[ring, :3] = ol
    out[ring, 3] = 255
    # soft drop shadow like MM: a semi-dark pixel offset down-right where there is no outline yet
    sh = np.roll(np.roll(mask, P["shadow_offset"][0], axis=0), P["shadow_offset"][1], axis=1) & ~mask & ~ring
    out[sh, :3] = np.array(P["shadow_rgb"], np.uint8)
    out[sh, 3] = 255
    return Image.fromarray(out, "RGBA")
