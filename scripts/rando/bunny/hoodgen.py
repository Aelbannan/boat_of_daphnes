"""The Bunny Hood geometry: the gallery prop (the player's own disc) reshaped by committed parameters.

Inputs: the pfigure room model (bytes) and the parameter dict (bunny_hood_params.json).
Outputs: joints, vertex arrays with skin weights, triangles -- all in WW Link's head_jnt local frame.

What is kept from the prop: its texture and UV layout (the cap's face and the ears' inner colour), its triangle
topology for the two ears, its snout relief (scaled down), its outline proportions.  What is generated: the cap
becomes an ellipsoid dome so the hood has volume and a back; the flat ears are oriented, tilted and given joints.
"""
from __future__ import annotations
import numpy as np
import j3d_geom as G


# ------------------------------------------------------------------ prop extraction
def extract_prop(model: bytes, P: dict):
    m = G.parse(model)
    world = G.joint_world(m)
    shape = [s for (j, mt, s) in m.scene if m.joints[j]["name"] == P["joint"]]
    assert len(shape) == 1, "prop joint not found"
    t = G.triangles(m, shape[0], world)
    box = P["select_box_room"]
    lo = np.array([box["x"][0], box["y"][0], box["z"][0]])
    hi = np.array([box["x"][1], box["y"][1], box["z"][1]])
    inside = ((t["pos"] >= lo) & (t["pos"] <= hi)).all(axis=(1, 2))
    pos, uv, clr = t["pos"][inside], t["uv"][inside], t["clr"][inside]
    zc, by, bx = P["centre_z"], P["cap_base_y"], P["back_plane_x"]
    uvd = np.stack([zc - pos[..., 2], pos[..., 1] - by, pos[..., 0] - bx], axis=-1)  # (u right, v up, d toward front)
    # connected components on rounded flat positions
    keys, par = {}, []

    def find(a):
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a

    ids = []
    for tri in pos:
        tt = []
        for v in tri:
            k = tuple(np.round(v, 2))
            if k not in keys:
                keys[k] = len(par)
                par.append(len(par))
            tt.append(keys[k])
        ids.append(tt)
        for a in tt[1:]:
            par[find(a)] = find(tt[0])
    comp = np.array([find(t0[0]) for t0 in ids])
    uniq, counts = np.unique(comp, return_counts=True)
    cap_id = uniq[np.argmax(counts)]
    parts = {"cap": comp == cap_id}
    ears = [c for c in uniq if c != cap_id]
    assert len(ears) == 2, f"expected 2 ears, found {len(ears)}"
    ears.sort(key=lambda c: uvd[comp == c][..., 0].mean())
    parts["ear_R"] = comp == ears[0]  # u < 0  -> Link's right (-Z)
    parts["ear_L"] = comp == ears[1]  # u > 0  -> Link's left (+Z)
    return dict(uvd=uvd, uv=uv, clr=clr, parts=parts)


# ------------------------------------------------------------------ cap lookup (front projection)
def _closest_bary(p, a, b, c):
    """Barycentric coordinates of the point in triangle abc closest to p (2D)."""
    v0, v1, v2 = b - a, c - a, p - a
    d00, d01, d11 = v0 @ v0, v0 @ v1, v1 @ v1
    d20, d21 = v2 @ v0, v2 @ v1
    den = d00 * d11 - d01 * d01
    if abs(den) < 1e-12:
        return np.array([1.0, 0, 0])
    w1 = (d11 * d20 - d01 * d21) / den
    w2 = (d00 * d21 - d01 * d20) / den
    w0 = 1 - w1 - w2
    if w0 >= 0 and w1 >= 0 and w2 >= 0:
        return np.array([w0, w1, w2])
    best, bd = None, 1e18
    for (i, j) in ((0, 1), (1, 2), (2, 0)):
        pts = (a, b, c)
        s, e = pts[i], pts[j]
        d = e - s
        tt = np.clip(((p - s) @ d) / max(d @ d, 1e-12), 0, 1)
        q = s + tt * d
        dd = (p - q) @ (p - q)
        if dd < bd:
            bd = dd
            w = np.zeros(3)
            w[i], w[j] = 1 - tt, tt
            best = w
    return best


