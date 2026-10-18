#!/usr/bin/env python3
"""A scenario that checks a seed's data in the running game against its seed.json (testing).

    python scripts/rando/e2e_peeks.py SEED_FOLDER NAME [--dir RANDOMIZER_DIR] > scripts/windows/scenarios/NAME.txt

The scenario boots a randomizer session on the seed, waits for the file select, and peeks the bytes the host writes at start
(scripts/rando/variant_sites.py's data table and the custom symbols): each peek is expected to hold what seed.json says.
Runs through scripts/windows/scenario.py like every test (docs/fork/rando-inhouse.md).
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("seed_folder")
    ap.add_argument("name")
    ap.add_argument("--dir", help="the Randomizer folder (default: the seed folder's parent)")
    a = ap.parse_args()
    folder = Path(a.seed_folder).resolve()
    pack = json.loads((folder / "seed.json").read_text())
    root = Path(a.dir).resolve() if a.dir else folder.parent
    seeds = {}
    # the data table the host uses, from the seed_sites.json the table was generated from
    sites = json.loads((HERE / "seed_sites.json").read_text())
    peeks = []
    for row in sites["seed_sites"]:
        if row["file"] != "main.dol" or row["kind"] != "data":
            continue
        for src in row["sources"]:
            if src["type"] == "item_location":
                address = int(row["guest_address"], 16) + src["byte_in_site"]
                item = pack["locations"][src["location_id"]]["item_id"]
                if item is not None:
                    peeks.append((address, 1, item, src["location"]))
    gv = pack["starting"]["game_variables"]
    for address, size, value, what in ((0x803FDC70, 1, gv["num_triforce_shards_to_start_with"], "shards"),
                                       (0x803FDC72, 1, gv["sword_mode"], "sword mode"),
                                       (0x803FDCCC, 2, gv["starting_quarter_hearts"], "quarter hearts"),
                                       (0x803FDCCE, 1, gv["starting_magic"], "magic")):
        peeks.append((address, size, value, what))
    gear = pack["starting"]["gear"]
    for i, g in enumerate(gear[:4]):
        peeks.append((0x803FDC74 + i, 1, g["item_id"], "starting gear %d: %s" % (i, g["item"])))
    out = ["description %s: the seed's data in the running game (peeks against seed.json)." % a.name,
           "start boot raw", "size 1280x960", "cvar gRandomizer.Mode 1", "cvar gRandomizer.PlaySeed %s" % folder.name,
           "env BLUEWAKE_RANDOMIZER_DIR %s" % root, "wait 1000"]
    for address, size, value, what in peeks:
        out.append("peek 0x%08X/%d  # %s" % (address, size, what))
        out.append("wait 2")
    out.append("end 1100")
    for address, size, value, what in peeks:
        out.append("expect peek retrace=\\d+ address=0x%08X size=%d value=0x%X" % (address, size, value))
    print("\n".join(out))


if __name__ == "__main__":
    main()
