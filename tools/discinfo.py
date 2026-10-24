# Dev helper: what a disc image is - game id, revision, the program's size, the files on it.
#   python tools/discinfo.py <disc image> [--files]
import struct
import sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
from discimage import open_disc

d = open_disc(sys.argv[1])
h = d.read(0, 0x440)
print('game id', h[:6].decode(), 'disc', h[6], 'revision', h[7], 'title', h[0x20:0x60].split(b'\0')[0].decode('latin1'))
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
hdr = d.read(dol_off, 0x100)
offs = struct.unpack('>18I', hdr[0:72]); sizes = struct.unpack('>18I', hdr[0x90:0xD8])
print('main.dol', max(o + s for o, s in zip(offs, sizes) if s), 'bytes;', len(files), 'files')
if '--files' in sys.argv:
    for p, off, ln in files:
        print(f'{ln:10d}  {p}')
