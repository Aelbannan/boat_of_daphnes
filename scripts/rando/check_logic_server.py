#!/usr/bin/env python3
"""The Check Tracker's reachability helper: wwrando's own Logic, kept running, asked again on each inventory change.

    check_logic_server.py SEED_JSON          (any Python; re-runs itself under the embedded 3.12 like generate_seed.py)

It rebuilds the seed exactly as generate_seed.py made it (same permalink, same dry run, so the same entrances, charts and
required bosses), then answers JSON lines on stdin with JSON lines on stdout, one request per line:

    {"id": 1, "items": {"Progressive Sword": 2, "DRC Small Key": 1, ...}}
        ->  {"id": 1, "reachable": ["Outset Island - Underneath Link's House", ...]}

`items` is what the player has found (a count per wwrando item name); the seed's starting items are always counted, and a
name wwrando does not know is ignored. The first line out is {"ready": true, "locations": N, "seed_matches": true}, where
seed_matches says the rebuild placed every item where seed.json has it. chatter goes to stderr.
"""
from __future__ import annotations

import contextlib
import json
import os
import sys
import tempfile
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
# In a package this file sits in rando\ with the runtime (python, wwrando, gclib, site-packages) beside it; in the repo the
# runtime is build\windows\rando_runtime. generate_seed.py has the same rule (RANDO_RUNTIME).
if "RANDO_RUNTIME" not in os.environ:
    os.environ["RANDO_RUNTIME"] = str(HERE if (HERE / "wwrando").is_dir() else HERE.parents[1] / "build" / "windows" / "rando_runtime")
sys.path.insert(0, str(HERE))
if sys.version_info[:2] != (3, 12):  # wwrando needs 3.12: hand over to the embedded one, stdin and stdout passed through
    import subprocess
    py = Path(os.environ["RANDO_RUNTIME"]) / "python" / "python.exe"
    sys.exit(subprocess.run([str(py), str(Path(__file__).resolve()), *sys.argv[1:]]).returncode)

import generate_seed as gs  # noqa: E402  (relaunches under the embedded Python 3.12 when needed, then sets sys.path)


def build(seed_json: Path):
    from logic.logic import Logic
    from randomizer import WWRandomizer
    from wwrando import make_argparser

    pack = json.loads(seed_json.read_text(encoding="utf-8"))
    bunny, _, _, permalink = gs.take_bunny_hood({}, pack["permalink"])
    start_hood = any(g["item"] == gs.BUNNY_HOOD[1] for g in pack["starting"]["gear"])
    seed, options = WWRandomizer.decode_permalink(permalink)
    options.dry_run = True
    if bunny and not start_hood:
        gs.add_bunny_hood_to_pool()
    cmd = make_argparser().parse_args(args=["--dry"])
    with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(sys.stderr):
        rando = WWRandomizer(seed=seed, clean_iso_path=None, randomized_output_folder=tmp, options=options, cmd_line_args=cmd)
        if bunny:
            gs.register_bunny_hood(rando)
        rando.randomize_all()
    placed = {loc["name"]: loc["item"] for loc in pack["locations"]}
    matches = all(rando.logic.done_item_locations.get(name) == item for name, item in placed.items())
    # A fresh Logic over the finished randomizer state: no items placed, only the starting ones owned (as the spoiler log does).
    with contextlib.redirect_stdout(sys.stderr):
        logic = Logic(rando)
    return logic, matches


class Server:
    def __init__(self, logic):
        self.logic = logic
        self.base = Counter(logic.currently_owned_items)  # the starting items (cleaned names)

    def reachable(self, items: dict) -> list[str]:
        logic = self.logic
        have = Counter(self.base)
        for name, count in items.items():
            cleaned = logic.clean_item_name(name)
            if cleaned in logic.all_cleaned_item_names and count > have[cleaned]:
                have[cleaned] = int(count)
        logic.currently_owned_items.clear()
        for name, count in have.items():
            logic.currently_owned_items.extend([name] * count)
        logic.clear_req_caches()
        return [name for name in logic.item_locations if logic.check_location_accessible(name)]


def main(argv) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    logic, matches = build(Path(argv[1]))
    server = Server(logic)
    print(json.dumps({"ready": True, "locations": len(logic.item_locations), "seed_matches": matches}), flush=True)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            if req.get("cmd") == "quit":
                break
            reply = {"id": req.get("id"), "reachable": server.reachable(req.get("items", {}))}
        except Exception as e:  # keep serving; the app shows the error
            reply = {"id": None, "error": f"{type(e).__name__}: {e}"}
        print(json.dumps(reply), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
