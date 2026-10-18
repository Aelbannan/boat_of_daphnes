"""Load a J3D model into render-ready meshes (geometry via j3d_geom, materials/textures via gclib)."""
from __future__ import annotations
import io
import numpy as np
import j3d_geom as G


def load_meshes(data: bytes, only_joint_names=None, joint_pose=None, tints=None, joint_override=None):
    from gclib.j3d import J3D
    j = J3D(io.BytesIO(data))
    m = G.parse(data)
    for jn, delta in (joint_pose or {}).items():
        for jj in m.joints:
            if jj['name'] == jn:
                jj['rot'] = [a + b for a, b in zip(jj['rot'], delta)]
    for jn, (rot, trans) in (joint_override or {}).items():
        for jj in m.joints:
            if jj['name'] == jn:
                jj['rot'], jj['trans'] = list(rot), tuple(trans)
    world = G.joint_world(m)
    cache = {}
    meshes = []
    for jt, mt, s in m.scene:
        name = m.joints[jt]["name"]
        if only_joint_names and name not in only_joint_names:
            continue
        mat = j.mat3.materials[mt]
        tex = None
        wrap = "repeat"
        ti = mat.textures[0]
        if isinstance(ti, int):
            if ti not in cache:
                cache[ti] = j.tex1.textures[ti].render().convert("RGBA")
            tex = cache[ti]
            t = j.tex1.textures[ti]
            wrap = {"MirroredRepeat": "mirror", "Clamp": "clamp"}.get(t.wrap_s.name, "repeat")
        mname = j.mat3.mat_names[mt]
        if mname in ("fur", "inner_ear", "band", "eye") and tex is not None:
            tex = hood_preview_texture(mname, mat, j.tex1.textures[ti].render().convert("RGBA"), tints)
        t = G.triangles(m, s, world)
        t.update(cw_front=True, tex=tex, wrap=wrap, double=("None" in mat.cull_mode.name), joint=name, mat=mt, shape=s,
                 matname=j.mat3.mat_names[mt])
        meshes.append(t)
    return m, j, meshes


def hood_preview_texture(name, mat, grey_tex, tints=None):
    """What the hood's TEV does, for the software renderer: fur/band = grey * 2 * C0, inner_ear = lerp(C0, C1, mask).
    tints: optional dict {"C0": (r,g,b), "C1": (r,g,b)} per material name overriding the model's registers."""
    from PIL import Image
    if name == "eye":  # the TEV passes the texture colour through and takes alpha from the vertex (opaque)
        a = np.asarray(grey_tex.convert("RGBA")).copy()
        a[:, :, 3] = 255
        return Image.fromarray(a, "RGBA")
    c = mat.tev_colors
    c0 = np.array([c[1].r, c[1].g, c[1].b], np.float32)  # logical C0 = tev colour index 1 (index 0 is the game's light)
    c1 = np.array([c[2].r, c[2].g, c[2].b], np.float32)
    if tints and name in tints:
        c0 = np.array(tints[name].get("C0", c0), np.float32)
        c1 = np.array(tints[name].get("C1", c1), np.float32)
    g = np.asarray(grey_tex, np.float32)[:, :, :1] / 255.0
    if name == "inner_ear":
        rgb = (1 - g) * c0 + g * c1
    else:
        rgb = g * 2.0 * c0
    out = np.concatenate([np.clip(rgb, 0, 255), np.full(g.shape, 255.0, np.float32)], axis=2)
    return Image.fromarray(out.astype(np.uint8), "RGBA")
