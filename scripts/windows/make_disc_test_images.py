#!/usr/bin/env python3
"""Makes compressed copies of a GameCube disc image to test the app's unpacker (runtime/host/src/disc_unpack.c) with.

    python scripts/windows/make_disc_test_images.py ISO OUT_DIR [--only NAME ...] [--unpacker bluewake_disc_unpack.exe]

With --unpacker, each image is unpacked by that program as soon as it is made, the result compared (SHA-1) with the ISO, and both
the image and the result deleted: a whole matrix of them would not fit on a disk.

Writes, for the ISO given, into OUT_DIR: Dolphin's WIA and RVZ in every compression the format has (none, purge, bzip2, LZMA,
LZMA2, Zstandard; chunk sizes 128 KiB, 2 MiB), RVZ with the discs' junk padding stored the way Dolphin does (the lagged
Fibonacci generator's seed instead of the bytes), .gcz and .ciso. The layouts follow Dolphin's docs/WiaAndRvz.md, CompressedBlob
and CISOBlob; there is no Dolphin here to make real ones, so these are the nearest a script can get. Then run the unpacker on each
(`bluewake_disc_unpack IN OUT`) and compare the SHA-1 of the result with the ISO's. Never commit the output (disc images).

Zstandard needs the `zstd` command line tool on the PATH.
"""
import argparse
import bz2
import hashlib
import lzma
import mmap
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

GC_SIZE = 1459978240
K, J = 521, 32  # the junk generator's lag and tap


# --- the junk generator (the discs' padding), with the seed recovered from data -------------------------------------

def swap32(x):
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) | (x >> 24)


class Lfg:
    def __init__(self):
        self.buf = [0] * K

    def forward(self):
        b = self.buf
        for i in range(J):
            b[i] ^= b[i + K - J]
        for i in range(J, K):
            b[i] ^= b[i - J]

    def backward(self, start=0, end=K):
        b = self.buf
        loop_end = max(J, start)
        for i in range(min(end, K), loop_end, -1):
            b[i - 1] ^= b[i - 1 - J]
        for i in range(min(end, J), start, -1):
            b[i - 1] ^= b[i - 1 + K - J]

    def initialize(self, check):
        b = self.buf
        for i in range(17, K):
            calc = ((b[i - 17] << 23) ^ (b[i - 16] >> 9) ^ b[i - 1]) & 0xFFFFFFFF
            if check:
                actual = (b[i] & 0xFF00FFFF) | ((b[i] << 2) & 0x00FC0000)
                if (calc & 0xFFFCFFFF) != actual:
                    return False
            b[i] = calc
        for i in range(K):
            x = b[i]
            b[i] = swap32((x & 0xFF00FFFF) | ((x >> 2) & 0x00FF0000))
        for _ in range(4):
            self.forward()
        return True

    def reinitialize(self):
        b = self.buf
        for _ in range(4):
            self.backward()
        for i in range(K):
            b[i] = swap32(b[i])
        for i in range(17):
            b[i] = (b[i] & 0xFF00FFFF) | ((b[i] << 2) & 0x00FC0000) | (((b[i + 16] ^ b[i + 15]) << 9) & 0x00030000)
        seed = b"".join(struct.pack(">I", b[i]) for i in range(17))  # Dolphin stores the u32s byteswapped, so the file has these big endian
        return seed if self.initialize(True) else None

    def get_byte_stream(self, position):
        """Bytes of the generator from `position` on, as a generator."""
        pos = position
        while True:
            word = self.buf[pos >> 2]  # the buffer's words are memory words: the generator's bytes are theirs, low byte first
            yield (word >> (8 * (pos & 3))) & 0xFF
            pos += 1
            if pos == K * 4:
                self.forward()
                pos = 0


