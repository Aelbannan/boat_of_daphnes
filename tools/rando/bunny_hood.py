#!/usr/bin/env python3
"""First-launch step: the Bunny Hood's model and item icon, made from the player's own disc.

    python bunny_hood.py DISC.iso OUT_DIR

Reads the Nintendo Gallery workshop's static prop (res/stage/pfigure/room0.arc, bdl/model.bdl, the joint "omen":
four wall masks, the hood among them) from the disc, in memory, and reshapes it with the committed parameters in
bunny/bunny_hood_params.json (math only: a dome fitted over WW Link's head, the ears turned and rigged, the prop's
own texture).  Writes into OUT_DIR:

    bunnyhood.arc          RARC with bdl/bunnyhood.bdl (J3D2bdl4; joints root, ear_L0 > ear_L1, ear_R0 > ear_R1)
    bunnyhood_item.bdl     the same hood, rigid and centred (about 28 units across, Y up, front +Z), for the item-get model
    VamuY.arc              the disc's Hero's Charm archive with bunnyhood_item.bdl appended as the last file entry
                           (index printed in hood.json as vamuy_item_index; it sits inside the BDLM node's range; existing members untouched)
    bunnyhood_icon.bti     48x48 C8 / RGB5A3 item icon, laid out like WW's own icons
    bunnyhood_icon.png     the same art as a PNG
    hood.json              what was made (counts, joints)

The model is in WW Link's head_jnt local frame (+X up, +Y front, Z lateral, origin = the joint's origin): attach it
with that joint's matrix and no offset.  Nothing from the disc is kept in the repo: the prop's triangles and
texture are read at run time and the parameters are only numbers.

Needs numpy, Pillow and gclib (all in the release's embedded Python).  See docs/fork/bunny-hood.md.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
for folder in (HERE / "bunny", HERE, HERE.parent / "windows"):
    if folder.is_dir() and str(folder) not in sys.path:
        sys.path.insert(0, str(folder))


def SF_gap(surf, verts):
    import numpy as np
    import surface as SF
    idx = verts["band_idx"]
    pts = np.array(verts["pos"])[idx]
    d = SF.point_tri_distance(pts, surf["tri"])
    al = np.array(verts["band_alpha"])
    arch = np.abs(((al - 180.0 + 180.0) % 360.0) - 180.0) < 60.0
    out = dict(all=dict(vertices=int(len(idx)), mean=round(float(d.mean()), 3), max=round(float(d.max()), 3),
                        min=round(float(d.min()), 3)))
    out["excluding_back_arch"] = dict(vertices=int((~arch).sum()), mean=round(float(d[~arch].mean()), 3),
                                      max=round(float(d[~arch].max()), 3), min=round(float(d[~arch].min()), 3))
    out["offset_asked"] = None
    return out


def mdl3_colour_offsets(bdl: bytes):
    """Where the colour registers live in the BDL's MDL3 display lists, for a host that patches bytes: per material (in
    MAT3 order) the file offsets of the 3 byte BP data of the logical C0 (GX_TEVREG1: BP 0xE4 = R,A and 0xE5 = B,G, the BG
    command repeated 3 times) and the logical C1 (GX_TEVREG2: 0xE6 / 0xE7).  (GX_TEVREG0 is not used: the game's lighting
    rewrites it on every draw.)  Data layout: 24 bits, low 11 bits = R (or B), bits 12..22 = A (or G), bit 23 = 0."""
    import struct
    import j3d_geom as G
    off, c = G.chunks_of(bdl)["MDL3"]
    n, po = struct.unpack_from(">H", c, 8)[0], struct.unpack_from(">I", c, 0x0C)[0]
    out = []
    for i in range(n):
        eo, es = struct.unpack_from(">II", c, po + 8 * i)
        a = po + eo
        seg = c[a:a + es]
        d = {"C0": {"ra": [], "bg": []}, "C1": {"ra": [], "bg": []}}
        k = 0
        while k < len(seg) - 4:
            if seg[k] == 0x61 and seg[k + 1] in (0xE4, 0xE5, 0xE6, 0xE7) and not (seg[k + 2] & 0x80):
                reg = "C0" if seg[k + 1] in (0xE4, 0xE5) else "C1"
                d[reg]["ra" if seg[k + 1] in (0xE4, 0xE6) else "bg"].append(off + a + k + 2)
                k += 5
            else:
                k += 1
        out.append(d)
    return out


def generate(iso: str, params_path: Path = None):
    """-> dict(arc=bytes, bti=bytes, png=PIL.Image, bdl=bytes, info=dict)"""
    import io
    import numpy as np
    import swift_sail as S
    import j3d_geom as G
    import hoodgen as HG
    import bmd_write as W
    import rarc_write as RW
    import matgen2
    import surface as SF
    import icon
    import bti_c8

    prm = json.loads(Path(params_path or HERE / "bunny" / "bunny_hood_params.json").read_text())
    disc = S.Disc(iso)
    try:
        room = S.open_archive(disc, prm["prop"]["stage_room"])
    finally:
        disc.close()
    model = room[prm["prop"]["model"]]
    prop = HG.extract_prop(model, prm["prop"])
    disc = S.Disc(iso)
    try:
        cl_bdl = S.open_archive(disc, "res/object/link.arc")["bdl/cl.bdl"]
        wait_bck = S.open_archive(disc, "res/object/lkanm.arc")["bcks/wait.bck"]
    finally:
        disc.close()
    # fit to Link as he stands in the game: the wait animation, the hat at its gravity rest (restpose.py)
    surf = SF.hat_cap(cl_bdl, wait_bck=wait_bck)
    verts, tris, joints = HG.build(prop, prm, surf)

    # the prop's own material (mask_v) and texture (mask_all)
    g = G.parse(model)
    mat_node = [mt for (j, mt, s) in g.scene if g.joints[j]["name"] == prm["prop"]["joint"]][0]
    from gclib.j3d import J3D as _J
    _j = _J(io.BytesIO(model))
    _tn = _j.tex1.texture_names.index(prm["prop"]["texture"])
    quarter = _j.tex1.textures[_tn].render().convert("RGBA").crop((64, 0, 128, 64))
    mat3, mdl3, tex1, colour_defaults = matgen2.material_chunks(model, mat_node, quarter, prm)
    bdl = W.build_model(joints, verts, tris, mat3, tex1, mdl3, group_order=["fur", "ear", "band", "eye"])

    # read it back: geometry must survive the round trip
    back = G.parse(bdl)
    nt = sum(len(G.triangles(back, si)["pos"]) for si in range(len(back.shapes)))
    if nt != len(tris):
        raise RuntimeError("bunny hood model failed its read-back check")
    from gclib.j3d import J3D
    J3D(io.BytesIO(bdl))  # gclib must parse every chunk

    arc = RW.write_rarc({"bdl/bunnyhood.bdl": bdl}, "bunnyhood")

    # the item-get / field variant: rigid, centred on its own origin, upright (Y up, front +Z), about 28 units across
    import vamuy
    ip = prm["item"]
    v2 = dict(verts)
    p = np.array(verts["pos"], float)
    n = np.array(verts["nrm"], float)
    # world pose of the rest model (ears' joint transforms applied), then head frame (X up, Y front, Z lateral) to item frame
    from j3d_geom import joint_local
    loc = [joint_local(dict(scale=j["scale"], rot=j["rot"], trans=j["trans"])) for j in joints]
    wmat = []
    for i, j in enumerate(joints):
        wmat.append(loc[i] if j["parent"] < 0 else wmat[j["parent"]] @ loc[i])
    # vertex positions are already in model space (the envelope/rigid conversion happens in the writer), so no change
    perm = lambda a: np.stack([a[:, 2], a[:, 0], a[:, 1]], axis=1)
    p2, n2 = perm(p), perm(n)
    lo, hi = p2.min(0), p2.max(0)
    centre = (lo + hi) / 2
    scale = ip["size"] / float((hi - lo).max())
    v2["pos"] = (p2 - centre) * scale
    v2["nrm"] = n2
    v2["skin"] = [[(0, 1.0)] for _ in p2]
    item_joints = [dict(name="bunnyhood_item", parent=-1, scale=(1, 1, 1), rot=(0, 0, 0), trans=(0, 0, 0))]
    mat3b, mdl3b, tex1b, _cd = matgen2.material_chunks(model, mat_node, quarter, prm)
    item_bdl = W.build_model(item_joints, v2, tris, mat3b, tex1b, mdl3b, group_order=["fur", "ear", "band", "eye"])
    disc2 = S.Disc(iso)
    try:
        vam = S.open_archive(disc2, "res/object/VamuY.arc")
    finally:
        disc2.close()
    # re-pack the disc's archive uncompressed from its own parts is unnecessary: the decompressed bytes are the archive
    disc3 = S.Disc(iso)
    try:
        vam_raw = S.yaz0_decompress(disc3.read("res/object/VamuY.arc"))
    finally:
        disc3.close()
    vamuy_arc, item_index = vamuy.append_file(vam_raw, "bunnyhood.bdl", item_bdl)
    chk = S.rarc_files(vamuy_arc)
    if chk["bdlm/vamuy.bdl"] != vam["bdlm/vamuy.bdl"] or chk["btk/vamuy.btk"] != vam["btk/vamuy.btk"]:
        raise RuntimeError("VamuY.arc: an existing member changed")
    # the icon is drawn from a copy of the hood with larger eyes (they must read at 48 pixels), without the band follow
    import copy
    prm_i = copy.deepcopy(prm)
    prm_i["dome"]["eye"]["radius_x"] *= prm["icon"]["eye_scale"]
    prm_i["dome"]["eye"]["radius_y"] *= prm["icon"]["eye_scale"]
    v_i, t_i, j_i = HG.build(prop, prm_i, surf)
    bdl_i = W.build_model(j_i, v_i, t_i, mat3, tex1, mdl3, group_order=["fur", "ear", "band", "eye"])
    png = icon.render_icon(bdl_i, prm)
    bti = bti_c8.encode(png)
    gap = SF_gap(surf, verts)
    colors = [dict(name=n, material=m, reg=r, default="%02X%02X%02X" % colour_defaults[m]) for n, m, r in
              (("Body", "fur", 0), ("Inner ears", "inner_ear", 1), ("Band", "band", 0))]
    names_ = ["fur", "inner_ear", "band"]
    followers = [dict(joint=j["name"], link_joint=j["link_joint"], rest_head_frame=j["rest_head_frame"])
                 for j in joints if "link_joint" in j]
    info = dict(followers=followers,
                follower_protocol="reference pose = Link idle (wait.bck frame 0) with the hat at its gravity rest; each frame set a "
                "follower joint's local transform to (inverse(head_jnt node) * node of its link_joint), a 3x4 in the head frame; "
                "its rest transform (rest_head_frame) is the same quantity at the reference pose",
                mdl3_patch={"head": dict(zip(names_, mdl3_colour_offsets(bdl))), "item": dict(zip(names_, mdl3_colour_offsets(item_bdl)))}, colors=colors, colour_registers="C0 = J3DMaterial tev colour index 1 (GX_TEVREG1, MDL3 BP 0xE4/0xE5), C1 = index 2 (GX_TEVREG2, BP 0xE6/0xE7); index 0 is rewritten by the game's lighting; "
                "fur and band: tex(grey)*C0*2*shade; inner_ear: lerp(C0,C1,mask)*shade; C0 of fur and inner_ear are the body colour",
                band_gap=gap, item_bdl_bytes=len(item_bdl), vamuy_arc_bytes=len(vamuy_arc), vamuy_item_index=item_index, extras_format=3, vertices=int(len(verts["pos"])), triangles=int(len(tris)), joints=[j["name"] for j in joints],
                bdl_bytes=len(bdl), arc_bytes=len(arc), icon_bti_bytes=len(bti))
    return dict(item_bdl=item_bdl, vamuy=vamuy_arc, arc=arc, bti=bti, png=png, bdl=bdl, info=info, params=prm, verts=verts, tris=tris, joints=joints)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("iso")
    ap.add_argument("out")
    ap.add_argument("--params", help="parameter file (default: bunny/bunny_hood_params.json)")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    r = generate(args.iso, args.params)
    (out / "bunnyhood.arc").write_bytes(r["arc"])
    (out / "bunnyhood_icon.bti").write_bytes(r["bti"])
    (out / "bunnyhood_item.bdl").write_bytes(r["item_bdl"])
    (out / "VamuY.arc").write_bytes(r["vamuy"])
    r["png"].save(out / "bunnyhood_icon.png")
    (out / "hood.json").write_text(json.dumps(r["info"], indent=2))
    print(json.dumps(r["info"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
