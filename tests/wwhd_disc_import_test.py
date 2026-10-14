#!/usr/bin/env python3
"""End-to-end synthetic pack test: python this_file.py /path/to/native/CLI.

Requires only pycryptodome, through wwhd_disc_fixture. BTI/BFRES construction
is adapted from build/wwhd-ios-simulator-check/make_fixtures.py in the primary
checkout. No Pillow, private input, game assets or external key files are used.
Everything generated is removed with its TemporaryDirectory, including on a
failed assertion. The only subprocess is the explicitly supplied native CLI.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import wwhd_disc_fixture as fixture
from wwhd.disc import WiiUDisc
from wwhd.vendor import addrlib

ROOT = Path(__file__).resolve().parents[1]
COLOR = bytes((173, 61, 29, 255))
# The GC fixture is I8: replacement R, G, B and A are the HD red channel.
# A constant opaque source stays constant at every downsampled mip, so this
# oracle needs neither the production resampler nor Pillow.
EXPECTED_PIXEL = bytes((173, 173, 173, 173))
MIP_SIZES = (32, 16, 8, 4, 2, 1)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def make_gamecube():
    disc = bytearray(8192)
    disc[:8] = b"GZLE01\0\0"
    fixture.put(disc, 28, "I", 0xC2339F3D)
    fst = bytearray(24) + b"A.bti\0"
    fixture.put(fst, 0, "III", 0x01000000, 0, 2)
    fixture.put(fst, 12, "III", 0, 0x800, 288)
    disc[0x800] = 1  # I8, 16x16, one level, no executable content.
    fixture.put(disc, 0x802, "HH", 16, 16)
    disc[0x818] = 1
    fixture.put(disc, 0x81C, "I", 32)
    disc[0x820:0x920] = bytes([42]) * 256
    fixture.put(disc, 0x424, "II", 0x500, len(fst))
    disc[0x500:0x500 + len(fst)] = fst
    return disc


def make_bfres():
    data = bytearray(0x2000)
    data[:10] = b"FRES\x03\0\0\0\xfe\xff"
    fixture.put(data, 0x24, "i", 0x100 - 0x24)
    fixture.put(data, 0x100, "II", 40, 1)
    fixture.put(data, 0x120, "ii", 0x200 - 0x120, 0x240 - 0x124)
    data[0x200:0x202] = b"A\0"
    data[0x240:0x244] = b"FTEX"
    info = addrlib.getSurfaceInfo(0x1A, 32, 32, 1, 1, 1, 0, 0)
    fixture.put(data, 0x244, "16I", 1, 32, 32, 1, 1, 0x1A, 0, 1,
                info.surfSize, 0, 0, 0, 1, 0, info.baseAlign, info.pitch)
    data[0x2C8:0x2CC] = bytes((0, 1, 2, 3))
    fixture.put(data, 0x2F0, "i", 0x800 - 0x2F0)
    data.extend(bytes(max(0, 0x800 + info.surfSize - len(data))))
    data[0x800:0x800 + info.surfSize] = COLOR * (info.surfSize // 4)
    return bytes(data)


def make_wiiu(bfres):
    image, _, _ = fixture.make_disc()
    offset = fixture.HASH_DATA - 6  # BFRES header and payload cross hash blocks.
    fst = fixture.file_table([(1, 1), (2, 2), (4, 2), (6, 6)], [
        (1, "content", 0, 3, 0, 0),
        (0, "A.bfres", offset // fixture.SCALE, len(bfres), 0, 3),
    ], fixture.SCALE)
    fixture.install_metadata(image, fst)
    plain = bytearray(fixture.pattern(3 * fixture.HASH_DATA, 131))
    plain[offset:offset + len(bfres)] = bfres
    for index in range(3):
        data = plain[index * fixture.HASH_DATA:(index + 1) * fixture.HASH_DATA]
        h0 = hashlib.sha1(data).digest()
        hashes = bytearray(0x400)
        hashes[index * 20:index * 20 + 20] = h0
        start = fixture.HASHED + index * fixture.BLOCK
        image[start:start + fixture.BLOCK] = (
            fixture.encrypt(hashes, fixture.TITLE)
            + fixture.encrypt(data, fixture.TITLE, h0[:16]))
    return image


def check_reference(path, bfres):
    for title, common in ((fixture.TITLE, None), (None, fixture.COMMON)):
        disc = WiiUDisc(path, fixture.DISC, title_key=title, common_key=common)
        try:
            check(len(disc.entries) == 1, "toy disc must have exactly one game file")
            check(disc.entries[0].path == "content/A.bfres", "toy disc path differs")
            check(disc.read(disc.entries[0]) == bfres, "Python reference BFRES bytes differ")
        finally:
            disc.close()


def check_png(data, size, label):
    """Independent stdlib check of every PNG chunk, CRC, scanline and pixel."""
    check(data[:8] == b"\x89PNG\r\n\x1a\n", f"{label}: PNG signature")
    at, compressed, kinds = 8, bytearray(), []
    while at < len(data):
        check(at + 12 <= len(data), f"{label}: truncated chunk")
        length, kind = struct.unpack_from(">I4s", data, at)
        end = at + 12 + length
        check(end <= len(data), f"{label}: truncated chunk body")
        body = data[at + 8:at + 8 + length]
        crc = struct.unpack_from(">I", data, at + 8 + length)[0]
        check(crc == zlib.crc32(kind + body) & 0xFFFFFFFF, f"{label}: bad {kind!r} CRC")
        kinds.append(kind)
        if kind == b"IHDR":
            check(length == 13 and kinds == [b"IHDR"], f"{label}: invalid IHDR")
            check(struct.unpack(">IIBBBBB", body) == (size, size, 8, 6, 0, 0, 0),
                  f"{label}: wrong mip dimensions or PNG format")
        elif kind == b"IDAT":
            compressed.extend(body)
        elif kind == b"IEND":
            check(not body and end == len(data), f"{label}: invalid IEND/trailing data")
        else:
            raise AssertionError(f"{label}: unexpected chunk {kind!r}")
        at = end
    check(kinds[0] == b"IHDR" and kinds[-1] == b"IEND" and b"IDAT" in kinds,
          f"{label}: incomplete PNG")
    inflater = zlib.decompressobj()
    raw = inflater.decompress(compressed) + inflater.flush()
    check(inflater.eof and not inflater.unused_data, f"{label}: invalid deflate stream")
    # The native PNG writer promises unfiltered RGBA scanlines. The expected
    # pixels are fixed above, independently of folder/disc output comparison.
    expected = (b"\0" + EXPECTED_PIXEL * size) * size
    check(raw == expected, f"{label}: unexpected pixels, scanlines or mip size")


def check_pack(output):
    report = json.loads((output / "import-report.json").read_text(encoding="utf-8"))
    check(report["replacement_count"] == 1 and report["conflicts_skipped"] == 0
          and report["unsupported"] == [] and len(report["textures"]) == 1,
          f"{output.name}: expected one replacement without errors")
    texture = report["textures"][0]
    check(texture["original_size"] == [16, 16] and texture["hd_size"] == [32, 32]
          and texture["mipmaps"] == len(MIP_SIZES), f"{output.name}: texture metadata")
    base = texture["file"]
    check(re.fullmatch(r"tex1_16x16_[0-9a-f]{16}_1\.png", base) is not None,
          f"{output.name}: invalid replacement filename")
    names = [base] + [base[:-4] + f"_mip{i}.png" for i in range(1, len(MIP_SIZES))]
    pngs = {path.relative_to(output).as_posix(): path.read_bytes() for path in output.rglob("*.png")}
    check(set(pngs) == set(names), f"{output.name}: missing or extra PNGs")
    for name, size in zip(names, MIP_SIZES):
        check_png(pngs[name], size, output.name + "/" + name)
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in pngs.items()}
    check(texture["files_sha256"] == hashes, f"{output.name}: report PNG hashes differ")
    return pngs


def snapshot(directory):
    # Include directories so even empty staging folders cannot escape a check.
    return {p.relative_to(directory).as_posix(): p.read_bytes() if p.is_file() else None
            for p in directory.rglob("*")}


def run(cli, root):
    bfres = make_bfres()
    gc = root / "synthetic.iso"
    gc.write_bytes(make_gamecube())
    folder = root / "folder"
    (folder / "content").mkdir(parents=True)
    (folder / "content/A.bfres").write_bytes(bfres)
    images, keys, outputs = (root / name for name in ("images", "keys", "outputs"))
    for directory in (images, keys, outputs):
        directory.mkdir()
    key_paths = {}
    for name, key in (("disc", fixture.DISC), ("title", fixture.TITLE),
                      ("common", fixture.COMMON), ("wrong", fixture.WRONG)):
        key_paths[name] = keys / ("SYNTHETIC-" + name + ".key")
        key_paths[name].write_bytes(key)
    # Fallback names are read only beside these freshly generated toy images.
    (images / "game.key").write_bytes(fixture.DISC)
    (images / "common.key").write_bytes(fixture.COMMON)
    wud = make_wiiu(bfres)
    (images / "toy.wud").write_bytes(wud)
    (images / "toy.wux").write_bytes(fixture.mapped_wux(wud))
    for name in ("toy.wud", "toy.wux"):
        check_reference(images / name, bfres)

    environment = dict(os.environ, WWHD_EXACT_REPORT="1")
    def invoke(source, output, arguments=(), failure=None):
        before = snapshot(outputs)
        command = [str(cli), str(source), str(gc), str(output)] + [str(a) for a in arguments]
        result = subprocess.run(command, cwd=root, env=environment, capture_output=True,
                                text=True, timeout=30, check=False)
        detail = f"{output.name}: exit {result.returncode}\n{result.stdout}\n{result.stderr}"
        if failure is not None:
            check(result.returncode == 1, "expected a handled import failure: " + detail)
            check(failure.lower() in result.stderr.lower(), "unexpected failure reason: " + detail)
            check(snapshot(outputs) == before, "failed import changed output or left staging: " + detail)
            return None
        check(result.returncode == 0, detail)
        check(output.is_dir(), "successful import produced no output: " + detail)
        after = snapshot(outputs)
        prefix = output.name + "/"
        outside = {p: data for p, data in after.items() if p != output.name and not p.startswith(prefix)}
        check(outside == before, "successful import modified another output or left staging: " + detail)
        return check_pack(output)

    baseline = outputs / "folder-pack"
    expected_pngs = invoke(folder, baseline)
    title_args = (key_paths["disc"], "", key_paths["title"])
    modes = {
        "title": title_args,
        "common": (key_paths["disc"], key_paths["common"]),
        "fallback": (),
        "title-override": (key_paths["disc"], key_paths["wrong"], key_paths["title"]),
    }
    failures = 0
    for name in ("toy.wud", "toy.wux"):
        image = images / name
        for mode, arguments in modes.items():
            pngs = invoke(image, outputs / (name + "-" + mode), arguments)
            check(pngs == expected_pngs, f"{name}/{mode}: PNG bytes differ from folder pack")
        for mode, arguments in {
            "wrong-disc": (key_paths["wrong"], "", key_paths["title"]),
            "wrong-common": (key_paths["disc"], key_paths["wrong"]),
            "wrong-title": (key_paths["disc"], key_paths["common"], key_paths["wrong"]),
        }.items():
            output = outputs / (name + "-" + mode)
            invoke(image, output, arguments, failure="key")
            check(not output.exists(), f"{mode}: failed import produced an output")
            failures += 1
        # Exercise preservation with both valid and wrong keys. The baseline
        # is an actual completed toy pack, not merely an empty destination.
        for arguments in (title_args, (key_paths["wrong"], "", key_paths["title"])):
            invoke(image, baseline, arguments, failure="output already exists")
            failures += 1

    # Unlike wrong keys (rejected while opening), a corrupt payload fails after
    # import_pack has created staging. Require its cleanup for WUD and WUX.
    wud[fixture.HASHED + fixture.BLOCK + 0x400 + 19] ^= 1
    for name, data in (("corrupt.wud", wud), ("corrupt.wux", fixture.mapped_wux(wud))):
        path = images / name
        path.write_bytes(data)
        invoke(path, outputs / name, title_args, failure="content block checksum")
        failures += 1
    check(check_pack(baseline) == expected_pngs, "original folder pack was changed")
    print(f"PASS: folder + 2 disc formats x {len(modes)} key modes; six PNG mips each, "
          f"independent pixels/CRC; {failures} failure/preservation checks")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} native-import-CLI")
    cli = Path(sys.argv[1]).resolve()
    check(cli.is_file(), f"native importer CLI is missing: {cli}")
    scratch = ROOT / "build/wwhd-disc-import"
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pack-integration-", dir=scratch) as temporary:
        run(cli, Path(temporary))


if __name__ == "__main__":
    main()
