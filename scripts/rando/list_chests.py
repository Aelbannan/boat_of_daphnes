#!/usr/bin/env python3
"""List the treasure chests of one stage in a seed's patched files (testing; runs under the embedded Python).

    python scripts/rando/list_chests.py SEED_FOLDER STAGE      e.g. ... build/e2e/rz/E2EB M_NewD2

Reads overlay\\files\\res\\Stage\\STAGE\\*.arc written by patch_game.py --files all (or the disc's, with --iso, for
the vanilla values) and prints each TRES entry: file, room, position, item id.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RUNTIME = Path(os.environ.get("RANDO_RUNTIME") or REPO / "build" / "windows" / "rando_runtime")
if sys.version_info[:2] != (3, 12):
    sys.exit(subprocess.run([str(RUNTIME / "python" / "python.exe"), str(Path(__file__).resolve()), *sys.argv[1:]]).returncode)
for _p in (RUNTIME / "wwrando", RUNTIME / "gclib", RUNTIME / "site-packages"):
    sys.path.insert(0, str(_p))

from gclib.rarc import RARC  # noqa: E402
from wwlib.dzx import DZx, TRES, PLYR  # noqa: E402


def main():
    folder, stage = Path(sys.argv[1]), sys.argv[2]
    base = folder / "overlay" / "files" / "res" / "Stage" / stage
    for arc_path in sorted(base.glob("*.arc")):
        rarc = RARC(str(arc_path))
        rarc.read()
        for f in rarc.file_entries:
            if getattr(f, "is_dir", False) or not f.name.endswith((".dzr", ".dzs")):
                continue
            dzx = DZx(f)
            dzx.read()
            if len(sys.argv) > 3 and sys.argv[3] == "--spawns":
                for chunk in dzx.entries_by_type(PLYR):
                    print(arc_path.name, f.name, "spawn id=%d" % chunk.spawn_id, "room=%d" % chunk.room_num,
                          "pos=(%.0f, %.0f, %.0f)" % (chunk.x_pos, chunk.y_pos, chunk.z_pos))
            for chunk in dzx.entries_by_type(TRES):
                print(arc_path.name, f.name, "item=0x%02X" % chunk.item_id, "pos=(%.0f, %.0f, %.0f)" % (chunk.x_pos, chunk.y_pos, chunk.z_pos),
                      "rot=0x%04X" % (chunk.y_rot & 0xFFFF), "params=0x%08X" % chunk.params)


if __name__ == "__main__":
    main()
