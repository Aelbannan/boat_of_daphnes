#!/usr/bin/env python3
"""The stability tour: does the game hold its frame rate in busy places?

    python scripts/windows/stability.py run --label NAME [--exe EXE] [--module DLL] [--size 3440x1381]
    python scripts/windows/stability.py compare A B

Real time, at a demanding player's settings (Interpolation FPS 60, vsync on, the window at 3440x1381), from the forest-card
state: points on Outset, Windfall (its crowd at noon) and Dragon Roost. At each point Link runs in circles while the
camera pans both ways, about 12 seconds. The first 2 seconds of a point (loading) are left out. Per point:
  min fps   the lowest second's game frames (two retraces make one)
  p50 fps   the median second
  worst ms  the longest frame
  dips      seconds under 29.5 game fps
--interp N sets Interpolation FPS (for example 120); --gba links the Tingle Tuner to the in-app GBA first (the
Tuner given and used, as gba_windfall_linked does), so its JoyBus stream runs through the tour.
Pass: no dips at any point (the busy places as steady as the quiet ones). Needs a quiet PC: other programs' load
shows up as dips. One game at a time; a window on the dummy monitor unless --size asks for more than it holds.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
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
OUT = Path(os.environ.get("BENCH_OUT", str(scratch() / "bench")))
SCENARIO = ROOT / "scripts/windows/scenarios/stability_tour.txt"
SETTLE, POINT = 120, 720  # retraces: left out at the start of a point; a point in all
POINTS = [
    ("outset-pier", "warp sea:44:0"),
    ("outset-village", "warp sea:44:204"),
    ("outset-hill", "warp sea:44:6"),
    ("outset-lookout", "warp sea:44:8"),
    ("windfall-town", "warp sea:11:1"),
    ("windfall-crowd", "teleport sea 11 -569 89 -198007 -32768"),
    ("windfall-square", "warp sea:11:0"),
    ("dragon-roost", "warp sea:13:0"),
    ("dragon-roost-2", "warp sea:13:1"),
]
PERF = re.compile(r"\[perf\] retrace=(\d+) rate=([\d.]+) worst_ms=([\d.]+)")
MOVE = re.compile(r"\[load\] test warp retrace=(\d+)|\[test\] teleport retrace=(\d+)")


# The Tuner given, put on X and used: the game uploads the Tingle program and the link streams from then on.
GBA_LINK = ["env BLUEWAKE_TINGLE_TUNER auto", "env BLUEWAKE_GBA_LOG 1", "give tingle_tuner", "equip X tingle_tuner", "set rupees 100", "wait 300",
            "press X", "wait 150", "press A", "wait 100", "press A", "wait 700", "press A", "wait 200", "press A",
            "wait 900"]


def write_scenario(size: str, interp: int = 60, gba: bool = False) -> None:
    lines = ["description The stability tour (scripts/windows/stability.py writes it; edit POINTS there).",
             "start state forest-card", "card user/GZLE01.card", f"size {size}",
             "env BLUEWAKE_WALL_PACE 1", "env BLUEWAKE_PERF_LOG 1",
             # Noon, held (the save's time of day, 180 degrees): Windfall's crowd is out by day, and a tour at
             # night missed it (Windfall can dip by day while a night tour passes).
             "env BLUEWAKE_TEST_POKE 1+:0x803C4C2C=0x43340000/4",
             f"cvar gSettings.Graphics.InterpolationFPS {interp}", "cvar gSettings.Graphics.Vsync 1", "wait 10"]
    if gba:
        lines += GBA_LINK
    for _, move in POINTS:
        lines += [move, f"wait {SETTLE}",
                  # run in a circle while the camera pans one way, then the other
                  "stick 0 127 100", "stick 127 0 100", "stick 0 -127 100", "stick -127 0 100",
                  "cstick 127 0 80", "cstick -127 0 80",
                  f"wait {POINT - SETTLE - 560}"]
    lines.append("end")
    SCENARIO.write_text("\n".join(lines) + "\n", encoding="utf-8")


def summarize(log: str) -> dict:
    perf = [(int(m[1]), float(m[2]), float(m[3])) for m in PERF.finditer(log)]
    moves = [int(m[1] or m[2]) for m in MOVE.finditer(log)]
    points = {}
    for k, (name, _) in enumerate(POINTS):
        if k >= len(moves):
            points[name] = None
            continue
        rows = [p for p in perf if moves[k] + SETTLE <= p[0] <= moves[k] + POINT]
        if not rows:
            points[name] = None
            continue
        fps = [r[1] / 2 for r in rows]
        points[name] = {"min": round(min(fps), 1), "p50": round(statistics.median(fps), 1),
                        "worst_ms": max(r[2] for r in rows), "dips": sum(1 for f in fps if f < 29.5),
                        "seconds": len(rows)}
    return {"points": points}


def table(name: str, s: dict) -> bool:
    print(name)
    print(f"  {'point':<18}{'min fps':>8}{'p50 fps':>9}{'worst ms':>10}{'dips':>6}")
    ok = True
    for p, v in s["points"].items():
        if v is None:
            print(f"  {p:<18}  no data (did it load?)")
            ok = False
            continue
        ok = ok and v["dips"] == 0
        print(f"  {p:<18}{v['min']:>8}{v['p50']:>9}{v['worst_ms']:>10}{v['dips']:>6}")
    print("  PASS: no dips" if ok else "  FAIL: dips or missing points")
    return ok


def run(a) -> None:
    write_scenario(a.size, a.interp, a.gba)
    OUT.mkdir(parents=True, exist_ok=True)
    tag = f"stab-{a.label}"
    cmd = [sys.executable, str(ROOT / "scripts/windows/scenario.py"), "stability_tour", "--tag", tag]
    if a.exe:
        cmd += ["--exe", a.exe]
    if a.module:
        cmd += ["--module", a.module]
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    logs = sorted((ROOT / "build/windows/scenarios/stability_tour").glob(f"*-{tag}/run.log"))
    if not logs:
        sys.exit(f"{tag}: the run failed\n" + "\n".join(res.stdout.splitlines()[-6:]))
    s = summarize(logs[-1].read_text(encoding="utf-8", errors="replace"))
    (OUT / f"{tag}.json").write_text(json.dumps(s, indent=1), encoding="utf-8")
    sys.exit(0 if table(tag, s) else 1)


def compare(a) -> None:
    x = json.loads((OUT / f"stab-{a.a}.json").read_text(encoding="utf-8"))
    y = json.loads((OUT / f"stab-{a.b}.json").read_text(encoding="utf-8"))
    print(f"{a.a} -> {a.b}")
    for p in x["points"]:
        u, v = x["points"].get(p), y["points"].get(p)
        if u and v:
            print(f"  {p:<18} min {u['min']:>5} -> {v['min']:<5} worst {u['worst_ms']:>6} -> {v['worst_ms']:<6} "
                  f"dips {u['dips']} -> {v['dips']}")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--label", required=True)
    r.add_argument("--exe")
    r.add_argument("--module")
    r.add_argument("--size", default="3440x1381")
    r.add_argument("--interp", type=int, default=60)
    r.add_argument("--gba", action="store_true")
    c = sub.add_parser("compare")
    c.add_argument("a")
    c.add_argument("b")
    a = p.parse_args()
    run(a) if a.cmd == "run" else compare(a)


if __name__ == "__main__":
    main()