def get_seed(data, data_offset):
    """Dolphin's LaggedFibonacciGenerator::GetSeed: (seed bytes, bytes of `data` the generator reproduces) or None."""
    skip = (-data_offset) % 4
    # the words are the little endian reading of the disc's bytes, as Dolphin reads them
    words = [struct.unpack_from("<I", data, skip + 4 * i)[0] for i in range((len(data) - skip) // 4)]
    word_offset = (data_offset + skip) // 4
    if len(words) < K:
        return None
    for x in words[:K]:
        s = swap32(x)
        if (s & 0x00C00000) != ((s >> 2) & 0x00C00000):
            return None
    mod, div = word_offset % K, word_offset // K
    lfg = Lfg()
    lfg.buf[mod:K] = words[0:K - mod]
    lfg.buf[0:mod] = words[K - mod:K]
    lfg.backward(0, mod)
    for _ in range(div):
        lfg.backward()
    seed = lfg.reinitialize()
    if seed is None:
        return None
    for _ in range(div):
        lfg.forward()
    stream = lfg.get_byte_stream(data_offset % (K * 4))
    n = 0
    for byte in data:
        if next(stream) != byte:
            break
        n += 1
    return seed, n


def junk_runs(iso, limit):
    """Runs of the disc's padding that the generator reproduces: [(start, length, seed)], found in the gaps between files."""
    fst_off, fst_size = struct.unpack_from(">II", iso, 0x424)
    fst = iso[fst_off:fst_off + fst_size]
    count = struct.unpack_from(">I", fst, 8)[0]
    files = sorted((struct.unpack_from(">I", fst, 12 * i + 4)[0], struct.unpack_from(">I", fst, 12 * i + 8)[0])
                   for i in range(1, count) if fst[12 * i] == 0)
    runs = []
    for (off, size), (next_off, _) in zip(files, files[1:]):
        a, b = off + size, next_off
        pos = a
        while pos < b and iso[pos] == 0:
            pos += 1
        while pos < b and len(runs) < limit:
            end = min(b, (pos // 0x8000 + 1) * 0x8000)
            found = get_seed(bytes(iso[pos:end]), pos % 0x8000) if end - pos >= (K + 1) * 4 else None
            if found and found[1] >= K * 4:
                runs.append((pos, found[1], found[0]))
            pos = end
        if len(runs) >= limit:
            break
    return runs


# --- compression ----------------------------------------------------------------------------------------------------------

NAMES = {"none": 0, "purge": 1, "bzip2": 2, "lzma": 3, "lzma2": 4, "zstd": 5}
LZMA_PROPS = bytes([(2 * 5 + 0) * 9 + 3]) + struct.pack("<I", 1 << 22)  # lc 3, lp 0, pb 2, dictionary 4 MiB
LZMA2_PROP = 24  # a 16 MiB dictionary


def purge(data):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        while i < n and data[i] == 0:
            i += 1
        if i >= n:
            break
        j = i
        while j < n and not (data[j] == 0 and data[j:j + 16] == bytes(16)):
            j += 1
        out += struct.pack(">II", i, j - i) + data[i:j]
        i = j
    return bytes(out) + hashlib.sha1(bytes(out)).digest()


def compress(kind, data):
    if kind == "none":
        return data
    if kind == "purge":
        return purge(data)
    if kind == "bzip2":
        return bz2.compress(data)
    if kind == "lzma":
        return lzma.compress(data, format=lzma.FORMAT_RAW,
                             filters=[{"id": lzma.FILTER_LZMA1, "preset": 1, "dict_size": 1 << 22, "lc": 3, "lp": 0, "pb": 2}])
    if kind == "lzma2":
        return lzma.compress(data, format=lzma.FORMAT_RAW, filters=[{"id": lzma.FILTER_LZMA2, "preset": 1, "dict_size": 1 << 24}])
    if kind == "zstd":
        return subprocess.run(["zstd", "-q", "-c", "-3"], input=data, stdout=subprocess.PIPE, check=True).stdout
    raise ValueError(kind)


# --- WIA and RVZ ---------------------------------------------------------------------------------------------------------

def write_wia(iso, path, rvz, kind, chunk, runs):
    size = len(iso)
    n_groups = (size + chunk - 1) // chunk
    groups = []  # (stored bytes, data_size field, packed_size)
    for g in range(n_groups):
        lo, hi = g * chunk, min((g + 1) * chunk, size)
        data = bytes(iso[lo:hi])
        if not any(data):
            groups.append((b"", 0, 0))
            continue
        packed_size = 0
        if rvz:
            mine = [(max(s, lo), min(s + n, hi), seed) for s, n, seed in runs if s < hi and s + n > lo]
            if mine:
                out = bytearray()
                cur = lo
                for s, e, seed in mine:
                    if s > cur:
                        out += struct.pack(">I", s - cur) + bytes(iso[cur:s])
                    out += struct.pack(">I", (e - s) | 0x80000000) + seed
                    cur = e
                if cur < hi:
                    out += struct.pack(">I", hi - cur) + bytes(iso[cur:hi])
                data = bytes(out)
                packed_size = len(data)
        if kind == "none":
            stored, field = data, len(data)
        else:
            stored = compress(kind, data)
            field = len(stored)
            if rvz:
                field |= 0x80000000
        groups.append((stored, field, packed_size))
    entry = 12 if rvz else 8
    raw_table = struct.pack(">QQII", 0x80, size - 0x80, 0, n_groups)
    # layout: header 1, header 2, the raw data table, the group table, the groups
    h2_size = 0xDC
    raw_stored = compress(kind, raw_table)
    group_table_len = n_groups * entry
    offset = 0x48 + h2_size
    raw_off = offset
    offset += (len(raw_stored) + 3) & ~3
    group_off = offset
    # the group table's own length is not known until it is compressed, and it holds offsets: place it after the groups
    cursor = offset
    placed = []
    offs = []
    for stored, field, packed in groups:
        offs.append(cursor if stored else 0)
        cursor += (len(stored) + 3) & ~3
    table = bytearray()
    for (stored, field, packed), o in zip(groups, offs):
        table += struct.pack(">II", o >> 2, field)
        if rvz:
            table += struct.pack(">I", packed)
    table_stored = compress(kind, bytes(table))
    group_table_off = cursor
    total = cursor + len(table_stored)
    comp_data = LZMA_PROPS if kind == "lzma" else bytes([LZMA2_PROP]) if kind == "lzma2" else b""
    dhead = bytes(iso[:0x80])
    h2 = struct.pack(">IIiI", 2 - 1, NAMES[kind], 5, chunk) + dhead
    h2 += struct.pack(">IIQ", 0, 0x30, 0) + hashlib.sha1(b"").digest()
    h2 += struct.pack(">IQI", 1, raw_off, len(raw_stored))
    h2 += struct.pack(">IQI", n_groups, group_table_off, len(table_stored))
    h2 += bytes([len(comp_data)]) + comp_data.ljust(7, b"\0")
    assert len(h2) == h2_size, len(h2)
    h1 = (b"RVZ\x01" if rvz else b"WIA\x01") + struct.pack(">III", 0x01000000 if not rvz else 0x00030000, 0x00030000 if rvz else 0x00080000, h2_size)
    h1 += hashlib.sha1(h2).digest() + struct.pack(">QQ", size, total)
    h1 += hashlib.sha1(h1[:0x30]).digest()
    assert len(h1) == 0x48
    with open(path, "wb") as f:
        f.write(h1 + h2)
        f.write(raw_stored + bytes(-len(raw_stored) % 4))
        for stored, field, packed in groups:
            f.write(stored + bytes(-len(stored) % 4))
        f.write(table_stored)
        assert f.tell() == total


# --- GCZ and CISO ---------------------------------------------------------------------------------------------------------

def write_gcz(iso, path, block=0x8000):
    size = len(iso)
    n = (size + block - 1) // block
    blocks = []
    for i in range(n):
        data = bytes(iso[i * block:(i + 1) * block]).ljust(block, b"\0")
        c = zlib.compress(data, 9)
        blocks.append((c, True) if len(c) < block * 97 // 100 else (data, False))
    pointers, hashes, pos = [], [], 0
    for data, comp in blocks:
        pointers.append(pos | (0 if comp else 1 << 63))
        hashes.append(zlib.adler32(data))
        pos += len(data)
    with open(path, "wb") as f:
        f.write(struct.pack("<IIQQII", 0xB10BC001, 0, pos, size, block, n))
        f.write(struct.pack("<%dQ" % n, *pointers))
        f.write(struct.pack("<%dI" % n, *hashes))
        for data, _ in blocks:
            f.write(data)


def write_ciso(iso, path, block=2 << 20):
    size = len(iso)
    n = (size + block - 1) // block
    used = [any(iso[i * block:(i + 1) * block]) for i in range(n)]
    header = b"CISO" + struct.pack("<I", block) + bytes(1 if i < n and used[i] else 0 for i in range(0x8000 - 8))
    with open(path, "wb") as f:
        f.write(header)
        for i in range(n):
            if used[i]:
                f.write(bytes(iso[i * block:(i + 1) * block]).ljust(block, b"\0"))


def write_nkit_like(iso, path):
    """A trimmed copy laid out as an NKit image is: the disc header, apploader, executable and file table where they were, the
    files packed one after another with the padding between them left out (offsets in the table rewritten), and the file ending
    with the last file. (NKit's own marker and the data to put the padding back are not made; the importer does not need them.)"""
    dol, fst_off, fst_size = struct.unpack_from(">III", iso, 0x420)
    fst = bytearray(iso[fst_off:fst_off + fst_size])
    count = struct.unpack_from(">I", fst, 8)[0]
    entries = sorted((struct.unpack_from(">I", fst, 12 * i + 4)[0], i) for i in range(1, count) if fst[12 * i] == 0)
    pos = (fst_off + fst_size + 0x7FFF) & ~0x7FFF
    with open(path, "wb") as f:
        f.write(bytes(iso[:pos]))
        for old, i in entries:
            size = struct.unpack_from(">I", fst, 12 * i + 8)[0]
            struct.pack_into(">I", fst, 12 * i + 4, pos)
            f.seek(pos)
            f.write(bytes(iso[old:old + size]))
            pos = (pos + size + 3) & ~3
        f.seek(fst_off)
        f.write(bytes(fst))
        f.truncate(pos)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("out")
    ap.add_argument("--only", nargs="*", help="file names to make (default: all)")
    ap.add_argument("--unpacker", help="bluewake_disc_unpack.exe: unpack and compare each image, then delete it")
    ap.add_argument("--junk-runs", type=int, default=400, help="how many stretches of junk padding to store as seeds in the RVZ files")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    f = open(a.iso, "rb")
    iso = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    assert len(iso) == GC_SIZE, "a retail GameCube disc image is %d bytes" % GC_SIZE
    jobs = []
    for kind in NAMES:
        if kind == "zstd":
            continue
        jobs.append((f"wia_{kind}.wia", lambda p, k=kind: write_wia(iso, p, False, k, 2 << 20, [])))
    for kind in NAMES:
        if kind == "purge":
            continue
        for chunk, label in ((32 << 10, "32k"), (128 << 10, "128k"), (2 << 20, "2m")):
            jobs.append((f"rvz_{kind}_{label}.rvz", lambda p, k=kind, c=chunk: write_rvz(iso, p, k, c, a.junk_runs)))
    jobs.append(("nkit_like.nkit.iso", lambda p: write_nkit_like(iso, p)))
    jobs.append(("disc.gcz", lambda p: write_gcz(iso, p)))
    jobs.append(("disc.ciso", lambda p: write_ciso(iso, p)))
    for name, make in jobs:
        if a.only and name not in a.only:
            continue
        if name.endswith(".rvz") and "_zstd_" in name and shutil.which("zstd") is None:
            print("skipped (no zstd on the PATH):", name)
            continue
        print("writing", name, flush=True)
        make(str(out / name))
        if a.unpacker and not name.endswith(".nkit.iso"):
            verify(a.unpacker, out / name, iso)


def sha1_of(data_or_path, is_path):
    h = hashlib.sha1()
    if is_path:
        with open(data_or_path, "rb") as f:
            for block in iter(lambda: f.read(1 << 22), b""):
                h.update(block)
    else:
        for i in range(0, len(data_or_path), 1 << 22):
            h.update(data_or_path[i:i + (1 << 22)])
    return h.hexdigest()


_ISO_SHA1 = []


def verify(unpacker, image, iso):
    """Unpacks `image` with the program, compares with the ISO, deletes both. Raises on a difference."""
    if not _ISO_SHA1:
        _ISO_SHA1.append(sha1_of(iso, False))
    result = image.with_suffix(image.suffix + ".iso")
    r = subprocess.run([unpacker, str(image), str(result)], capture_output=True, text=True)
    status = "unpacked" if r.returncode == 0 else "exit %d: %s" % (r.returncode, r.stderr.strip().splitlines()[-1] if r.stderr.strip() else "")
    same = r.returncode == 0 and sha1_of(result, True) == _ISO_SHA1[0]
    print("  %s: %s, %s (%d bytes of image)" % (image.name, status, "SHA-1 equal" if same else "DIFFERENT", image.stat().st_size), flush=True)
    for p in (image, result):
        if p.exists():
            p.unlink()
    if not same:
        raise SystemExit("%s did not unpack to the same ISO" % image.name)


_RUNS = {}


def write_rvz(iso, path, kind, chunk, limit):
    if limit not in _RUNS:
        _RUNS[limit] = junk_runs(iso, limit)
        print("  junk runs found:", len(_RUNS[limit]), "covering", sum(n for _, n, _ in _RUNS[limit]), "bytes", flush=True)
    write_wia(iso, path, True, kind, chunk, _RUNS[limit])


if __name__ == "__main__":
    sys.exit(main())
