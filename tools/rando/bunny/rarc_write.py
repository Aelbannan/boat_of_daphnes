"""Minimal uncompressed RARC writer: files = {"dir/name.ext": bytes}."""
from __future__ import annotations
import struct


def name_hash(name: str) -> int:
    h = 0
    for ch in name:
        h = (h * 3 + ord(ch)) & 0xFFFF
    return h


def write_rarc(files: dict, root_name: str = "root") -> bytes:
    dirs = ["."]  # index 0 = root
    by_dir = {}
    for path in sorted(files):
        parts = path.split("/")
        d = parts[0] if len(parts) > 1 else ""
        by_dir.setdefault(d, []).append((parts[-1], files[path]))
    dir_names = [d for d in by_dir if d]
    nodes = [("ROOT", root_name)] + [(d.upper()[:4].ljust(4), d) for d in dir_names]
    strings = bytearray()
    soff = {}

    def s(name):
        if name not in soff:
            soff[name] = len(strings)
            strings.extend(name.encode("shift_jis") + b"\0")
        return soff[name]

    s(".")
    s("..")
    entries = []  # per node list of tuples
    data = bytearray()
    node_entries = []
    for ni, (_t, nm) in enumerate(nodes):
        ents = []
        if ni == 0:
            for f, b in by_dir.get("", []):
                ents.append(("file", f, b))
            for di, d in enumerate(dir_names):
                ents.append(("dir", d, di + 1))
        else:
            for f, b in by_dir[nm]:
                ents.append(("file", f, b))
        ents.append(("dot", ".", ni))
        ents.append(("dot", "..", 0 if ni else 0xFFFFFFFF))
        node_entries.append(ents)
    for ni, (_t, nm) in enumerate(nodes):
        s(nm)
    file_entries = bytearray()
    first_idx = []
    idx = 0
    file_id = 0
    for ni, ents in enumerate(node_entries):
        first_idx.append(idx)
        for kind, nm, v in ents:
            if kind == "file":
                while len(data) % 32:
                    data.append(0)
                off = len(data)
                data.extend(v)
                file_entries += struct.pack(">HHHHII4x", file_id, name_hash(nm), 0x1100, s(nm), off, len(v))
                file_id += 1
            elif kind == "dir":
                file_entries += struct.pack(">HHHHII4x", 0xFFFF, name_hash(nm), 0x0200, s(nm), v, 0x10)
            else:
                file_entries += struct.pack(">HHHHII4x", 0xFFFF, name_hash(nm), 0x0200, s(nm), v & 0xFFFFFFFF, 0x10)
            idx += 1
    node_bytes = bytearray()
    for ni, (t, nm) in enumerate(nodes):
        node_bytes += struct.pack(">4sIHHI", t.encode(), s(nm), name_hash(nm), len(node_entries[ni]), first_idx[ni])
    while len(data) % 32:
        data.append(0)
    info_off = 0x20
    node_off = 0x20
    ent_off = node_off + len(node_bytes)
    while ent_off % 32:
        ent_off += 1
    str_off = ent_off + len(file_entries)
    str_size = len(strings)
    pad_str = (-(str_off + str_size)) % 32
    info_len = str_off + str_size + pad_str
    info = bytearray(info_len)
    struct.pack_into(">IIIIIIHBx", info, 0, len(nodes), node_off, len(node_entries and file_entries) // 20,
                     ent_off, str_size + pad_str, str_off, file_id, 0)
    info[node_off:node_off + len(node_bytes)] = node_bytes
    info[ent_off:ent_off + len(file_entries)] = file_entries
    info[str_off:str_off + str_size] = strings
    total = 0x20 + info_len + len(data)
    head = struct.pack(">4sIIIIIII", b"RARC", total, 0x20, info_len, len(data), len(data), 0, 0)
    return head + bytes(info) + bytes(data)
