#!/usr/bin/env python3
"""Rebuild only the app (boat-of-daphnes.exe) of this checkout, in the Visual Studio environment.

    python scripts/windows/build_app_only.py [--jobs 2] [--reconfigure]

For work that changes the host or the app and not the game module. The exe lands in build/windows/app/; the
package folder is not touched (copy the exe yourself, or run build.py). The compile waits for the machine's
build lock (machine_lock.py), so builds of several worktrees take turns. Never run cmake or ninja from Git
Bash: this script sets up the environment build.py uses.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/windows"))
import build  # noqa: E402
from machine_lock import machine_lock  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--reconfigure", action="store_true", help="configure the app build again first")
    p.add_argument("--rando-variant", action="store_true", help="compile the randomizer's host tables (build/windows/rando/sites/"
                   "rando_sites.inc, made by a build.py --rando-variant run): what a release exe has; without them the seed has no hooks")
    p.add_argument("--target", action="append", help="build this target instead of the app (repeatable), for example "
                   "bluewake_gc_adapter_proto_test; the program lands in build/windows/app/")
    a = p.parse_args()
    args = types.SimpleNamespace(out=ROOT / "build/windows", march="x86-64-v3", console=False, no_app_pgo=True,
                                 jobs=a.jobs, rando_variant=a.rando_variant)
    b = build.Builder(args)
    b.env = b.visual_studio_env()
    app = ROOT / "build/windows/app"
    if a.reconfigure or not (app / "build.ninja").exists():
        b.configure_app()
    cmake = shutil.which("cmake", path=b.env["PATH"])
    # Its own lock: an app build is small and runs beside a game-module
    # compile (which holds "build" for an hour or more), never beside another app build.
    with machine_lock("app-build"):
        targets = a.target or ["BlueWake"]
        r = subprocess.run([cmake, "--build", str(app), "--target", *targets, "-j", str(a.jobs)], env=b.env)
    print(f"app: {app / 'boat-of-daphnes.exe'} (exit {r.returncode})")
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
