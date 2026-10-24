"""Small J3D (BMD/BDL) geometry reader used by the Bunny Hood generator and its reference renderer.

Reads INF1/VTX1/EVP1/DRW1/JNT1/SHP1 into plain Python/numpy structures.  No game data lives here.
"""
from __future__ import annotations
import struct
import numpy as np


def be(fmt, data, off):
    return struct.unpack_from(">" + fmt, data, off)


def chunks_of(data: bytes) -> dict:
    n = be("I", data, 12)[0]
    off = 0x20
    out = {}
    for _ in range(n):
        if off >= len(data):
            break
        magic = data[off:off + 4].decode("ascii")
        size = be("I", data, off + 4)[0]
        out[magic] = (off, data[off:off + size])
        off += size
    return out


def read_names(c: bytes, off: int):
    if not off:
        return []
    n = be("H", c, off)[0]
    names = []
    for i in range(n):
        so = be("H", c, off + 4 + i * 4 + 2)[0]
        e = c.index(b"\0", off + so)
        names.append(c[off + so:e].decode("shift_jis"))
    return names


class Model:
    pass


COLOR_STRIDE = {0: 2, 1: 3, 2: 4, 3: 2, 4: 3, 5: 4}


def parse(data: bytes) -> Model:
    ch = {k: v[1] for k, v in chunks_of(data).items()}
    m = Model()
    # ---- VTX1
    c = ch["VTX1"]
    fmt_off = be("I", c, 8)[0]
    arr_offs = list(be("13I", c, 0x0C))
    fmts = {}
    o = fmt_off
    while True:
        attr, cnt, ctype, frac = be("IIIB", c, o)
        if attr == 0xFF:
            break
        fmts[attr] = (cnt, ctype, frac)
        o += 16
    arrays = {}
    idx_of = {9: 0, 10: 1, 11: 3, 12: 4}
    for t in range(8):
        idx_of[13 + t] = 5 + t
    bounds = sorted(set(x for x in arr_offs if x) | {len(c)})
    for attr, (cnt, ctype, frac) in fmts.items():
        if attr not in idx_of:
            continue
        start = arr_offs[idx_of[attr]]
        if not start:
            continue
        end = min(x for x in bounds if x > start)
        if attr in (11, 12):
            stride = COLOR_STRIDE[ctype]
            n = (end - start) // stride
            raw = np.frombuffer(c[start:start + n * stride], dtype=np.uint8).reshape(n, stride)
            if ctype == 5:
                arr = raw.astype(np.float32) / 255
            elif ctype in (1, 2):
                arr = np.concatenate([raw[:, :3].astype(np.float32) / 255, np.ones((n, 1), np.float32)], 1)
            else:
                arr = np.ones((n, 4), np.float32)
            arrays[attr] = arr
            continue
        comps = {9: 2 + cnt, 10: 3}.get(attr, 1 + cnt)
        dt, bs = {0: (">u1", 1), 1: (">i1", 1), 2: (">u2", 2), 3: (">i2", 2), 4: (">f4", 4)}[ctype]
        stride = comps * bs
        n = (end - start) // stride
        arr = np.frombuffer(c[start:start + n * stride], dtype=dt).reshape(n, comps).astype(np.float32)
        if ctype != 4:
            arr = arr / float(1 << frac)
        arrays[attr] = arr
    m.arrays = arrays
    m.vtx_fmts = fmts
    # ---- JNT1
    c = ch["JNT1"]
    n = be("H", c, 8)[0]
    jo, ro, no = be("III", c, 0x0C)
    names = read_names(c, no)
    joints = []
    for i in range(n):
        o = jo + 0x40 * i
        mt = be("H", c, o)[0]
        sc = be("3f", c, o + 4)
        rot = be("3h", c, o + 0x10)
        tr = be("3f", c, o + 0x18)
        rad = be("f", c, o + 0x24)[0]
        bmin = be("3f", c, o + 0x28)
        bmax = be("3f", c, o + 0x34)
        joints.append(dict(mtype=mt, scale=sc, rot=[r * np.pi / 32768 for r in rot], trans=tr, radius=rad,
                           bmin=bmin, bmax=bmax, name=names[i] if i < len(names) else f"j{i}", parent=-1))
    m.joints = joints
    # ---- INF1
    c = ch["INF1"]
    cnt, _vc, ho = be("III", c, 0x0C)
    o = ho
    m.scene = []  # (joint, material_node_index, shape_index)
    last_mat = None
    parent_stack = [-1]
    pending = -1
    while True:
        t, i = be("HH", c, o)
        o += 4
        if t == 0:
            break
        if t == 1:
            parent_stack.append(pending)
        elif t == 2:
            parent_stack.pop()
        elif t == 0x10:
            joints[i]["parent"] = parent_stack[-1]
            pending = i
        elif t == 0x11:
            last_mat = i
        elif t == 0x12:
            m.scene.append((pending, last_mat, i))
    # ---- DRW1
    c = ch["DRW1"]
    n, wo, io = be("HxxII", c, 8)
    m.drw = [(c[wo + i], be("H", c, io + 2 * i)[0]) for i in range(n)]
    # ---- EVP1
    c = ch["EVP1"]
    n = be("H", c, 8)[0]
    m.evp = []
    m.inv = []
    if n:
        co, io, wo, mo = be("IIII", c, 0x0C)
        ip = 0
        for i in range(n):
            k = c[co + i]
            js = [be("H", c, io + 2 * (ip + j))[0] for j in range(k)]
            ws = [be("f", c, wo + 4 * (ip + j))[0] for j in range(k)]
            ip += k
            m.evp.append((js, ws))
        nm = (len(c) - mo) // 48
        m.inv = [np.vstack([np.array(be("12f", c, mo + 48 * i)).reshape(3, 4), [0, 0, 0, 1]]) for i in range(nm)]
    # ---- SHP1
    c = ch["SHP1"]
    n = be("H", c, 8)[0]
    so, ro, no, ao, mto, po, mdo, pko = be("8I", c, 0x0C)
    m.shapes = []
    for i in range(n):
        s = so + 0x28 * i
        mt, ngroups, aoff, fmi, fpi = be("BxHHHH", c, s)
        a = ao + aoff
        desc = []
        while True:
            at, ty = be("II", c, a)
            a += 8
            if at == 0xFF:
                break
            desc.append((at, ty))
        groups = []
        for g in range(ngroups):
            _u, cnt, first = be("HHI", c, mdo + 8 * (fmi + g))
            tbl = [be("H", c, mto + 2 * (first + k))[0] for k in range(cnt)]
            size, poff = be("II", c, pko + 8 * (fpi + g))
            dl = c[po + poff:po + poff + size]
            groups.append(dict(drw_table=tbl, prims=parse_dl(dl, desc)))
        m.shapes.append(dict(mtype=mt, desc=desc, groups=groups, radius=be("f", c, s + 0x0C)[0],
                             bmin=be("3f", c, s + 0x10), bmax=be("3f", c, s + 0x1C)))
    m.chunks = ch
    return m