class CapLookup:
    def __init__(self, prop):
        sel = prop["parts"]["cap"]
        self.uvd = prop["uvd"][sel]
        self.uv = prop["uv"][sel]
        self.clr = prop["clr"][sel]

    def query(self, u, v):
        """-> (uv (2,), d, brightness) at flat point (u, v); outside the outline the nearest point is used."""
        p = np.array([u, v])
        best, bd = None, 1e18
        for t in range(len(self.uvd)):
            a, b, c = self.uvd[t][:, :2]
            w = _closest_bary(p, a, b, c)
            q = w[0] * a + w[1] * b + w[2] * c
            dd = (p - q) @ (p - q)
            if dd < bd:
                bd, best = dd, (t, w)
                if dd < 1e-9:
                    break
        t, w = best
        uv = w @ self.uv[t]
        d = w @ self.uvd[t][:, 2]
        return uv, d, bd


# ------------------------------------------------------------------ the hood
def rot_xyz_zyx(R):
    """Euler (rx, ry, rz) for R = Rz Ry Rx (J3D joint convention)."""
    ry = -np.arcsin(np.clip(R[2, 0], -1, 1))
    rx = np.arctan2(R[2, 1], R[2, 2])
    rz = np.arctan2(R[1, 0], R[0, 0])
    return rx, ry, rz


