# Dev helper: extract the game's program from the user's own disc into orig/ (gitignored):
#   orig/main.dol
#   orig/rel/<name>.rel   every code module, decompressed: the loose ones in rels/ (Yaz0) and the
#                         ones packed in RELS.arc (a RARC archive of Yaz0 files)
#   orig/maps/<name>.map  the linker maps the disc ships (framework.map: main.dol's)
#   orig/framework.str    the module-name table
# The shipped program reads the disc itself.
#   python tools/extract.py <disc image> [out]
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from discimage import open_disc


def yaz0(data):
    """Nintendo's Yaz0: a 16-byte header (magic, size), then groups of a flag byte and 8 items -
    a literal byte (bit set) or a back reference (2 or 3 bytes)."""
    if data[:4] != b'Yaz0':
        return data
    size = struct.unpack('>I', data[4:8])[0]
    out = bytearray(size)
    src, dst = 16, 0
    while dst < size:
        flags = data[src]; src += 1
        for bit in range(7, -1, -1):
            if dst >= size:
                break
            if flags >> bit & 1:
                out[dst] = data[src]; src += 1; dst += 1
            else:
                b1, b2 = data[src], data[src + 1]; src += 2
                dist = ((b1 & 0x0F) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0:
                    n = data[src] + 0x12; src += 1
                else:
                    n += 2
                for _ in range(n):
                    out[dst] = out[dst - dist]; dst += 1
    return bytes(out)


def rarc(data):
    """A RARC archive's files: (name, bytes) for every file entry (directories walked flat)."""
    assert data[:4] == b'RARC'
    data_off = struct.unpack('>I', data[0x0C:0x10])[0] + 0x20
    info = 0x20
    n_nodes, nodes_off, n_ents, ents_off, _, strs_off = struct.unpack('>IIIIII', data[info:info + 24])
    nodes_off += info; ents_off += info; strs_off += info
    name = lambda o: data[strs_off + o:data.index(b'\0', strs_off + o)].decode('latin1')
    out = []
    for i in range(n_ents):
        e = ents_off + i * 20
        _id, _hash, typ, name_off, off, size = struct.unpack('>HHHHII', data[e:e + 16])
        if typ & 0x0200:  # directory
            continue
        nm = name(name_off)
        out.append((nm, data[data_off + off:data_off + off + size]))
    return out


def main():
    iso = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else 'orig'
    d = open_disc(iso)
    h = d.read(0, 0x440)
    if h[:6] not in (b'GZLE01', b'GZLE99'):  # (GZLE99: a disc wwrando wrote, the randomizer's)
        sys.exit(f'not The Wind Waker (USA): {h[:6]!r}')
    dol_off, fst_off, fst_size = struct.unpack('>III', h[0x420:0x42C])
    fst = d.read(fst_off, fst_size)
    n = struct.unpack('>I', fst[8:12])[0]
    strtab = fst[n * 12:]
    nm = lambda o: strtab[o:strtab.index(b'\0', o)].decode('latin1')
    files = []

    def walk(i, end, path):
        while i < end:
            a, off, ln = struct.unpack('>III', fst[i * 12:i * 12 + 12])
            name = nm(a & 0xFFFFFF)
            if a >> 24:
                walk(i + 1, ln, path + name + '/'); i = ln
            else:
                files.append((path + name, off, ln)); i += 1

    walk(1, n, '')
    for sub in ('rel', 'maps'):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    hdr = d.read(dol_off, 0x100)
    offs = struct.unpack('>18I', hdr[0:72]); sizes = struct.unpack('>18I', hdr[0x90:0xD8])
    dol_size = max(o + s for o, s in zip(offs, sizes) if s)
    open(os.path.join(out, 'main.dol'), 'wb').write(d.read(dol_off, dol_size))
    rels = maps = 0
    for path, off, ln in files:
        low = path.lower()
        base = os.path.basename(path)
        if low.startswith('rels/') and low.endswith('.rel'):
            open(os.path.join(out, 'rel', base), 'wb').write(yaz0(d.read(off, ln)))
            rels += 1
        elif low == 'rels.arc':
            for name, data in rarc(d.read(off, ln)):
                if name.lower().endswith('.rel'):
                    open(os.path.join(out, 'rel', name), 'wb').write(yaz0(data))
                    rels += 1
        elif low.startswith('maps/') and low.endswith('.map'):
            open(os.path.join(out, 'maps', base), 'wb').write(d.read(off, ln))
            maps += 1
        elif low == 'framework.str':
            open(os.path.join(out, 'framework.str'), 'wb').write(d.read(off, ln))
    print(f'main.dol {dol_size} bytes, {rels} modules, {maps} maps -> {out}/')


if __name__ == '__main__':
    main()