def parse_dl(dl: bytes, desc):
    prims = []
    o = 0
    while o < len(dl):
        cmd = dl[o]
        if cmd == 0:
            o += 1
            continue
        if cmd not in (0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0):
            break
        cnt = be("H", dl, o + 1)[0]
        o += 3
        verts = []
        for _ in range(cnt):
            v = {}
            for at, ty in desc:
                if ty == 0:
                    continue
                if ty in (1, 2):
                    v[at] = dl[o]
                    o += 1
                else:
                    v[at] = be("H", dl, o)[0]
                    o += 2
            verts.append(v)
        prims.append((cmd, verts))
    return prims


def prim_tris(cmd, verts):
    n = len(verts)
    if cmd == 0x90:
        return [(verts[i], verts[i + 1], verts[i + 2]) for i in range(0, n - 2, 3)]
    if cmd == 0x98:
        return [((verts[i], verts[i + 1], verts[i + 2]) if i % 2 == 0 else (verts[i + 1], verts[i], verts[i + 2]))
                for i in range(n - 2)]
    if cmd == 0xA0:
        return [(verts[0], verts[i], verts[i + 1]) for i in range(1, n - 1)]
    if cmd == 0x80:
        out = []
        for i in range(0, n - 3, 4):
            a, b, c, d = verts[i:i + 4]
            out += [(a, b, c), (a, c, d)]
        return out
    return []


