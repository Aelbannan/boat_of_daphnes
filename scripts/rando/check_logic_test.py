#!/usr/bin/env python3
"""Does check_logic_server.py answer as wwrando's own playthrough does? (Check Tracker, reachability)

    python scripts/rando/check_logic_test.py SEED_JSON [SEED_JSON ...]

For each seed, drives the server as the app does and checks it against the seed's `playthrough` (wwrando's own sphere
calculation, in seed.json): with only the starting items the reachable set holds every sphere-1 location; with the items
of spheres 1..k-1 (plus the small keys found on the way, as wwrando hands them out) it holds every sphere-k location and,
for k >= 2, none of them is reachable one sphere earlier. Exit code 0 when every seed agrees.
"""
from __future__ import annotations

import json
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
SERVER = HERE / "check_logic_server.py"


class Client:
    def __init__(self, seed_json: Path):
        self.proc = subprocess.Popen([sys.executable, str(SERVER), str(seed_json)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     text=True, bufsize=1)
        self.ready = json.loads(self.proc.stdout.readline())
        self.n = 0

    def reach(self, items: Counter) -> set[str]:
        self.n += 1
        self.proc.stdin.write(json.dumps({"id": self.n, "items": dict(items)}) + "\n")
        self.proc.stdin.flush()
        reply = json.loads(self.proc.stdout.readline())
        if "error" in reply:
            raise RuntimeError(reply["error"])
        return set(reply["reachable"])

    def close(self):
        self.proc.stdin.write(json.dumps({"cmd": "quit"}) + "\n")
        self.proc.stdin.flush()
        self.proc.wait(timeout=30)


def run(seed_json: Path) -> bool:
    pack = json.loads(seed_json.read_text(encoding="utf-8"))
    placed = {loc["name"]: loc["item"] for loc in pack["locations"]}
    t0 = time.time()
    client = Client(seed_json)
    print(f"{seed_json.name}: ready in {time.time() - t0:.1f}s, {client.ready['locations']} locations, rebuild matches seed.json: {client.ready['seed_matches']}")
    ok = client.ready["seed_matches"]

    def with_keys(items: Counter) -> tuple[Counter, set[str]]:
        # wwrando hands out the dungeon keys placed in reachable locations without counting a sphere (non-keylunacy)
        while True:
            reach = client.reach(items)
            keys = Counter(placed[n] for n in reach if placed[n].endswith((" Small Key", " Big Key")))
            merged = items.copy()
            changed = False
            for k, c in keys.items():
                if merged[k] < c:
                    merged[k] = c
                    changed = True
            if not changed:
                return items, reach
            items = merged

    spheres = [[e for e in sphere if e["location_id"] is not None] for sphere in pack["playthrough"]]
    items = Counter(pack["starting"]["logic_items"])  # the save holds the starting gear too: the server takes the larger count
    previous: set[str] = set()
    checked = 0
    for k, sphere in enumerate(spheres, start=1):
        items, reach = with_keys(items)
        names = [e["location"] for e in sphere]
        missing = [n for n in names if n not in reach]
        # (a boss's Heart Container is listed in the sphere after the boss is beaten, though logic opens it with the boss room)
        early = [n for n in names if n in previous and not n.endswith(" Heart Container")] if k >= 2 else []
        if missing or early:
            ok = False
            print(f"  sphere {k}: NOT reachable now: {missing[:5]}; already reachable a sphere early: {early[:5]}")
        checked += len(names)
        previous = reach
        for e in sphere:
            if not e["item"].endswith((" Small Key", " Big Key")):
                items[e["item"]] += 1
    print(f"  {len(spheres)} spheres, {checked} playthrough locations checked: {'agree' if ok else 'DISAGREE'}")
    client.close()
    return ok


if __name__ == "__main__":
    results = [run(Path(p)) for p in sys.argv[1:]]
    sys.exit(0 if all(results) else 1)
