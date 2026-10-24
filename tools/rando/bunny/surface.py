"""WW Link's hat cap as a surface in the head_jnt local frame, with ray casts and point distances.

The hood's shell and headband are fitted to this surface at generation time (read from the player's own disc);
nothing of Link's model is stored anywhere.
"""
from __future__ import annotations
import io
import numpy as np
import j3d_geom as G

HEAD_JOINT = 15


def hat_cap(cl_bdl: bytes, material_names=("ear(3)",), wait_bck: bytes = None, droop=(-6.0, -28.0, -28.0)):
    """Triangles of the hat cap (the cap that rides on head_jnt, not the tail chain) in the head_jnt local frame.
    Returns dict(tri (T,3,3), nrm (T,3) outward-ish unit normals)."""
    from gclib.j3d import J3D
    j = J3D(io.BytesIO(cl_bdl))
    m = G.parse(cl_bdl)
    if wait_bck is not None:
        import restpose
        restpose.apply(m, wait_bck)  # standing idle, hat at its gravity rest
    else:
        for jn, dz in zip((26, 27, 28), droop):
            r = list(m.joints[jn]["rot"])
            r[2] += np.radians(dz)
            m.joints[jn]["rot"] = r
    world = G.joint_world(m)
    inv = np.linalg.inv(world[HEAD_JOINT])
    tris = []
    skins = []
    for jt, mt, s in m.scene:
        if j.mat3.mat_names[mt] not in material_names:
            continue
        t = G.triangles(m, s, world)
        dom = np.array([max(sk, key=lambda a: a[1])[0] for sk in t["skin"]]).reshape(-1, 3)
        keep = np.isin(dom, (HEAD_JOINT, 26, 27)).all(axis=1)
        p = t["pos"][keep]
        tris.append((p @ inv[:3, :3].T) + inv[:3, 3])
        sk = [t["skin"][3 * i:3 * i + 3] for i in np.nonzero(keep)[0]]
        skins.extend(sk)
    tri = np.concatenate(tris)
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ln = np.linalg.norm(n, axis=1, keepdims=True)
    ok = ln[:, 0] > 1e-9
    tri, n = tri[ok], n[ok] / ln[ok]
    skins = [skins[i] for i in np.nonzero(ok)[0]]
    joint_head = {jn: inv @ world[jn] for jn in (26, 27, 28)}
    return dict(tri=tri, nrm=n, skin=skins, joint_head=joint_head)


def raycast(surf, origin, direction):
    """Nearest hit of a ray with the surface: (t, normal facing along the ray, triangle index) or None (Moller-Trumbore)."""
    tri = surf["tri"]
    d = np.asarray(direction, float)
    d = d / np.linalg.norm(d)
    e1, e2 = tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]
    h = np.cross(d, e2)
    a = np.einsum("ij,ij->i", e1, h)
    ok = np.abs(a) > 1e-9
    f = np.where(ok, 1.0 / np.where(ok, a, 1), 0.0)
    s = origin - tri[:, 0]
    u = f * np.einsum("ij,ij->i", s, h)
    q = np.cross(s, e1)
    v = f * (q @ d)
    t = f * np.einsum("ij,ij->i", e2, q)
    hit = ok & (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1 + 1e-6) & (t > 1e-4)
    if not hit.any():
        return None
    idx = np.nonzero(hit)[0]
    k = idx[np.argmin(t[idx])]
    n = surf["nrm"][k].copy()
    if n @ d > 0:
        n = n  # already facing along the ray = outward from an inside origin
    else:
        n = -n
    return float(t[k]), n, int(k)


def point_tri_distance(points, tri):
    """Distance from each point (N,3) to the nearest triangle of tri (T,3,3): returns (N,) (Ericson's closest point)."""
    P = np.asarray(points, float)
    out = np.empty(len(P))
    a, b, c = tri[:, 0], tri[:, 1], tri[:, 2]
    ab, ac = b - a, c - a
    for i, p in enumerate(P):
        ap = p - a
        d1, d2 = np.einsum("ij,ij->i", ab, ap), np.einsum("ij,ij->i", ac, ap)
        bp = p - b
        d3, d4 = np.einsum("ij,ij->i", ab, bp), np.einsum("ij,ij->i", ac, bp)
        cp = p - c
        d5, d6 = np.einsum("ij,ij->i", ab, cp), np.einsum("ij,ij->i", ac, cp)
        vc = d1 * d4 - d3 * d2
        vb = d5 * d2 - d1 * d6
        va = d3 * d6 - d5 * d4
        den = va + vb + vc
        den = np.where(np.abs(den) < 1e-12, 1e-12, den)
        v, w = vb / den, vc / den
        q = a + ab * v[:, None] + ac * w[:, None]
        # edge/vertex regions: clamp the barycentrics, then re-project onto the edges
        inside = (v >= 0) & (w >= 0) & (v + w <= 1)
        best = np.linalg.norm(p - q, axis=1)
        best = np.where(inside, best, np.inf)
        for (s0, s1) in ((a, b), (b, c), (c, a)):
            e = s1 - s0
            tt = np.clip(np.einsum("ij,ij->i", p - s0, e) / np.maximum(np.einsum("ij,ij->i", e, e), 1e-12), 0, 1)
            best = np.minimum(best, np.linalg.norm(p - (s0 + e * tt[:, None]), axis=1))
        out[i] = best.min()
    return out
