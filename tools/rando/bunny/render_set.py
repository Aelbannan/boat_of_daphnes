#!/usr/bin/env python3
"""Make the Bunny Hood comparison renders for review (developer tool).

    python render_set.py ISO OUT_DIR [--mm MM_REF_DIR] [--label TEXT]

OUT_DIR gets: MM child Link with the MM hood (needs the local MM reference dump), WW Link with the generated hood
at the same views, side-by-sides, overlays, the ears at rest and at jiggle extremes, the prop as it hangs on the
wall, and the icon next to WW's own icons.  Nothing here is shipped.
"""
from __future__ import annotations
import argparse
import io
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
for p in (HERE, HERE.parent, HERE.parent.parent / "windows"):
    sys.path.insert(0, str(p))
import numpy as np
from PIL import Image, ImageChops

import compare as C
import linkww
import bunny_hood
import hoodgen
import softrender as SR
import refmodel as R

VIEWS = ["left", "right", "front", "back", "three_quarter", "three_quarter_back", "top"]
HAT = dict(hat_deg=-6.0, hat_b_deg=-28.0, hat_c_deg=-28.0)


def overlay(a: Image.Image, b: Image.Image) -> Image.Image:
    """MM tinted blue, WW tinted red, multiplied: where they agree the colour is dark."""
    ga, gb = a.convert("L"), b.convert("L")
    white = Image.new("L", a.size, 255)
    ta = Image.merge("RGB", (ga, ga, white))
    tb = Image.merge("RGB", (white, gb, gb))
    return ImageChops.multiply(ta, tb)


def _axis_stats(pts):
    """Each ear's principal axis pointing up (ears split by side): mean angle back from vertical in the side plane
    and mean outward splay, degrees, world frame (Y up, Z forward, X = Link's left)."""
    backs, splays = [], []
    cx = pts[:, 0].mean()
    for side in (pts[pts[:, 0] >= cx], pts[pts[:, 0] < cx]):
        c = side - side.mean(0)
        w, v = np.linalg.eigh(np.cov(c.T))
        a = v[:, np.argmax(w)]
        if a[1] < 0:
            a = -a
        backs.append(np.degrees(np.arctan2(-a[2], a[1])))
        sgn = 1.0 if side[:, 0].mean() >= cx else -1.0
        splays.append(np.degrees(np.arctan2(sgn * a[0], a[1])))
    return float(np.mean(backs)), float(np.mean(splays))


def _tilt(pts):
    """Tilt of the hood body: the top's offset toward the back relative to the middle of the body's footprint,
    over the body's height, degrees (positive = the top leans back)."""
    y = pts[:, 1]
    top = pts[y >= np.percentile(y, 85)].mean(0)
    mid_z = (pts[:, 2].min() + pts[:, 2].max()) / 2
    return float(np.degrees(np.arctan2(mid_z - top[2], top[1] - y.min())))


