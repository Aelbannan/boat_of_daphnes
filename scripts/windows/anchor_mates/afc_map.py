"""afc_map.py RUN_LOG JAIINIT_AAF VOICE_0_AW: which voice-bank wave each voice id of a sweep run plays."""
import re, sys, struct, collections
if len(sys.argv) != 4:
    sys.exit(__doc__)
d = open(sys.argv[2], 'rb').read()
aw = open(sys.argv[3], 'rb').read()
i = d.find(b'voice_0.aw'); base = d.rfind(b'WSYS', 0, i)
w = lambda o: struct.unpack('>I', d[base + o:base + o + 4])[0]
winf = w(0x10)
first = {}
waves = []
for gi in range(w(winf + 4)):
    g = w(winf + 8 + 4 * gi)
    if d[base + g:base + g + 0x70].split(b'\0')[0] != b'voice_0.aw': continue
    for k in range(w(g + 0x70)):
        o = w(g + 0x74 + 4 * k)
        off, size = struct.unpack('>II', d[base + o + 8:base + o + 16])
        waves.append((off, size))
        first.setdefault(aw[off:off + 9].hex(), []).append(k)
cur = None
res = collections.defaultdict(list)
for line in open(sys.argv[1], 'rb'):
    line = line.decode('latin1')
    m = re.search(r'sweep: voice id (\d+)', line)
    if m: cur = int(m.group(1)); continue
    m = re.search(r'\[dsp-afc\] start base=0x([0-9A-F]+) type=(\d+) ratio=0x([0-9A-F]+) first=([0-9a-f]*)', line)
    if m and cur is not None:
        f = m.group(4)
        if f in first: res[cur].append((first[f], int(m.group(3), 16)))
for k in sorted(res): print(k, res[k][:4])
print('ids with a voice-bank wave:', len(res))
