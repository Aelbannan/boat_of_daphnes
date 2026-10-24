#!/usr/bin/env python3
"""Which functions differ between the game module and the randomizer's (for switching between them while the game runs).

    python tools/rando/module_diff.py build/gen build/gen_rando OUT.bin

Compares the recompiler's output of the two programs function by function (the C text, with the per-module table
indexes left out: an override slot or hook number says nothing about what the code does). A function whose text is
the same in both does the same in both (they share guest memory and the REL load bases); every other one - changed,
or in one program only - is listed. When the session switches modules, code of the module that is no longer in force
(the frames already running on a guest thread's stack) reaches these through the module in force
(src/runtime/modswitch.cpp).

OUT.bin: u32 magic 'MDIF', u32 count, then count x (u32 module id, u32 address): main.dol is module 0 with the guest
address; a REL function has its section-relative address ((section << 24) | offset). Little-endian.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

FUNC = re.compile(r"^void (f_([0-9A-F]{8})|m(\d+)_([0-9A-F]{8}))(_orig)?\(Ctx& c\) \{$")
NUMBERS = re.compile(r"(g_fnOverride|g_hooks|g_patchPoints)\[\d+\]|runOverride\(c, \d+u\)")


def functions(folder: Path) -> dict[tuple[int, int], str]:
    out: dict[tuple[int, int], list[str]] = {}
    for path in sorted(folder.glob("*.cpp")):
        if path.name.endswith("_table.cpp"):
            continue
        key = None
        body: list[str] = []
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if key is None:
                m = FUNC.match(line)
                if m:
                    key = (0, int(m.group(2), 16)) if m.group(2) else (int(m.group(3)), int(m.group(4), 16))
                    body = [line.replace("_orig(", "(")]
                continue
            body.append(NUMBERS.sub(lambda x: x.group(1) or "runOverride", line))
            if line == "}":
                out.setdefault(key, []).append("\n".join(body))
                key = None
    return {k: "\n".join(v) for k, v in out.items()}


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    a, b = functions(Path(sys.argv[1])), functions(Path(sys.argv[2]))
    differ = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
    only_a = sum(1 for k in differ if k not in b)
    only_b = sum(1 for k in differ if k not in a)
    with open(sys.argv[3], "wb") as f:
        f.write(struct.pack("<4sI", b"MDIF", len(differ)))
        for module, addr in differ:
            f.write(struct.pack("<II", module, addr))
    dol = sum(1 for k in differ if k[0] == 0)
    print(f"{len(a)} and {len(b)} functions; {len(differ)} differ ({dol} in main.dol), {only_a} only in the first, "
          f"{only_b} only in the second -> {sys.argv[3]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
