#!/usr/bin/env python3
"""Frame rate of a scenario run, from its log's [fps] lines.

    python scripts/windows/fps_check.py <scenario> [<scenario> ...] [--size WxH] [--target 60] [--settle 8]

The host prints "[fps] shown=S game=G ..." about once a second: S is the frames presented, G the game's own
frames. The lines after the scenario's first warp, less the first --settle seconds (the area loading), are
the measure: minimum, 5th percentile and mean of each, and the seconds under the target. Meant for the
fpsq_* scenarios (Interpolation FPS 60, BLUEWAKE_WALL_PACE 1: real time), where shown should hold 60 and
game 30.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FPS = re.compile(r"\[fps\] shown=([\d.]+) game=([\d.]+)")


def measure(log: Path, settle: int):
    shown, game, started = [], [], False
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        if not started and ("[load] test warp" in line or "[teleport]" in line):
            started = True
            continue
        m = FPS.search(line)
        if m and started:
            shown.append(float(m.group(1)))
            game.append(float(m.group(2)))
    return shown[settle:], game[settle:]


def stats(values):
    if not values:
        return "no samples"
    ordered = sorted(values)
    p5 = ordered[max(0, int(len(ordered) * 0.05) - 1)]
    return f"min {ordered[0]:5.1f}  p5 {p5:5.1f}  mean {sum(values) / len(values):5.1f}  ({len(values)} s)"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("scenarios", nargs="+")
    p.add_argument("--size", default="1920x1080")
    p.add_argument("--tag", default="")
    p.add_argument("--target", type=float, default=60.0)
    p.add_argument("--settle", type=int, default=8)
    args = p.parse_args()
    bad = 0
    for name in args.scenarios:
        folder = ROOT / "build/windows/scenarios" / name / (f"{args.size}-{args.tag}" if args.tag else args.size)
        shown, game = measure(folder / "run.log", args.settle)
        under = sum(1 for s in shown if s < args.target - 1.0)
        print(f"{name:20} shown {stats(shown)}  under {args.target:.0f}: {under} s")
        print(f"{'':20} game  {stats(game)}")
        bad += under > 0
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
