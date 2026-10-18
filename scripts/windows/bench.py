#!/usr/bin/env python3
"""The speed benchmark: one command, a short table, and whether two builds play the same.

    python scripts/windows/bench.py run --label base [--exe EXE] [--module DLL] [--mode headless|rendered|both]
    python scripts/windows/bench.py compare base-headless,base-r2-headless,base-r3-headless perf1-headless,...

`run` plays scenarios/bench_tour.txt (written from STOPS below): from the forest-card state it warps through
Outset, Windfall, Windfall's crowd view, Dragon Roost Cavern, Forest Haven and the sea by the Fortress, unpaced
(as fast as the PC allows) with Smooth Motion off, so only the game is measured. Each stop's first SKIP retraces
(loading, shader compiles) are left out. Per stop it reports:
  game fps   retraces a second / 2 (unpaced: what the PC can do, not capped at 30)
  Mcyc/frame million reference cycles (QueryThreadCycleTime: the TSC rate, not the core clock) the game thread ran
             per game frame: its own CPU time, so time other programs take is left out, but turbo clocks and
             cache sharing still move it. Two runs of one build differ by up to ~6% per stop here: run each build
             three times, alternating, and compare with comma-separated labels (medians).
  worst ms   the longest retrace gap
headless: the game alone (BLUEWAKE_RENDERER=headless). rendered: the game plus the GX worker and the GPU.
Guest checkpoints (CPU and memory hashes every 300 retraces) are saved, and `compare` says whether two runs
match and where they first differ. A summary goes to OUT/<label>-<mode>.json; the output is the table only.
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
SCENARIO = ROOT / "scripts/windows/scenarios/bench_tour.txt"

LEAD = 10        # retraces before the first stop
STOP = 900       # retraces at each stop
SKIP = 300       # left out at the start of each stop
# (name, scenario line that takes Link there)
STOPS = [
    ("outset", "warp sea:44:0"),
    ("windfall", "warp sea:11:1"),
    ("windfall-crowd", "teleport sea 11 -569 89 -198007 -32768"),
    ("drc", "warp M_NewD2:0:0"),
    ("forest-haven", "warp sea:41:0"),
    ("sea-fortress", "warp sea:1:100"),
]
PERF = re.compile(r"\[perf\] retrace=(\d+) rate=([\d.]+) worst_ms=([\d.]+) hitches=(\d+) busy=(\d+)%(?: mcyc=([\d.]+))?")
CHECK = re.compile(r"\[guest-checkpoint\] version=\d+ retrace=(\d+) (.*)")
RANGE = re.compile(r"retraces (\d+)\.\.(\d+)")
# Where each stop begins: the log's own line for its warp or teleport. (The scenario's printed range is not used: a
# run with another module first remakes its start state, a run of its own with its own range.)
MOVE = re.compile(r"\[load\] test warp retrace=(\d+)|\[test\] teleport retrace=(\d+)")


def write_scenario() -> None:
    lines = ["description The speed benchmark's tour (written by scripts/windows/bench.py; edit STOPS there).",
             "start state forest-card", "card user/GZLE01.card", "size 1280x720",
             "env BLUEWAKE_WALL_PACE 0", "env BLUEWAKE_PERF_LOG 1", "env DOL_AURORA_FRAME_INTERP 0",
             # Noon, held (the save's time of day, 180 degrees): Windfall's crowd is out by day, and a tour at
             # night missed it (Windfall can dip by day while a night tour passes).
             "env BLUEWAKE_TEST_POKE 1+:0x803C4C2C=0x43340000/4",
             "env BLUEWAKE_GUEST_CHECKPOINT_INTERVAL 300", "cvar gSettings.Graphics.InterpolationFPS 30",
             f"wait {LEAD}"]
    for _, line in STOPS:
        lines += [line, f"wait {STOP}"]
    lines.append("end")
    text = "\n".join(lines) + "\n"
    if not SCENARIO.exists() or SCENARIO.read_text(encoding="utf-8") != text:
        SCENARIO.write_text(text, encoding="utf-8")


def summarize(log: str) -> dict:
    perf = [(int(m[1]), float(m[2]), float(m[3]), int(m[5]), float(m[6]) if m[6] else 0.0) for m in PERF.finditer(log)]
    moves = [int(m[1] or m[2]) for m in MOVE.finditer(log)]
    stops = {}
    for k, (name, _) in enumerate(STOPS):
        if k >= len(moves):
            stops[name] = None
            continue
        lo = moves[k] + SKIP
        hi = moves[k] + STOP
        rows = [p for p in perf if lo <= p[0] <= hi]
        if not rows:
            stops[name] = None
            continue
        stops[name] = {
            "game_fps": round(statistics.median(r[1] for r in rows) / 2, 1),
            "mcyc_frame": round(statistics.median(r[4] for r in rows) * 2, 2),
            "worst_ms": max(r[2] for r in rows),
            "busy": statistics.median(r[3] for r in rows),
            "seconds": len(rows),
        }
    checks = {m[1]: m[2] for m in CHECK.finditer(log)}
    return {"stops": stops, "checkpoints": checks}


def table(name: str, s: dict) -> None:
    print(f"{name}")
    print(f"  {'stop':<16}{'game fps':>9}{'Mcyc/frame':>12}{'worst ms':>10}{'busy':>6}")
    for stop, v in s["stops"].items():
        if v is None:
            print(f"  {stop:<16}{'no data (did the stop load?)':>37}")
        else:
            print(f"  {stop:<16}{v['game_fps']:>9}{v['mcyc_frame']:>12}{v['worst_ms']:>10}{v['busy']:>6}%")
    print(f"  checkpoints: {len(s['checkpoints'])}")


def run(args) -> None:
    write_scenario()
    OUT.mkdir(parents=True, exist_ok=True)
    modes = ["headless", "rendered"] if args.mode == "both" else [args.mode]
    for mode in modes:
        tag = f"{args.label}-{mode}"
        cmd = [sys.executable, str(ROOT / "scripts/windows/scenario.py"), "bench_tour", "--tag", tag,
               "--env", f"BLUEWAKE_RENDERER={'headless' if mode == 'headless' else 'aurora'}"]
        if args.exe:
            cmd += ["--exe", args.exe]
        if args.module:
            cmd += ["--module", args.module]
        for kv in args.env:
            cmd += ["--env", kv]
        res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        logs = sorted((ROOT / "build/windows/scenarios/bench_tour").glob(f"*-{tag}/run.log"))
        if not logs:
            print(f"{tag}: the run failed\n" + "\n".join(res.stdout.splitlines()[-6:]))
            continue
        s = summarize(logs[-1].read_text(encoding="utf-8", errors="replace"))
        s.update({"label": args.label, "mode": mode, "exe": args.exe or "", "module": args.module or ""})
        (OUT / f"{tag}.json").write_text(json.dumps(s, indent=1), encoding="utf-8")
        table(tag, s)


def load_runs(labels: str) -> list[dict]:
    return [json.loads((OUT / f"{x}.json").read_text(encoding="utf-8")) for x in labels.split(",")]


def median_stop(runs: list[dict], stop: str, key: str):
    vals = [r["stops"][stop][key] for r in runs if r["stops"].get(stop)]
    return statistics.median(vals) if vals else None


def redo(args) -> None:
    """Summarize saved runs again from their logs (after a change to summarize)."""
    for tag in args.tags:
        logs = sorted((ROOT / "build/windows/scenarios/bench_tour").glob(f"*-{tag}/run.log"))
        old = json.loads((OUT / f"{tag}.json").read_text(encoding="utf-8")) if (OUT / f"{tag}.json").exists() else {}
        s = summarize(logs[-1].read_text(encoding="utf-8", errors="replace"))
        s.update({k: old[k] for k in ("label", "mode", "exe", "module") if k in old})
        (OUT / f"{tag}.json").write_text(json.dumps(s, indent=1), encoding="utf-8")
        table(tag, s)


def compare(args) -> None:
    a, b = load_runs(args.a), load_runs(args.b)
    print(f"{args.a} -> {args.b}" + ("" if len(a) == len(b) == 1 else f"  (medians of {len(a)} and {len(b)} runs)"))
    print(f"  {'stop':<16}{'game fps':>18}{'Mcyc/frame':>20}")
    for stop in a[0]["stops"]:
        xf, yf = median_stop(a, stop, "game_fps"), median_stop(b, stop, "game_fps")
        xc, yc = median_stop(a, stop, "mcyc_frame"), median_stop(b, stop, "mcyc_frame")
        if xf is None or yf is None:
            print(f"  {stop:<16}  missing in one side")
            continue
        df = 100 * (yf / xf - 1)
        dc = 100 * (yc / xc - 1) if xc else 0.0
        print(f"  {stop:<16}{xf:>6.1f} -> {yf:<6.1f}{df:+5.1f}%{xc:>7.1f} -> {yc:<6.1f}{dc:+5.1f}%")
    ca, cb = a[0]["checkpoints"], b[0]["checkpoints"]
    common = sorted(set(ca) & set(cb), key=int)
    diff = [r for r in common if ca[r] != cb[r]]
    print(f"  checkpoints: {len(common)} compared, " + (f"first difference at retrace {diff[0]}" if diff else "identical"))


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--label", required=True)
    r.add_argument("--exe")
    r.add_argument("--module")
    r.add_argument("--mode", choices=("headless", "rendered", "both"), default="headless")
    r.add_argument("--env", action="append", default=[], metavar="NAME=VALUE",
                   help="an environment variable for the run, e.g. DOL_AURORA_BACKEND=vulkan or DOL_GX_FUSE=0")
    c = sub.add_parser("compare")
    c.add_argument("a")
    c.add_argument("b")
    d = sub.add_parser("redo")
    d.add_argument("tags", nargs="+")
    args = p.parse_args()
    {"run": run, "compare": compare, "redo": redo}[args.cmd](args)


if __name__ == "__main__":
    main()
