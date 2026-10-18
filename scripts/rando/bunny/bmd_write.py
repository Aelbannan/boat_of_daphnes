"""Write a J3D2bmd3 model: INF1, VTX1, EVP1, DRW1, JNT1, SHP1 here; MAT3 and TEX1 chunks come in as bytes.

The model is one multi-matrix shape (skinned), triangles only, attributes PNMTXIDX(u8) POS NRM CLR0 TEX0 (index16).
Skinning: a vertex has up to a few (joint, weight) pairs; every distinct set becomes a DRW1/EVP1 entry and the
triangles are split into packets of at most 10 distinct entries (the GX position matrix limit).
"""
from __future__ import annotations
import struct
import numpy as np


def pad(b: bytearray, n: int, fill=0):
    while len(b) % n:
        b.append(fill)


def name_table(names):
    out = bytearray(struct.pack(">HH", len(names), 0xFFFF))
    off = 4 + 4 * len(names)
    strs = bytearray()
    for nm in names:
        h = 0
        for ch in nm:
            h = (h * 3 + ord(ch)) & 0xFFFF
        out += struct.pack(">HH", h, off + len(strs))
        strs += nm.encode("shift_jis") + b"\0"
    return bytes(out + strs)


def chunk(magic: bytes, body: bytes) -> bytes:
    b = bytearray(magic + b"\0\0\0\0" + body)
    pad(b, 32, 0)
    b[4:8] = struct.pack(">I", len(b))
    return bytes(b)


def build_vtx1(pos, nrm, clr, uv):
    fmts = [(9, 1, 4, 0), (10, 0, 4, 0), (11, 1, 5, 0), (13, 1, 4, 0)]
    b = bytearray(0x40)
    for a, c, t, f in fmts:
        b += struct.pack(">IIIBBBB", a, c, t, f, 0xFF, 0xFF, 0xFF)
    b += struct.pack(">IIIBBBB", 0xFF, 1, 0, 0, 0xFF, 0xFF, 0xFF)
    pad(b, 32)
    offs = {}
    for key, arr, fmt in (("pos", pos, ">f"), ("nrm", nrm, ">f"), ("uv", uv, ">f")):
        offs[key] = len(b)
        b += np.asarray(arr, ">f4").tobytes()
        pad(b, 32)
    offs["clr"] = len(b)
    b += np.clip(np.round(np.asarray(clr) * 255), 0, 255).astype(np.uint8).tobytes()
    pad(b, 32)
    table = [0] * 13
    table[0], table[1], table[3], table[5] = offs["pos"], offs["nrm"], offs["clr"], offs["uv"]
    b[0x08:0x0C] = struct.pack(">I", 0x40)
    b[0x0C:0x40] = struct.pack(">13I", *table)
    # offsets are chunk-relative (the chunk header is 8 bytes of the 0x08.. layout already counted)
    return b