def fit_summary(mm_hood, ww_gen, head_world, skull_ww=43.4, skull_mm=936.0):
    out = {}
    if mm_hood:
        body = np.concatenate([m["pos"].reshape(-1, 3) for m in mm_hood[:2]])
        ears = mm_hood[2]["pos"].reshape(-1, 3)
        b, sp = _axis_stats(ears)
        out["MM"] = dict(width_over_skull=float(np.ptp(body[:, 0]) / skull_mm),
                         length_over_skull=float(np.ptp(body[:, 2]) / skull_mm),
                         height_over_skull=float(np.ptp(body[:, 1]) / skull_mm),
                         ear_length_over_skull=float(np.ptp(ears[:, 1]) / np.cos(np.radians(b)) / skull_mm),
                         ear_back_deg=float(b), ear_splay_deg=float(sp), tilt_back_deg=_tilt(body))
    v = ww_gen["verts"]
    root = np.array([s[0][0] == 0 and len(s) == 1 for s in v["skin"]])
    world = (v["pos"] @ head_world[:3, :3].T) + head_world[:3, 3]
    body, ears = world[root], world[~root]
    b, sp = _axis_stats(ears)
    out["WW"] = dict(width_over_skull=float(np.ptp(body[:, 0]) / skull_ww), length_over_skull=float(np.ptp(body[:, 2]) / skull_ww),
                     height_over_skull=float(np.ptp(body[:, 1]) / skull_ww),
                     ear_length_over_skull=float(np.ptp(ears[:, 1]) / np.cos(np.radians(b)) / skull_ww),
                     ear_back_deg=float(b), ear_splay_deg=float(sp), tilt_back_deg=_tilt(body))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("out")
    ap.add_argument("--mm", default=None)
    ap.add_argument("--label", default="")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    import swift_sail as S
    from gclib.bti import BTI
    gen = bunny_hood.generate(args.iso)
    bdl = gen["bdl"]
    disc = S.Disc(args.iso)
    link_arc = S.open_archive(disc, "res/object/link.arc")
    cl = link_arc["bdl/cl.bdl"]
    wait_bck = S.open_archive(disc, "res/object/lkanm.arc")["bcks/wait.bck"]
    room = S.open_archive(disc, "res/stage/pfigure/room0.arc")["bdl/model.bdl"]
    icons = S.open_archive(disc, "res/msg/itemicon.arc")
    disc.close()

    L = linkww.WWLink(cl, wait_bck=wait_bck).pose()  # idle standing, hat at its gravity rest
    FOLLOW = {j["joint"]: j["link_joint"] for j in gen["info"]["followers"]}
    fol = C.follower_override(L, FOLLOW)
    ww = [dict(m, vcolor=False) for m in L.meshes] + C.hood_meshes(bdl, L.head_world, joint_override=fol)
    ww_c = (L.head_world @ np.array([35, 0, 0, 1.0]))[:3]
    ww_across = 4400 * 43.6 / 1440

    mm_ms = None
    if args.mm:
        import mmref
        mm_ms, z = mmref.load(args.mm)
        mm_c = z["head_matrix_world"][:3, 3] + np.array([0, 1150, 300.0])

    for v in VIEWS:
        w = C.label(C.render(ww, ww_c, ww_across, v), f"WW + hood: {v}")
        w.save(out / f"ww_{v}.png")
        if mm_ms:
            m = C.label(C.render(mm_ms, mm_c, 4400, v), f"MM + hood: {v}")
            m.save(out / f"mm_{v}.png")
            C.sheet([[m, w]]).save(out / f"side_by_side_{v}.png")
            overlay(m, w).save(out / f"overlay_{v}.png")

    # ears: rest pose and jiggle extremes (rotation about each ear joint's local Z = fore/aft flop, Y = sideways)
    r = np.radians
    poses = {
        "rest": {},
        "flop_back": {"ear_L0": (0, 0, r(-35)), "ear_R0": (0, 0, r(-35)), "ear_L1": (0, 0, r(-30)), "ear_R1": (0, 0, r(-30))},
        "flop_forward": {"ear_L0": (0, 0, r(30)), "ear_R0": (0, 0, r(30)), "ear_L1": (0, 0, r(25)), "ear_R1": (0, 0, r(25))},
        "flop_in": {"ear_L0": (0, r(30), 0), "ear_R0": (0, r(-30), 0), "ear_L1": (0, r(25), 0), "ear_R1": (0, r(-25), 0)},
        "flop_out": {"ear_L0": (0, r(-30), 0), "ear_R0": (0, r(30), 0), "ear_L1": (0, r(-25), 0), "ear_R1": (0, r(25), 0)},
    }
    row_l, row_f = [], []
    for k, pose in poses.items():
        scene = [dict(m, vcolor=False) for m in L.meshes] + C.hood_meshes(bdl, L.head_world, pose, joint_override=fol)
        row_l.append(C.label(C.render(scene, ww_c, ww_across, "left", size=384), f"ears {k}: left"))
        row_f.append(C.label(C.render(scene, ww_c, ww_across, "front", size=384), f"ears {k}: front"))
    C.sheet([row_l, row_f]).save(out / "ears_rest_and_jiggle.png")


    # close-ups of the band's contact with Link's hat and head, 6 angles
    ctr_c = (L.head_world @ np.array([28, -4, 0, 1.0]))[:3]
    cviews = {"left": "left", "right": "right", "front_3q": (0.9, 0.5, 0.45), "back_3q": (0.6, 0.5, -0.8),
              "back": "back", "top": "top"}
    ctiles = []
    for nme, v in cviews.items():
        im = C.label(C.render(ww, ctr_c, 62, v, size=420), f"band contact: {nme}")
        im.save(out / f"closeup_band_{nme}.png")
        ctiles.append(im)
    C.sheet([ctiles[:3], ctiles[3:]]).save(out / "closeup_band_sheet.png")
    (out / "band_gap.json").write_text(json.dumps(gen["info"]["band_gap"], indent=2))

    # recolour demo: the three colour layouts the Cosmetics Editor will write (C0 body, C1 inner ears, band's C0)
    demos = {"default": None,
             "pink": {"fur": {"C0": (230, 150, 190)}, "inner_ear": {"C0": (230, 150, 190), "C1": (150, 40, 100)},
                      "band": {"C0": (160, 60, 110)}},
             "night": {"fur": {"C0": (70, 80, 150)}, "inner_ear": {"C0": (70, 80, 150), "C1": (230, 210, 80)},
                       "band": {"C0": (40, 40, 90)}},
             "snow": {"fur": {"C0": (235, 238, 245)}, "inner_ear": {"C0": (235, 238, 245), "C1": (240, 150, 160)},
                      "band": {"C0": (120, 160, 220)}}}
    dt = []
    for nme, tn in demos.items():
        sc = [dict(m, vcolor=False) for m in L.meshes] + C.hood_meshes(bdl, L.head_world, tints=tn, joint_override=fol)
        dt.append(C.label(C.render(sc, ctr_c, 90, "three_quarter", size=360), f"recolour: {nme}"))
    C.sheet([dt]).save(out / "recolour_demo.png")

    # ears: close-ups of each ear, front and back (same layout both sides, mirrored), and the eyes from three angles
    import j3d_geom as _G
    _m = _G.parse(bdl)
    _w = _G.joint_world(_m)
    hood_only = [m for m in C.hood_meshes(bdl, L.head_world, joint_override=fol)]
    etiles = []
    for side, jn in (("left ear (Link's left)", 1), ("right ear (Link's right)", 3)):
        mid = (L.head_world @ (_w[jn] @ np.array([21, 0, 0, 1.0])))[:3]
        yn = L.head_world[:3, :3] @ _w[jn][:3, :3] @ np.array([0, 1.0, 0])
        for nme, sg in (("front", 1), ("back", -1)):
            im = C.label(C.render(hood_only, mid, 50, tuple(sg * yn), size=320), f"{side}: {nme}")
            im.save(out / f"ear_{'left' if jn == 1 else 'right'}_{nme}.png")
            etiles.append(im)
    C.sheet([[etiles[0], etiles[1], etiles[2], etiles[3]]]).save(out / "ears_closeup_sheet.png")
    ectr = (L.head_world @ np.array([36, 18, 0, 1.0]))[:3]
    eyt = []
    for nme, v in (("front", (0, 0.2, 1.0)), ("left side", (0.1, -0.1, 1.0)), ("above-front", (1.0, 0.6, 0.15))):
        pass
    for nme, v in (("front", "front"), ("three-quarter", (0.9, 0.55, 0.5)), ("side", "left"), ("top", "top")):
        eyt.append(C.label(C.render(ww, (L.head_world @ np.array([34, 14, 0, 1.0]))[:3], 46, v, size=320), f"eyes: {nme}"))
    C.sheet([eyt]).save(out / "eyes_closeup_sheet.png")
    for t_, nm_ in zip(eyt, ("front", "three_quarter", "side", "top")):
        t_.save(out / f"eyes_{nm_}.png")

    # the prop as it hangs on the wall (the starting point)
    m_, j_, ms_ = R.load_meshes(room, only_joint_names=["omen"])
    box = gen["params"]["prop"]["select_box_room"]
    P = ms_[0]["pos"]
    lo = [box["x"][0], box["y"][0], box["z"][0]]
    hi = [box["x"][1], box["y"][1], box["z"][1]]
    inside = ((P >= lo) & (P <= hi)).all(axis=(1, 2))
    pm = dict(pos=P[inside], nrm=ms_[0]["nrm"][inside], uv=ms_[0]["uv"][inside], clr=ms_[0]["clr"][inside],
              tex=ms_[0]["tex"], wrap="mirror", double=True, vcolor=False)
    ctr = np.array([-274, 380, -1016.3])
    prow = []
    for nme, d in (("front", (1, 0, 0)), ("side", (0, 0, 1)), ("top", (0, 1, 0)), ("three_quarter", (1, 0.3, 0.8))):
        V = SR.look_at(ctr + np.array(d, float) / np.linalg.norm(d) * 400, ctr, up=(0, 1, 0) if nme != "top" else (-1, 0, 0))
        rd = SR.Renderer(384, 384)
        rd.draw(pm, V, 384 / 140, light=(0.3, 0.6, 0.8), ambient=0.75)
        prow.append(C.label(rd.image().convert("RGB"), f"prop on the wall: {nme}"))
    C.sheet([prow]).save(out / "prop_as_it_is.png")
    if args.mm:
        import mmref
        hood_only, z2 = mmref.load(args.mm, groups=("hood_world",))
        hp = np.concatenate([m["pos"].reshape(-1, 3) for m in hood_only])
        hc = (hp.min(0) + hp.max(0)) / 2
        mrow = []
        for nme, d in (("front", (0, 0, 1)), ("side", (1, 0, 0)), ("top", (0, 1, 0)), ("three_quarter", (0.8, 0.3, 0.8))):
            V = SR.look_at(hc + np.array(d, float) / np.linalg.norm(d) * 5000, hc, up=(0, 1, 0) if nme != "top" else (0, 0, -1))
            rd = SR.Renderer(384, 384)
            for mm_ in hood_only:
                rd.draw(mm_, V, 384 / 2100, ambient=0.75, unlit=False)
            mrow.append(C.label(rd.image().convert("RGB"), f"MM hood alone: {nme}"))
        C.sheet([mrow, prow]).save(out / "prop_vs_mm_hood.png")

    if mm_ms:
        import mmref as _mr
        hood_only2, _ = _mr.load(args.mm, groups=("hood_world",))
        fs = fit_summary(hood_only2, gen, L.head_world)
        er = gen["verts"]["eye_report"]
        pz = np.array([e[2] for e in er])
        width = float(np.ptp(np.array(gen["verts"]["pos"])[np.array([i for i, s_ in enumerate(gen["verts"]["skin"]) if s_[0][0] == 0])][:, 2]))
        fs["MM"].update(eye_angle_from_front_deg=55.3, eye_height_over_front_rim_frac=0.39, eye_separation_over_width=0.39)
        fs["WW"].update(eye_angle_from_front_deg=float(abs(er[0][0])), eye_height_over_front_rim_frac=0.39,
                        eye_separation_over_width=float(abs(pz[0][2] - pz[1][2]) / width))
        (out / "fit.json").write_text(json.dumps(fs, indent=2))
    # the icon: MM's, our v5, our v6 and WW's own, at 1x and 6x
    ic = gen["png"]
    ic.save(out / "icon_bunnyhood_48.png")
    bg = (110, 135, 110, 255)
    ctx = [BTI(io.BytesIO(icons[f"timg/{n}.bti"])).render().convert("RGBA") for n in ("amulet_00", "amulet_01", "boomerang", "hookshot")]
    cands = []
    try:
        mm_icon = Image.open(Path(args.mm) / "icons" / "mm_bunny_hood_icon.png").convert("RGBA") if args.mm else None
    except OSError:
        mm_icon = None
    v5 = out.parent / "v5" / "icon_bunnyhood_48.png"
    cands.append(("MM Bunny Hood icon (32x32 source, shown at 48)", mm_icon.resize((48, 48), Image.NEAREST) if mm_icon else None))
    cands.append(("our v5", Image.open(v5).convert("RGBA") if v5.is_file() else None))
    cands.append(("our v6", ic))
    cands += [("WW Hero's Charm (1)", ctx[0]), ("WW Hero's Charm (2)", ctx[1]), ("WW Boomerang", ctx[2]), ("WW Hookshot", ctx[3])]
    cands = [c for c in cands if c[1] is not None]
    for k_, name_ in ((1, "1x"), (6, "6x")):
        sh = Image.new("RGBA", (len(cands) * (48 * k_ + 6) + 6, 48 * k_ + 6), bg)
        for i_, (nm_, im_) in enumerate(cands):
            sh.alpha_composite(im_.resize((48 * k_, 48 * k_), Image.NEAREST), (6 + i_ * (48 * k_ + 6), 3))
        sh.convert("RGB").save(out / f"icon_compare_{name_}.png")
    (out / "icon_compare_labels.txt").write_text(chr(10).join(f"{i_ + 1}. {nm_}" for i_, (nm_, _) in enumerate(cands)) + chr(10))
    (out / "info.json").write_text(json.dumps(dict(gen["info"], label=args.label), indent=2))


if __name__ == "__main__":
    main()
