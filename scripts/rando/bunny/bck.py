"""Minimal BCK (J3D bck1 / ANK1) reader: sample a joint animation at a frame (developer tool for the fit)."""
from __future__ import annotations
import struct
import numpy as np
import j3d_geom as G


def parse(data: bytes):
    c = G.chunks_of(data)["ANK1"][1]
    loop, rot_frac = c[8], c[9]
    dur, n, ns, nr, nt = struct.unpack_from(">5H", c, 0x0A)
    ao, so, ro, to = struct.unpack_from(">4I", c, 0x14)
    scale = np.frombuffer(c[so:so + 4 * ns], ">f4")
    rot = np.frombuffer(c[ro:ro + 2 * nr], ">i2").astype(np.float32)
    trans = np.frombuffer(c[to:to + 4 * nt], ">f4")
    joints = []
    for j in range(n):
        o = ao + 0x36 * j
        axes = []
        for ax in range(3):
            tr = []
            for k in range(3):  # scale, rotation, translation
                cnt, idx, tang = struct.unpack_from(">3H", c, o + 6 * (ax * 3 + k))
                tr.append((cnt, idx, tang))
            axes.append(tr)
        joints.append(axes)
    return dict(dur=dur, rot_frac=rot_frac, scale=scale, rot=rot, trans=trans, joints=joints)


def _track(tbl, cnt, idx, tang, frame, is_rot):
    if cnt == 1:
        return float(tbl[idx])
    stride = 3 if tang == 0 else 4
    keys = [(tbl[idx + stride * i], tbl[idx + stride * i + 1], tbl[idx + stride * i + 2],
             tbl[idx + stride * i + (2 if tang == 0 else 3)]) for i in range(cnt)]
    if frame <= keys[0][0]:
        return float(keys[0][1])
    for a, b in zip(keys, keys[1:]):
        if frame <= b[0]:
            t = (frame - a[0]) / max(b[0] - a[0], 1e-6)
            h = b[0] - a[0]
            t2, t3 = t * t, t * t * t
            # cubic hermite with the out tangent of a and in tangent of b
            return float((2 * t3 - 3 * t2 + 1) * a[1] + (t3 - 2 * t2 + t) * a[3] * h + (-2 * t3 + 3 * t2) * b[1] +
                         (t3 - t2) * b[2] * h)
    return float(keys[-1][1])


def sample(anim, frame: float):
    """-> list per joint of (scale (3), rotation radians (3), translation (3))."""
    out = []
    k = np.pi / 32768.0 * (2 ** anim["rot_frac"])
    for axes in anim["joints"]:
        s, r, t = [], [], []
        for (sc, ro, tr) in axes:
            s.append(_track(anim["scale"], *sc, frame, False))
            r.append(_track(anim["rot"], *ro, frame, True) * k)
            t.append(_track(anim["trans"], *tr, frame, False))
        out.append((s, r, t))
    return out