def build_model(joints, verts, tris, mat3: bytes, tex1: bytes, mdl3: bytes = None, group_order=None):
    """joints: list of dict(name, parent, scale(3), rot_rad(3), trans(3)).
    verts: dict pos (N,3), nrm (N,3), clr (N,4 0..1), uv (N,2), skin list of [(joint, weight), ...].
    tris: (T,3) vertex indices.  Returns the file bytes."""
    nj = len(joints)
    world = []
    from j3d_geom import joint_local
    loc = [joint_local(dict(scale=j["scale"], rot=j["rot"], trans=j["trans"])) for j in joints]
    for i, j in enumerate(joints):
        world.append(loc[i] if j["parent"] < 0 else world[j["parent"]] @ loc[i])
    inv = [np.linalg.inv(w) for w in world]

    # ---- distinct skin entries
    def key(s):
        s = sorted((int(a), round(float(b), 4)) for a, b in s if b > 1e-4)
        tot = sum(b for _, b in s)
        s = [(a, b / tot) for a, b in s]
        return tuple(s)

    skin_keys = [key(s) for s in verts["skin"]]
    entries = []  # DRW1 entries: ('rigid', joint) | ('weighted', evp_index)
    evp = []
    entry_of = {}
    for k in skin_keys:
        if k in entry_of:
            continue
        if len(k) == 1:
            entry_of[k] = len(entries)
            entries.append((0, k[0][0]))
        else:
            entry_of[k] = len(entries)
            entries.append((1, len(evp)))
            evp.append(k)
    vtx_entry = [entry_of[k] for k in skin_keys]
    # a rigid vertex of a non-root joint is stored in that joint's local space (J3D rigid matrices carry no
    # inverse bind); weighted vertices stay in model space (the envelope multiplies by the inverse bind)
    verts = dict(verts)
    pos_a = np.array(verts["pos"], float)
    nrm_a = np.array(verts["nrm"], float)
    for i, k in enumerate(skin_keys):
        if len(k) == 1 and k[0][0] != 0:
            j = k[0][0]
            pos_a[i] = (inv[j] @ np.append(pos_a[i], 1.0))[:3]
            nrm_a[i] = inv[j][:3, :3] @ nrm_a[i]
    verts["pos"], verts["nrm"] = pos_a, nrm_a

    # ---- split triangles into packets by distinct entries (<=10), one shape per material group
    tgrp = verts.get("tri_group") or ["all"] * len(tris)
    order = group_order or sorted(set(tgrp), key=lambda g: tgrp.index(g))
    shape_packets = []
    for g in order:
        packets = []
        cur_set, cur_tris = [], []
        for t, tg in zip(tris, tgrp):
            if tg != g:
                continue
            need = {vtx_entry[v] for v in t}
            if len(set(cur_set) | need) > 10:
                packets.append((cur_set, cur_tris))
                cur_set, cur_tris = [], []
            for e in sorted(need):
                if e not in cur_set:
                    cur_set.append(e)
            cur_tris.append(t)
        if cur_tris:
            packets.append((cur_set, cur_tris))
        shape_packets.append(packets)

    # ---- VTX1
    vtx = build_vtx1(verts["pos"], verts["nrm"], verts["clr"], verts["uv"])
    vtx1 = chunk(b"VTX1", bytes(vtx[8:]))
    n_verts = len(verts["pos"])

    # ---- EVP1
    if evp:
        counts = bytearray(len(k) for k in evp)
        idx = bytearray()
        wts = bytearray()
        for k in evp:
            for a, bw in k:
                idx += struct.pack(">H", a)
                wts += struct.pack(">f", bw)
        body = bytearray(struct.pack(">HH", len(evp), 0xFFFF) + b"\0" * 16)
        o_counts = 8 + 4 + 16
        body += counts
        pad(body, 2)
        o_idx = len(body) + 8
        body += idx
        pad(body, 4)
        o_w = len(body) + 8
        body += wts
        pad(body, 4)
        o_m = len(body) + 8
        for i in range(nj):
            body += struct.pack(">12f", *inv[i][:3, :4].reshape(-1))
        body[4:20] = struct.pack(">4I", o_counts, o_idx, o_w, o_m)
    else:
        body = bytearray(struct.pack(">HH", 0, 0xFFFF) + b"\0" * 16)
    evp1 = chunk(b"EVP1", bytes(body[0:]))

    # ---- DRW1
    flags = bytes(e[0] for e in entries)
    data = b"".join(struct.pack(">H", e[1]) for e in entries)
    hdr = 8 + 4 + 8
    o_flags = hdr
    o_data = o_flags + len(flags)
    if o_data % 2:
        flags += b"\0"
        o_data += 1
    body = struct.pack(">HHII", len(entries), 0xFFFF, o_flags, o_data) + flags + data
    drw1 = chunk(b"DRW1", body)

    # ---- JNT1
    jd = bytearray()
    for j in joints:
        r = [int(round(a / np.pi * 32768)) & 0xFFFF for a in j["rot"]]
        sc = j["scale"]
        pts = np.array(j.get("bound_pts", [(0, 0, 0)]))
        jd += struct.pack(">HBB3f3H H3f f3f3f", 0, 0xFF if False else 0, 0xFF, *sc, *r, 0xFFFF, *j["trans"],
                          float(np.linalg.norm(pts, axis=1).max()), *pts.min(0), *pts.max(0))
    remap = b"".join(struct.pack(">H", i) for i in range(nj))
    names = name_table([j["name"] for j in joints])
    o_j = 0x18
    o_r = o_j + len(jd)
    o_n = o_r + len(remap)
    if o_n % 4:
        remap += b"\0" * (4 - o_n % 4)
        o_n = o_r + len(remap)
    body = struct.pack(">HHIII", nj, 0xFFFF, o_j, o_r, o_n) + bytes(jd) + remap + names
    jnt1 = chunk(b"JNT1", body)

    # ---- SHP1
    pos = np.asarray(verts["pos"])
    attrs = struct.pack(">IIIIIIIIII", 0, 1, 9, 3, 10, 3, 11, 3, 13, 3) + struct.pack(">II", 0xFF, 0)
    matrix_table = bytearray()
    dls = bytearray()
    mdata = bytearray()
    ploc = bytearray()
    shape_info = []  # (n_groups, first mdata idx, first ploc idx, bbox pts)
    n_mdata = 0
    for packets in shape_packets:
        first_m = n_mdata
        pts_used = set()
        for entry_list, ptris in packets:
            first = len(matrix_table) // 2
            for e in entry_list:
                matrix_table += struct.pack(">H", e)
            slot = {e: i for i, e in enumerate(entry_list)}
            dl = bytearray()
            flat = [v for t in ptris for v in t]
            pts_used.update(flat)
            for s0 in range(0, len(flat), 65535 - 65535 % 3):
                part = flat[s0:s0 + 65535 - 65535 % 3]
                dl += struct.pack(">BH", 0x90, len(part))
                for v in part:
                    dl += struct.pack(">BHHHH", slot[vtx_entry[v]] * 3, v, v, v, v)
            pad(dl, 32)
            mdata += struct.pack(">HHI", 0, len(entry_list), first)
            ploc += struct.pack(">II", len(dl), len(dls))
            dls += dl
            n_mdata += 1
        shape_info.append((len(packets), first_m, first_m, sorted(pts_used)))
    n_groups = n_mdata
    n_shapes = len(shape_packets)
    hdr_len = 0x2C
    o_shape = hdr_len
    o_remap = o_shape + 0x28 * n_shapes
    remap_bytes = b"".join(struct.pack(">H", i) for i in range(n_shapes))
    while len(remap_bytes) % 4:
        remap_bytes += b"\xFF"
    o_attr = o_remap + len(remap_bytes)
    pad_attr = bytearray(attrs)
    while (o_attr + len(pad_attr)) % 4:
        pad_attr.append(0)
    o_mtbl = o_attr + len(pad_attr)
    o_prim = o_mtbl + len(matrix_table)
    gap = (-o_prim) % 32
    o_prim += gap
    o_mdata = o_prim + len(dls)
    o_ploc = o_mdata + len(mdata)
    shapes_bytes = b""
    for ng, fm, fp, used in shape_info:
        sp = pos[used]
        rad = float(np.linalg.norm(sp, axis=1).max())
        shapes_bytes += struct.pack(">BBHHHHH f3f3f", 3, 0xFF, ng, 0, fm, fp, 0xFFFF, rad, *sp.min(0), *sp.max(0))
    body = bytearray(struct.pack(">HHIIIIIIII", n_shapes, 0xFFFF, o_shape, o_remap, 0, o_attr, o_mtbl, o_prim, o_mdata,
                                 o_ploc))
    body += shapes_bytes + remap_bytes + pad_attr + matrix_table + b"\0" * gap + dls + mdata + ploc
    shp1 = chunk(b"SHP1", bytes(body))

    # ---- INF1
    hier = bytearray()

    def node(t, i):
        hier.extend(struct.pack(">HH", t, i))

    kids = {}
    for i, j in enumerate(joints):
        kids.setdefault(j["parent"], []).append(i)

    def walk(i):
        node(0x10, i)
        if i == 0:
            pass
        if i in kids or i == 0:
            node(1, 0)
            if i == 0:
                for si in range(n_shapes):
                    node(0x11, si)
                    node(1, 0)
                    node(0x12, si)
                    node(2, 0)
            for k in kids.get(i, []):
                walk(k)
            node(2, 0)

    for r in kids[-1]:
        walk(r)
    node(0, 0)
    body = struct.pack(">HHIII", 0, 0xFFFF, n_groups, n_verts, 0x18) + bytes(hier)
    inf1 = chunk(b"INF1", body)

    chunks = [inf1, vtx1, evp1, drw1, jnt1, shp1, mat3] + ([mdl3] if mdl3 else []) + [tex1]
    out = bytearray(b"J3D2" + (b"bdl4" if mdl3 else b"bmd3") + b"\0\0\0\0" + struct.pack(">I", len(chunks)) + b"SVR3" + b"\xFF" * 12)
    for c in chunks:
        out += c
    out[8:12] = struct.pack(">I", len(out))
    return bytes(out)