def smoothstep(x):
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def build(prop, prm: dict, surf=None):
    """v5: the shell is fitted to Link's hat.  A headband strip follows the hat cap's brim all the way round (rays from
    the head centre against the cap, hit point plus an even offset along the surface normal), and above it the shell
    blends from the cap's surface to a tilted ellipsoid.  The lower edge is lifted into an arch at the back so the hat tail
    comes out beneath it.  Triangles are tagged by material: fur (shell, ears' backs), band, ear (inner-ear front)."""
    import surface as SF
    D, E, S = prm["dome"], prm["ears"], prm["shading"]
    cx, cy, cz = D["centre"]
    H, B, C = D["radius_up"], D["radius_front_back"], D["radius_lateral"]
    naz = D["azimuth_steps"]
    F0 = D["face_uv"]
    Bd = D["band"]

    def texel(x, y):  # quarter-relative uv of the fur texture (64x64 = the prop's top-right quarter)
        return np.array([x / 64.0, y / 64.0])

    def prop_uv(u):  # the prop's own uv (whole 128x128 texture) -> quarter-relative
        return np.array([(u[0] * 128.0 - F0["quad_origin"][0]) / 64.0, (u[1] * 128.0 - F0["quad_origin"][1]) / 64.0])

    cream_uv = texel(*F0["cream_texel"])
    band_uv = np.array([0.5, 0.5])
    pitch = np.radians(D.get("pitch_back_deg", 0.0))
    Rp = np.array([[np.cos(pitch), -np.sin(pitch), 0], [np.sin(pitch), np.cos(pitch), 0], [0, 0, 1]])
    ctr = np.array([cx, cy, cz])
    inv_axes = np.array([1.0 / H, 1.0 / B, 1.0 / C])

    pos, nrm, uv, skin, tris, tone, tgrp = [], [], [], [], [], [], []

    def add(p, n, t, w, tn=1.0):
        tone.append(tn)
        pos.append(np.asarray(p, float))
        nn = np.asarray(n, float)
        nrm.append(nn / (np.linalg.norm(nn) + 1e-9))
        uv.append(np.asarray(t, float))
        skin.append(w)
        return len(pos) - 1

    def tri(a, b, c, g):
        tris.append((a, b, c))
        tgrp.append(g)

    follow_joints = sorted({jn for sk in surf["skin"] for tri_ in sk for (jn, w_) in tri_ if jn in (26, 27, 28)})
    follow_index = {jn: 5 + i for i, jn in enumerate(follow_joints)}

    def brim_skin(hit_k):
        """The hat's weights at a surface hit (the hit triangle's three vertices, averaged), as hood joints: Link's head
        joint is the model's root (the model is in the head's frame), the hat joints become follower joints."""
        acc = {}
        for vs in surf["skin"][hit_k]:
            for jn, w_ in vs:
                key = 0 if jn == 15 else follow_index.get(jn, 0)
                acc[key] = acc.get(key, 0.0) + w_ / 3.0
        tot = sum(acc.values())
        q = {k_: round(v_ / tot * 10) / 10.0 for k_, v_ in acc.items()}
        q = {k_: v_ for k_, v_ in q.items() if v_ > 0}
        tq = sum(q.values()) or 1.0
        return [(k_, v_ / tq) for k_, v_ in q.items()]

    def direction(al, be):
        return np.array([np.sin(be), np.cos(be) * np.cos(al), np.cos(be) * np.sin(al)])

    def hit(al, be):
        h = SF.raycast(surf, ctr, direction(al, be))
        return h if (h and h[0] < Bd["max_hit_dist"]) else None

    def ell_point(d):  # where the ray from the centre meets the tilted ellipsoid
        q = inv_axes * (Rp.T @ d)
        return ctr + d / np.linalg.norm(q)

    nb, nu = Bd["rows"], D["rows_above"]
    betas = np.arange(-1.0, 1.45, 0.02)
    rows = []
    lows = []
    band_idx = []
    band_alpha = []
    for j in range(naz):
        al = 2 * np.pi * j / naz
        dd = ((np.degrees(al) - 180.0 + 180.0) % 360.0) - 180.0
        bump = np.exp(-(dd / Bd["notch_width_deg"]) ** 2)
        # the brim: the lowest ray angle that still hits the cap near the head (a contiguous run up from the first hit)
        low = None
        for be in betas:
            if hit(al, be):
                low = be
                break
        if low is None:
            low = 0.6
        low = low + bump * max(0.0, Bd["notch_floor_beta"] - low)
        lows.append(low)
        col = []
        total = nb + nu
        for k in range(total):
            if k < nb:
                be = low + Bd["width_rad"] * k / max(nb - 1, 1)
                s_ = 0.0
            else:
                t = (k - nb + 1) / nu
                be0 = low + Bd["width_rad"]
                be = be0 + (np.pi / 2 - be0) * t
                s_ = smoothstep((be - be0) / (D["blend_to_ellipsoid_rad"]))
            if k == total - 1:
                be = np.pi / 2 - 1e-3
            d = direction(al, be)
            h = hit(al, be)
            pe = ell_point(d)
            if h:
                ph = ctr + d * h[0] + h[1] * Bd["offset"]
                nh = h[1]
            else:
                ph, nh = pe, (pe - ctr) / np.linalg.norm(pe - ctr)
            p = (1 - s_) * ph + s_ * pe
            n = (1 - s_) * nh + s_ * ((pe - ctr) / np.linalg.norm(pe - ctr))
            n = n / np.linalg.norm(n)
            tx, tn_ = (band_uv, 1.0) if k < nb else (cream_uv, 1.0)
            sk_ = [(0, 1.0)]
            if h and k < nb + D["band"]["fade_rows"]:
                bs = brim_skin(h[2])
                fade = 1.0 if k < nb else 1.0 - (k - nb + 1) / (D["band"]["fade_rows"] + 1.0)
                sk_ = [(0, 1.0 - fade * (1.0 - dict(bs).get(0, 0.0)))] + [(j_, w_ * fade) for j_, w_ in bs if j_ != 0]
                sk_ = [(j_, w_) for j_, w_ in sk_ if w_ > 1e-3]
            vi = add(p, n, tx, sk_, tn_)
            col.append(vi)
            if k < nb:
                band_idx.append(vi)
                band_alpha.append(float(np.degrees(al)))
        rows.append(col)
    # an apex vertex closes the top
    apex = add(ell_point(np.array([1.0, 0, 0])), [1, 0, 0], cream_uv, [(0, 1.0)])
    total = nb + nu
    for j in range(naz):
        j2 = (j + 1) % naz
        for k in range(total - 1):
            a, b, c, d = rows[j][k], rows[j2][k], rows[j][k + 1], rows[j2][k + 1]
            g = "band" if k < nb else "fur"
            tri(a, b, d, g)
            tri(a, d, c, g)
        tri(rows[j][total - 1], rows[j2][total - 1], apex, "fur")


    # ---- the eyes: a decal per side, laid ON the shell (every vertex is a shell point found by the same ray fit, then
    # lifted by a tiny offset against z-fighting), so it follows the surface instead of sitting on a flat disc
    ED = D["eye"]
    eye_report = []

    def shell_point(al, be):
        al = al % (2 * np.pi)
        f = al / (2 * np.pi) * naz
        j0_, fr = int(np.floor(f)) % naz, f - np.floor(f)
        low_ = (1 - fr) * lows[j0_] + fr * lows[(j0_ + 1) % naz]
        be0 = low_ + Bd["width_rad"]
        s_ = smoothstep((be - be0) / D["blend_to_ellipsoid_rad"])
        d = direction(al, be)
        pe = ell_point(d)
        h = hit(al, be)
        if h:
            ph, nh = ctr + d * h[0] + h[1] * Bd["offset"], h[1]
        else:
            ph, nh = pe, (pe - ctr) / np.linalg.norm(pe - ctr)
        n_e = (pe - ctr) / np.linalg.norm(pe - ctr)
        n = (1 - s_) * nh + s_ * n_e
        return (1 - s_) * ph + s_ * pe, n / np.linalg.norm(n)

    for side in (1.0, -1.0):
        al0 = side * np.radians(ED["azimuth_deg"])
        # elevation: solved so the eye sits a given fraction of the hood's height above the FRONT rim (MM: 0.39)
        rim_x = shell_point(0.0, lows[0])[0][0]
        apex_x = pos[apex][0]
        target_x = rim_x + ED["height_frac"] * (apex_x - rim_x)
        lo_b, hi_b = lows[0], np.pi / 2 - 0.02
        for _ in range(40):
            mid_b = 0.5 * (lo_b + hi_b)
            if shell_point(al0, mid_b)[0][0] < target_x:
                lo_b = mid_b
            else:
                hi_b = mid_b
        be0_ = 0.5 * (lo_b + hi_b)
        pc, nc = shell_point(al0, be0_)
        eye_report.append((float(np.degrees(al0)), float(be0_), pc.tolist(), float(rim_x), float(apex_x)))
        R0 = np.linalg.norm(pc - ctr)
        rx, ry = ED["radius_x"], ED["radius_y"]
        tilt = np.radians(ED.get("slant_deg", 0.0)) * side  # a slight slant of the oval, like MM's
        rings_ = ED["rings"]

        def eye_uv(x_, y_):  # x_, y_ in -1..1 across the oval, (0,0) = centre; u mirrored on the other side so the highlight stays on the outer-upper corner
            return np.array([0.5 + 0.5 * x_ * side * -1.0 * -1.0, 0.5 - 0.5 * y_])

        def eye_vertex(x_, y_):
            ex_, ey_ = x_ * rx, y_ * ry
            cx_, sx_ = np.cos(tilt), np.sin(tilt)
            ex2, ey2 = ex_ * cx_ - ey_ * sx_, ex_ * sx_ + ey_ * cx_
            pp, nn = shell_point(al0 + ex2 / (R0 * np.cos(be0_)), be0_ + ey2 / R0)
            return add(pp + nn * ED["lift"], nn, eye_uv(x_, y_), [(0, 1.0)])

        ctr_i = eye_vertex(0.0, 0.0)
        grid = []
        for ri in range(1, rings_ + 1):
            f_ = ri / rings_
            grid.append([eye_vertex(f_ * np.cos(2 * np.pi * q / ED["segments"]), f_ * np.sin(2 * np.pi * q / ED["segments"]))
                         for q in range(ED["segments"])])

        def face(a_, b_, c_):
            n_ = np.cross(pos[b_] - pos[a_], pos[c_] - pos[a_])
            tri(a_, b_, c_, "eye") if n_ @ nc >= 0 else tri(a_, c_, b_, "eye")

        for q in range(ED["segments"]):
            face(ctr_i, grid[0][q], grid[0][(q + 1) % ED["segments"]])
        for ri in range(rings_ - 1):
            for q in range(ED["segments"]):
                q2 = (q + 1) % ED["segments"]
                face(grid[ri][q], grid[ri + 1][q], grid[ri + 1][q2])
                face(grid[ri][q], grid[ri + 1][q2], grid[ri][q2])

    # ---- ears
    joints = [dict(name="root", parent=-1, scale=(1, 1, 1), rot=(0, 0, 0), trans=(0, 0, 0))]
    uvd_all, uv_all = prop["uvd"], prop["uv"]
    k_len = E["length"]
    for side, nm, sgn in (("ear_L", "L", 1.0), ("ear_R", "R", -1.0)):
        sel = prop["parts"][side]
        pts = uvd_all[sel].reshape(-1, 3)
        c0 = pts[:, :2].mean(0)
        w, vec = np.linalg.eigh(np.cov((pts[:, :2] - c0).T))
        pa = vec[:, np.argmax(w)]
        if pa[1] < 0:
            pa = -pa
        perp = np.array([pa[1], -pa[0]])
        s_all = (pts[:, :2] - c0) @ pa
        s0, s1 = s_all.min(), s_all.max()
        scale = k_len / (s1 - s0)
        lean, splay = np.radians(E["lean_back_deg"]), np.radians(E["splay_deg"])
        Xe = np.array([np.cos(lean) * np.cos(splay), -np.sin(lean), sgn * np.cos(lean) * np.sin(splay)])
        Xe /= np.linalg.norm(Xe)
        Ze = np.cross(Xe, [0, 1.0, 0])
        Ze /= np.linalg.norm(Ze)
        Ye = np.cross(Ze, Xe)
        tw = np.radians(E.get("twist_deg", 0.0))
        Ye, Ze = np.cos(tw) * Ye + sgn * np.sin(tw) * Ze, np.cos(tw) * Ze - sgn * np.sin(tw) * Ye
        Rm = np.stack([Xe, Ye, Ze], axis=1)
        be, ae = E["pivot_beta_rad"], np.radians(E["pivot_az_from_back_deg"])
        alp = np.pi - sgn * ae
        pv = np.array([cx + H * np.sin(be), cy + B * np.cos(be) * np.cos(alp), cz + C * np.cos(be) * np.sin(alp)])
        P = ctr + Rp @ (pv - ctr) - Xe * E["sink"]
        rx, ry, rz = rot_xyz_zyx(Rm)
        j0 = len(joints)
        joints.append(dict(name=f"ear_{nm}0", parent=0, scale=(1, 1, 1), rot=(rx, ry, rz), trans=tuple(P)))
        joints.append(dict(name=f"ear_{nm}1", parent=j0, scale=(1, 1, 1), rot=(0, 0, 0),
                           trans=(E["mid_fraction"] * k_len, 0, 0)))
        tri_pts, tri_uv = uvd_all[sel], uv_all[sel]

        def ear_section(sc_):
            """The ear's cross-section at axis distance sc_: its min/max width crossings and the 6 ring points."""
            cr = []
            for tp in tri_pts:
                p3 = [(((q[:2] - c0) @ pa - s0) * scale, ((q[:2] - c0) @ perp) * scale * E["width_scale"], q[2] * scale * 0.5)
                      for q in tp]
                for ia, ib in ((0, 1), (1, 2), (2, 0)):
                    a3, b3 = p3[ia], p3[ib]
                    if (a3[0] - sc_) * (b3[0] - sc_) < 0:
                        f_ = (sc_ - a3[0]) / (b3[0] - a3[0])
                        cr.append((a3[1] + f_ * (b3[1] - a3[1]), a3[2] + f_ * (b3[2] - a3[2])))
            if not cr:
                return None
            lo_, hi_ = min(cr), max(cr)
            fl_ = 1.0 + E["root_flare"] * smoothstep(1.0 - sc_ / E["root_flare_len"])
            tr2 = E["thickness"] * (1.0 + E["root_thick"] * smoothstep(1.0 - sc_ / E["root_flare_len"]))

            def sp(t0, d0, back):
                t1 = t0 * fl_
                cup_ = E.get("cup", 0.0) * (t1 / (0.5 * k_len * 0.65)) ** 2
                return P + Xe * sc_ + Ze * t1 + Ye * (d0 - cup_ - (tr2 if back else 0.0))

            tm, dm = 0.5 * (lo_[0] + hi_[0]), 0.5 * (lo_[1] + hi_[1])
            return lo_, hi_, [sp(lo_[0], lo_[1], False), sp(tm, dm, False), sp(hi_[0], hi_[1], False),
                              sp(hi_[0], hi_[1], True), sp(tm, dm, True), sp(lo_[0], lo_[1], True)]

        def outside_shell(p_, margin):
            dv = p_ - ctr
            dist = np.linalg.norm(dv)
            dv = dv / dist
            ps_, _ = shell_point(np.arctan2(dv[2], dv[1]), np.arcsin(np.clip(dv[0], -1, 1)))
            return dist > np.linalg.norm(ps_ - ctr) + margin

        sc_found, crossings_t, crossings_t2, ring_top_found = None, None, None, None
        for sc_try in np.arange(E["collar_search"][0], E["collar_search"][1], 0.5):
            sec_ = ear_section(sc_try)
            if sec_ and all(outside_shell(p_, E["collar_margin"]) for p_ in sec_[2]):
                sc_found, crossings_t, crossings_t2, ring_top_found = sc_try, sec_[0], sec_[1], sec_[2]
                break
        if sc_found is None:
            sc_found = E["collar_search"][1]
            sec_ = ear_section(sc_found)
            crossings_t, crossings_t2, ring_top_found = sec_
        # The ear's outline (edges used by one triangle only) and each vertex's distance to it, in the ear's own (s, t) plane:
        # beyond the root the two faces are pinched together towards the outline and the tip, so the ear is one closed,
        # rounded and tapered solid (the front and back meet along the whole edge and in one point at the tip), not two sheets.
        def st_of(q):
            return (((q[:2] - c0) @ pa - s0) * scale, ((q[:2] - c0) @ perp) * scale * E["width_scale"])

        def pkey(q):
            return (round(float(q[0]), 2), round(float(q[1]), 2), round(float(q[2]), 2))

        edge_n, edge_pts = {}, {}
        for tp in tri_pts:
            for ia, ib in ((0, 1), (1, 2), (2, 0)):
                ka, kb = pkey(tp[ia]), pkey(tp[ib])
                ek = (ka, kb) if ka < kb else (kb, ka)
                edge_n[ek] = edge_n.get(ek, 0) + 1
                edge_pts[ek] = (st_of(tp[ia]), st_of(tp[ib]))
        outline = np.array([edge_pts[e_] for e_, n_ in edge_n.items() if n_ == 1], float)  # (n, 2 ends, 2)

        def outline_dist(q):
            sq, tq = st_of(q)
            pt = np.array([sq, tq])
            a_, b_ = outline[:, 0, :], outline[:, 1, :]
            ab = b_ - a_
            u_ = np.clip(((pt - a_) * ab).sum(1) / np.maximum((ab * ab).sum(1), 1e-9), 0.0, 1.0)
            return float(np.linalg.norm(a_ + ab * u_[:, None] - pt, axis=1).min())

        for layer in (0, 1):
            idx_map, tl = {}, []
            for tp, tu in zip(tri_pts, tri_uv):
                row = []
                for q, qu in zip(tp, tu):
                    key = (round(q[0], 2), round(q[1], 2), round(q[2], 2), round(float(qu[0]), 4), round(float(qu[1]), 4))  # a position can carry several UVs (seams)
                    if key not in idx_map:
                        s_ = ((q[:2] - c0) @ pa - s0) * scale
                        flare = 1.0 + E["root_flare"] * smoothstep(1.0 - s_ / E["root_flare_len"])
                        t_ = ((q[:2] - c0) @ perp) * scale * E["width_scale"] * flare
                        cup = E.get("cup", 0.0) * (t_ / (0.5 * k_len * 0.65)) ** 2
                        thick_root = E["thickness"] * (1.0 + E["root_thick"] * smoothstep(1.0 - s_ / E["root_flare_len"]))
                        # the volume: 1 at the root (the collar's ring is made for it), pinching to 0 on the outline and the tip
                        edge_g = smoothstep(min(1.0, outline_dist(q) / E.get("edge_round_len", 2.4)))
                        ramp = smoothstep((s_ - (sc_found + E["root_rigid_after"])) / E.get("pinch_len", 12.0))
                        vol = 1.0 - ramp * (1.0 - edge_g)
                        off = q[2] * scale * 0.5 - cup + (E.get("front_bulge", 0.0) * thick_root * vol if layer == 0 else -thick_root * vol)
                        p3 = P + Xe * s_ + Ze * t_ + Ye * off
                        w1 = smoothstep((s_ / k_len - E["blend"][0]) / (E["blend"][1] - E["blend"][0]))
                        a_ = smoothstep((s_ - (sc_found + E["root_rigid_after"])) / E["root_ramp_len"])  # 0 = the hood's own root joint
                        sk = [(0, 1 - a_), (j0, a_ * (1 - w1)), (j0 + 1, a_ * w1)]
                        sk = [(jj, ww) for jj, ww in sk if ww > 1e-3]
                        tcoord = prop_uv(qu) if layer == 0 else texel(*E["back_texel"])
                        idx_map[key] = add(p3, Ye if layer == 0 else -Ye, tcoord, sk, 1.0 if layer == 0 else 0.86)
                    row.append(idx_map[key])
                tl.append(tuple(row))
            want = Ye if layer == 0 else -Ye
            for a, b, c in tl:
                n = np.cross(pos[b] - pos[a], pos[c] - pos[a])
                tri(a, b, c, "ear") if (n @ want >= 0 and layer == 0) else None
                if layer == 0 and n @ want < 0:
                    tri(a, c, b, "ear")
                if layer == 1:
                    tri(a, b, c, "fur") if n @ want >= 0 else tri(a, c, b, "fur")

        # ---- the root collar: a flared skirt from the ear's own cross-section, taken where the ear has fully emerged from
        # the shell, down onto the shell, so the ear grows out of the hood with no cut edge.  Rigid on the root joint.
        if True:
            tmin, tmax = crossings_t, crossings_t2
            sc = sc_found
            fl = 1.0 + E["root_flare"] * smoothstep(1.0 - sc / E["root_flare_len"])
            tr_ = E["thickness"] * (1.0 + E["root_thick"] * smoothstep(1.0 - sc / E["root_flare_len"]))
            ring_top = ring_top_found
            cen = sum(ring_top) / len(ring_top)
            rings = [ring_top]
            ring_bot, ring_mid = [], []
            for p_ in ring_top:
                out_ = p_ - cen
                out_ -= Xe * (out_ @ Xe)
                out_ /= np.linalg.norm(out_) + 1e-9
                o_ = p_ + out_ * E["collar_out"] - Xe * E["collar_drop"]
                dvec = o_ - ctr
                dvec = dvec / np.linalg.norm(dvec)
                ps_, ns_ = shell_point(np.arctan2(dvec[2], dvec[1]), np.arcsin(np.clip(dvec[0], -1, 1)))
                ring_bot.append(ps_ + ns_ * E["collar_lift"])
                ring_mid.append(0.5 * (p_ + ring_bot[-1]) + out_ * E["collar_out"] * 0.28)
            idx = []
            for ring, nk in ((ring_top, 0.0), (ring_mid, 0.5), (ring_bot, 1.0)):
                row = []
                for p_ in ring:
                    out_ = p_ - cen
                    out_ -= Xe * (out_ @ Xe)
                    out_ /= np.linalg.norm(out_) + 1e-9
                    nrm_ = (1 - nk) * out_ + nk * ((p_ - ctr) / np.linalg.norm(p_ - ctr))
                    row.append(add(p_, nrm_, cream_uv, [(0, 1.0)]))
                idx.append(row)
            for r_ in range(2):
                for q_ in range(6):
                    q2_ = (q_ + 1) % 6
                    a_, b_, c_, d_ = idx[r_][q_], idx[r_][q2_], idx[r_ + 1][q_], idx[r_ + 1][q2_]
                    for tri_ in ((a_, b_, d_), (a_, d_, c_)):
                        tri(*tri_, "fur")

    pos, uv = np.array(pos), np.array(uv)
    nrm = np.array(nrm)
    Ld = np.array(S["light"], float)
    Ld /= np.linalg.norm(Ld)
    shade = (S["ambient"] + (1 - S["ambient"]) * np.clip(np.abs(nrm @ Ld), 0, 1)) * np.array(tone)
    clr = np.stack([shade, shade, shade, np.ones_like(shade)], axis=1)
    for jn in follow_joints:  # one follower joint per Link hat joint the band follows, at that joint's rest pose
        Mj = surf["joint_head"][jn]
        rx, ry, rz = rot_xyz_zyx(Mj[:3, :3])
        joints.append(dict(name=f"follow_{jn}", parent=0, scale=(1, 1, 1), rot=(rx, ry, rz), trans=tuple(Mj[:3, 3]),
                           link_joint=jn, rest_head_frame=[float(x) for x in Mj[:3, :4].reshape(-1)]))
    assert [j["name"] for j in joints[:5]] == ["root", "ear_L0", "ear_L1", "ear_R0", "ear_R1"]
    return (dict(pos=pos, nrm=nrm, clr=clr, uv=uv, skin=skin, tri_group=tgrp, band_idx=band_idx, band_alpha=band_alpha, eye_report=eye_report),
            np.array(tris, int), joints)
