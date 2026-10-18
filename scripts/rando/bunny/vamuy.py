"""Append one file to an existing RARC without disturbing any existing entry (indices, ids, bytes).

Used for the Bunny Hood's item-get / field model: the disc's Hero's Charm archive (res/Object/VamuY.arc) plus the hood's
item BDL as the first file of the 'BDLM' directory node's range (it takes the slot of the ROOT node's '..' link).  Every
other member keeps its index, id and bytes.
"""
from __future__ import annotations
import struct


def name_hash(name: str) -> int:
    h = 0
    for ch in name:
        h = (h * 3 + ord(ch)) & 0xFFFF
    return h


def append_file(arc: bytes, name: str, payload: bytes, flags: int = 0x11):
    """-> (new archive bytes, entry index of the new file).  `arc` must be an uncompressed RARC."""
    assert arc[:4] == b"RARC"
    total, info_off, data_len = struct.unpack_from(">III", arc, 4)[0], struct.unpack_from(">I", arc, 8)[0], struct.unpack_from(">I", arc, 0x0C)[0]
    data_off_rel = struct.unpack_from(">I", arc, 0x0C)[0]
    # header: 0x00 'RARC', 0x04 file size, 0x08 data header offset (0x20), 0x0C data offset (from the data header),
    #         0x10 data length, 0x14 mram length, 0x18 aram length
    hdr_len, data_start_rel, dlen, mram, aram = struct.unpack_from(">IIIII", arc, 0x08)
    info = hdr_len
    n_nodes, node_off, n_files, file_off, str_size, str_off = struct.unpack_from(">6I", arc, info)
    next_id, sync = struct.unpack_from(">HB", arc, info + 0x18)
    data_start = info + data_start_rel
    nodes = arc[info + node_off:info + file_off]
    entries = bytearray(arc[info + file_off:info + file_off + n_files * 20])
    strings = bytearray(arc[info + str_off:info + str_off + str_size])
    data = bytearray(arc[data_start:data_start + dlen])
    while len(data) % 32:
        data.append(0)
    new_data_off = len(data)
    data += payload
    soff = len(strings)
    strings += name.encode("ascii") + b"\0"
    # The game loads an archive's models through its directory nodes, and getFirstResource(type) walks only the file range of the
    # FIRST 'BDLM' node, files before its '.' and '..' links.  So the new file goes at the FRONT of that range, into the slot of
    # the ROOT node's '..' link (index 3 in this archive, which nothing looks up: the root has no parent), and the two nodes'
    # ranges move over it: ROOT loses its last entry, BDLM starts one earlier.  The disc's own members keep their indices,
    # ids and bytes, and the entry count does not change.  (Appending the file at the end, outside every range, gave a model
    # the game could not find; placing it after the '.' link broke the game's walk of the range.)
    def node(i):
        o = node_off + 16 * i
        return nodes[o - node_off:o - node_off + 4], struct.unpack_from(">H", nodes, o - node_off + 10)[0], struct.unpack_from(">I", nodes, o - node_off + 12)[0]
    assert node(0)[0] == b"ROOT"
    bdlm_i = next(i for i in range(n_nodes) if node(i)[0] == b"BDLM")
    _, root_count, root_first = node(0)
    _, bdlm_count, bdlm_first = node(bdlm_i)
    last = root_first + root_count - 1
    assert bdlm_first == last + 1, "the BDLM node does not follow the ROOT node's entries"
    link = entries[last * 20:last * 20 + 20]
    assert struct.unpack_from(">H", link, 0)[0] == 0xFFFF and link[4] & 2, "the ROOT node's last entry is not a directory link"
    entries[last * 20:last * 20 + 20] = struct.pack(">HHHHII4x", last, name_hash(name), flags << 8, soff, new_data_off, len(payload))
    # the archive keeps file ids equal to entry indices (its sync flag): the new file's id is its index
    assert all(struct.unpack_from(">H", entries, i * 20)[0] != last for i in range(n_files) if i != last)
    nodes = bytearray(nodes)
    struct.pack_into(">H", nodes, 10, root_count - 1)
    struct.pack_into(">H", nodes, 16 * bdlm_i + 10, bdlm_count + 1)
    struct.pack_into(">I", nodes, 16 * bdlm_i + 12, last)
    index = last
    # rebuild the info block
    pre = bytearray(arc[info:info + node_off])  # the 0x20 byte info header (node offset is 0x20)
    file_off_new = node_off + len(nodes)
    while file_off_new % 32:
        file_off_new += 1
    str_off_new = file_off_new + len(entries)
    while str_off_new % 32:
        str_off_new += 1
    str_pad = (-(str_off_new + len(strings))) % 32
    info_len = str_off_new + len(strings) + str_pad
    blk = bytearray(info_len)
    blk[0:node_off] = pre
    blk[node_off:node_off + len(nodes)] = nodes
    blk[file_off_new:file_off_new + len(entries)] = entries
    blk[str_off_new:str_off_new + len(strings)] = strings
    struct.pack_into(">6I", blk, 0, n_nodes, node_off, n_files, file_off_new, len(strings) + str_pad, str_off_new)
    struct.pack_into(">HB", blk, 0x18, next_id, sync)
    while len(data) % 32:
        data.append(0)
    out_total = info + info_len + len(data)
    head = bytearray(arc[:info])
    struct.pack_into(">IIIIIII", head, 4, out_total, info, info_len, len(data), len(data), 0, 0)
    return bytes(head) + bytes(blk) + bytes(data), index