def joint_local(j):
    sx, sy, sz = j["scale"]
    rx, ry, rz = j["rot"]
    cx, sx_ = np.cos(rx), np.sin(rx)
    cy, sy_ = np.cos(ry), np.sin(ry)
    cz, sz_ = np.cos(rz), np.sin(rz)
    Rx = np.array([[1, 0, 0], [0, cx, -sx_], [0, sx_, cx]])
    Ry = np.array([[cy, 0, sy_], [0, 1, 0], [-sy_, 0, cy]])
    Rz = np.array([[cz, -sz_, 0], [sz_, cz, 0], [0, 0, 1]])
    M = np.eye(4)
    M[:3, :3] = Rz @ Ry @ Rx @ np.diag([sx, sy, sz])
    M[:3, 3] = j["trans"]
    return M


def joint_world(m: Model):
    mats = [joint_local(j) for j in m.joints]
    world = [None] * len(mats)

    def get(i):
        if world[i] is None:
            p = m.joints[i]["parent"]
            world[i] = mats[i] if p < 0 else get(p) @ mats[i]
        return world[i]

    return [get(i) for i in range(len(mats))]


def triangles(m: Model, shape_index: int, world=None):
    """Triangles of one shape in model space.  Returns dict: pos (T,3,3), nrm (T,3,3), uv (T,3,2), clr (T,3,4),
    skin: list (3T) of [(joint, weight)], rest-pose positions already skinned."""
    world = world if world is not None else joint_world(m)
    sh = m.shapes[shape_index]
    joint = [s[0] for s in m.scene if s[2] == shape_index][0]
    pos, nrm, uv, clr, skin = [], [], [], [], []
    A = m.arrays
    slots = {}
    for g in sh["groups"]:
        for k, d in enumerate(g["drw_table"]):
            if d != 0xFFFF:
                slots[k] = d
        tbl = slots
        for cmd, verts in g["prims"]:
            for tri in prim_tris(cmd, verts):
                for v in tri:
                    p = np.append(A[9][v[9]][:3], 1.0)
                    w = [(joint, 1.0)]
                    M = world[joint]
                    skinned = None
                    if 0 in v and tbl:
                        weighted, idx = m.drw[tbl[v[0] // 3]]
                        if weighted:
                            js, ws = m.evp[idx]
                            acc = np.zeros(3)
                            for jj, ww in zip(js, ws):
                                acc += ww * (world[jj] @ m.inv[jj] @ p)[:3]
                            skinned = acc
                            w = list(zip(js, ws))
                        else:
                            M = world[idx]
                            w = [(idx, 1.0)]
                    pos.append(skinned if skinned is not None else (M @ p)[:3])
                    n = A[10][v[10]][:3] if 10 in v else np.array([0, 1, 0.0])
                    nrm.append(n if skinned is not None else (M[:3, :3] @ n))
                    uv.append(A[13][v[13]][:2] if 13 in v else np.zeros(2))
                    clr.append(A[11][v[11]] if 11 in v else np.ones(4))
                    skin.append(w)
    t = len(pos) // 3
    return dict(pos=np.array(pos).reshape(t, 3, 3), nrm=np.array(nrm).reshape(t, 3, 3),
                uv=np.array(uv).reshape(t, 3, 2), clr=np.array(clr).reshape(t, 3, 4), skin=skin)
