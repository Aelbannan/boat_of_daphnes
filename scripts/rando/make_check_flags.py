#!/usr/bin/env python3
"""Make check_flags.json: the save-file flag that says each randomizer location has been collected (one-time script).

    python scripts/rando/make_check_flags.py STAGE_DIR [--out scripts/rando/check_flags.json]

STAGE_DIR is a res\\Stage folder with the stage archives (a prepared seed's overlay\\files\\res\\Stage has every one a location
lives in; the item ids differ per seed but the flag numbers do not). Runs under the embedded Python 3.12 like the other
scripts here. For each location in wwrando's logic\\item_locations.txt it reads the actor behind each of the location's Paths
and works out the flag the game sets when the item is taken (decomp: d_a_tbox, d_a_item, d_salvage, d_a_boss_item ...):

    tbox   {t: save table, b: bit}   a chest was opened: dSv_memBit_c::mTbox of that stage's memory (STAG stage_id is the table)
    item   {t, b}                    an item was picked up: mItem bit b of the table (field items, pots, stone heads, buried
                                     treasure, the boss's heart container drop, whose item flag wwrando fixes at 0x15)
    ocean  {g, b}                    dSv_ocean_c grid g (the sea room), bit b: a big octo's or gunboat's treasure
    chart  {b}                       dSv_player_map_c "complete" bit b: a sunken treasure salvaged

Locations whose item is given by an NPC, a shop or an event (the rel/dol/event paths) have no such actor; they are listed
under "unmapped" with the reason, for the tracker to show as uncheckable.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RUNTIME = Path(os.environ.get("RANDO_RUNTIME") or REPO / "build" / "windows" / "rando_runtime")
if sys.version_info[:2] != (3, 12):
    sys.exit(subprocess.run([str(RUNTIME / "python" / "python.exe"), str(Path(__file__).resolve()), *sys.argv[1:]]).returncode)
for _p in (RUNTIME / "wwrando", RUNTIME / "gclib", RUNTIME / "site-packages"):
    sys.path.insert(0, str(_p))

from ruamel.yaml import YAML  # noqa: E402
from gclib.gcm import GCM  # noqa: E402
from gclib.rarc import RARC  # noqa: E402
from wwlib.dzx import ACTR, SCOB, STAG, TRES, DZx, DZxLayer  # noqa: E402

BOSS_ITEM_BIT = 0x15  # flexible_item_locations.asm: "item pickup flag 0x15 for all boss items"
MEMORY_ITEM = 32      # item bits from here on live in the room's zone bits, which the save does not keep

PATH_RE = re.compile(r"^([^/]+)/([^/]+\.arc)(?:/Layer([0-9a-b]))?/(Chest|Actor|ScalableObject)([0-9A-F]{3})$")


class Stages:
    def __init__(self, base: Path, overlay: Path | None):
        self.base = base
        self.overlay = overlay
        self.gcm = None
        if base.is_file():
            self.gcm = GCM(str(base))
            self.gcm.read_entire_disc()
        self.arcs: dict[str, RARC] = {}
        self.tables: dict[str, int] = {}

    def arc(self, stage: str, name: str) -> RARC:
        key = f"{stage}/{name}"
        if key not in self.arcs:
            patched = self.overlay / stage / name if self.overlay else None
            if patched is not None and patched.is_file():  # wwrando adds chests: its Paths count the added ones
                rarc = RARC(str(patched))
            elif self.gcm is not None:
                rarc = RARC(self.gcm.read_file_data(f"files/res/Stage/{stage}/{name}"))
            else:
                rarc = RARC(str(self.base / stage / name))
            rarc.read()
            self.arcs[key] = rarc
        return self.arcs[key]

    def dzx(self, stage: str, name: str) -> DZx:
        rarc = self.arc(stage, name)
        return rarc.get_file("stage.dzs" if name == "Stage.arc" else "room.dzr", DZx)

    def table(self, stage: str) -> int:
        if stage not in self.tables:
            stag = self.dzx(stage, "Stage.arc").entries_by_type(STAG)[0]
            self.tables[stage] = stag.stage_id
        return self.tables[stage]


def flag_for(stages: Stages, path: str):
    """The flag dict for one Path, or (None, reason)."""
    m = PATH_RE.match(path)
    if not m:
        kind = "rel" if path.startswith("rels/") else "dol" if path.startswith("main.dol") else \
            "event" if "/Event" in path else "custom" if path.startswith("CustomSymbol") else "path"
        return None, f"{kind}: {path}"
    stage, arc_name, layer, what, index = m.groups()
    index = int(index, 16)
    dz = stages.dzx(stage, arc_name)
    layer = DZxLayer(layer)
    table = stages.table(stage)
    if what == "Chest":
        e = dz.entries_by_type_and_layer(TRES, layer=layer)[index]
        if (e.params & 0x7F) in (7, 8):  # d_a_tbox "extra save info" chests keep their bit in the second sea table
            table = 1
        return {"k": "tbox", "t": table, "b": (e.params >> 7) & 0x1F}, None
    if what == "Actor":
        e = dz.entries_by_type_and_layer(ACTR, layer=layer)[index]
        p = e.params
        if e.name == "item":
            bit = (p >> 8) & 0x7F
        elif e.name == "Bitem":
            if table == 0:  # Helmaroc King's drop seen from the sea: the sea's bit 0x15 is not the fortress's
                return None, f"sea-stage boss item: {path}"
            return {"k": "item", "t": table, "b": BOSS_ITEM_BIT}, None
        elif e.name in ("Kmtub", "ootubo1", "kotubo"):
            bit = (p >> 16) & 0x7F
        elif e.name.startswith("Homen"):
            bit = p & 0x7F
        else:
            return None, f"actor {e.name}: {path}"
        if bit >= MEMORY_ITEM:
            return None, f"item bit {bit} is a zone bit: {path}"
        return {"k": "item", "t": table, "b": bit}, None
    e = dz.entries_by_type_and_layer(SCOB, layer=layer)[index]
    p = e.params
    room = int(re.search(r"Room(\d+)\.arc", arc_name).group(1)) if arc_name.startswith("Room") else 0
    if e.name == "Salvage" and (p >> 28) == 0:
        return {"k": "chart", "b": ((p >> 20) & 0xFF) - 1}, None
    if e.name == "SwSlvg" and (p >> 28) == 2:
        return {"k": "ocean", "g": room, "b": (p >> 20) & 0xFF}, None
    if e.name == "TagKb":
        bit = (p >> 8) & 0xFF
        return {"k": "item", "t": table, "b": bit}, None
    return None, f"scob {e.name}: {path}"


def main():
    args = sys.argv[1:]
    out = REPO / "scripts" / "rando" / "check_flags.json"
    if "--out" in args:
        i = args.index("--out")
        out = Path(args[i + 1])
        del args[i:i + 2]
    overlay = None
    if "--overlay" in args:
        i = args.index("--overlay")
        overlay = Path(args[i + 1])
        del args[i:i + 2]
    stages = Stages(Path(args[0]), overlay)
    locs = YAML(typ="safe").load((RUNTIME / "wwrando" / "logic" / "item_locations.txt").read_text(encoding="utf-8"))
    flags, unmapped = {}, {}
    stage_areas: dict[str, dict[str, int]] = {}
    for name, info in locs.items():
        found, why = [], []
        area = name.split(" - ")[0].removesuffix(" Sector")
        for path in info["Paths"]:
            stage = path.split("/")[0]
            if "/" in path and not stage.startswith(("rels", "main.dol", "CustomSymbol")):
                stage_areas.setdefault(stage, {}).setdefault(area, 0)
                stage_areas[stage][area] += 1
            f, reason = flag_for(stages, path)
            if f is not None:
                if f not in found:
                    found.append(f)
            else:
                why.append(reason)
        if found:
            flags[name] = found
        else:
            unmapped[name] = why[0] if len(why) == 1 else "; ".join(why)
    # Which area a stage belongs to (for opening the area Link is in): the area most of its locations are in. The sea is
    # told apart by room number instead (Room N is the chart's square N).
    areas = {stage: max(counts, key=counts.get) for stage, counts in stage_areas.items() if stage != "sea"}
    doc = {"version": 1, "flags": flags, "unmapped": unmapped, "stage_areas": areas}
    out.write_text(json.dumps(doc, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    kinds = {}
    for fl in flags.values():
        for f in fl:
            kinds[f["k"]] = kinds.get(f["k"], 0) + 1
    print(f"{len(flags)} locations with a flag {kinds}, {len(unmapped)} unmapped -> {out}")


if __name__ == "__main__":
    main()
