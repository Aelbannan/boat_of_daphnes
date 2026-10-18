#!/usr/bin/env python3
"""Make guest_symbols.txt for the in-client profiler (runtime/host/src/prof.c): one line per guest function,

    ADDR SIZE MODULE NAME        (hex ADDR as the recompiled game sees it, hex SIZE, module "main" or the REL's name)

    python scripts/windows/gen_guest_symbols.py [--decomp D:\\...\\tww-main] [--composite build/windows/composite-src] [-o guest_symbols.txt]

The names are the zeldaret/tww decomp's (CC0). Main-program symbols carry their own addresses; a REL's carry
offsets into its .text, placed at the base the composite linked that REL's section 1 at (rel_modules.inc;
the chunk folder names give each REL's name and id). Put the file beside the exe (or point
BLUEWAKE_PROF_SYMBOLS at it) and the profiler names its functions. Not part of a build: nothing here is shipped.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path
import sys as _bod_sys
from pathlib import Path as _BodPath
_bod_sys.path.insert(0, str(_BodPath(__file__).resolve().parents[1]))
try:
    from bod_paths import local_path, scratch  # noqa: E402  (scripts/bod_paths.py)
except ImportError:  # a release's copy of this script, without the repository's scripts/ beside it
    import os as _bod_os

    def local_path(name, fallback=""):
        return _bod_os.environ.get(name) or fallback

    def scratch():
        return _BodPath(local_path("BOD_SCRATCH", "scratch"))

ROOT = Path(__file__).resolve().parents[2]


def demangle(n: str) -> str:
    """tww symbols are CodeWarrior-mangled: name__<len>Class<F|C|...>args. A readable Class::name or the raw."""
    m = re.match(r"^(.+?)__(\d+)([A-Za-z_]\w*?)(?:F|FP|FU|FR|FQ|FC|C|$)", n)
    if not m:
        return n
    name, ln, rest = m.group(1), int(m.group(2)), n[m.end(1) + 2 + len(m.group(2)):]
    cls = rest[:ln]
    return f"{cls}::{name}" if len(cls) == ln else n


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--decomp", default=local_path("BOD_TWW_DECOMP"))
    p.add_argument("--composite", default=str(ROOT / "build/windows/composite-src"))
    p.add_argument("-o", "--out", default=str(ROOT / "build/windows/guest_symbols.txt"))
    a = p.parse_args()
    cfg = Path(a.decomp) / "config/GZLE01"
    comp = Path(a.composite)
    sym = re.compile(r"^(\S+) = \.(\w+):0x([0-9A-Fa-f]+); // type:function(?: size:0x([0-9A-Fa-f]+))?")
    rows: list[tuple[int, int, str, str]] = []
    for line in (cfg / "symbols.txt").read_text().splitlines():
        m = sym.match(line)
        if m and m.group(2) in ("text", "init"):
            rows.append((int(m.group(3), 16), int(m.group(4) or "0", 16), "main", demangle(m.group(1))))
    # REL text bases
    bases = {}
    text = (comp / "rel_modules.inc").read_text()
    for m in re.finditer(r"\{(\d+)u, 1u, 0x([0-9A-Fa-f]+)u, 0x([0-9A-Fa-f]+)u\}", text):
        bases[int(m.group(1))] = (int(m.group(2), 16), int(m.group(3), 16))
    n_rel = 0
    for d in sorted(comp.glob("chunks_*")):
        m = re.match(r"chunks_(.+)_(\d+)$", d.name)
        if not m:
            continue
        name, rid = m.group(1), int(m.group(2))
        sf = cfg / "rels" / name / "symbols.txt"
        if rid not in bases or not sf.is_file():
            continue
        base = bases[rid][0]
        n_rel += 1
        for line in sf.read_text().splitlines():
            s = sym.match(line)
            if s and s.group(2) == "text":
                rows.append((base + int(s.group(3), 16), int(s.group(4) or "0", 16), name, demangle(s.group(1))))
    rows.sort()
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", newline="\n") as f:
        for addr, size, mod, name in rows:
            f.write(f"{addr:08X} {size:X} {mod} {name}\n")
    print(f"{len(rows)} functions ({n_rel} RELs) -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
