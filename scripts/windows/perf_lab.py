#!/usr/bin/env python3
"""Quiet-PC performance runs of a player's folder (the "lab"): the player's exe, settings and save, a real-time
window on the main monitor, the game started from a saved state, and a table made from the log.

    python scripts/windows/perf_lab.py make-state --lab LAB --name windfall   # file select -> state
    python scripts/windows/perf_lab.py run --lab LAB --label base --state windfall --seconds 60 \
        [--exe EXE] [--cvar A.B=V ...] [--env K=V ...] [--size 3440x1381] [--pos 0,0] [--no-locks]
    python scripts/windows/perf_lab.py table LOG [--skip 8]

Holds the machine locks scenario, build, app-build and perf for the run (one game window machine-wide, no
compile beside it), checks that the CPU is quiet before it starts, and records the CPU use of everything but the
game while it runs. Settings: LAB/boat-of-daphnes.json is saved once as boat-of-daphnes.json.orig and every run
writes orig + the --cvar overrides. Testing only; nothing here goes into a player's build.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import shutil
import subprocess
import sys
import time
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
from machine_lock import machine_lock  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get("PERF_OUT", str(scratch() / "perf")))

# File select with a played card (as scenarios/card_forest.txt): retraces from the start.
MAKE_PADS = [(600, 0x1000, 3), (703, 0x0100, 2), (805, 0x0100, 2), (907, 0x0100, 2), (1009, 0x0100, 2),
             (1111, 0x0100, 2), (1313, 0x0100, 2)]
MAKE_SAVE_AT = 2700


class _FT(ctypes.Structure):
    _fields_ = [("lo", ctypes.c_uint32), ("hi", ctypes.c_uint32)]


def _ft(f):
    return (f.hi << 32) | f.lo


def system_times():
    idle, kern, user = _FT(), _FT(), _FT()
    ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kern), ctypes.byref(user))
    return _ft(idle), _ft(kern) + _ft(user)  # kernel includes idle


def proc_times(handle):
    c, e, k, u = _FT(), _FT(), _FT(), _FT()
    ctypes.windll.kernel32.GetProcessTimes(handle, ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u))
    return _ft(k) + _ft(u)


def quiet_check(seconds=3.0):
    """Total CPU% of the machine over a few seconds, and the busiest process names."""
    i0, t0 = system_times()
    time.sleep(seconds)
    i1, t1 = system_times()
    busy = 100.0 * (1.0 - (i1 - i0) / max(1, t1 - t0))
    names = subprocess.run(["tasklist", "/NH", "/FO", "CSV"], capture_output=True, text=True).stdout.lower()
    bad = [n for n in ("clang", "ninja", "lld", "cl.exe", "link.exe", "boat-of-daphnes", "bluewake") if f'"{n}' in names]
    return busy, bad


def read_settings(lab: Path) -> dict:
    orig = lab / "boat-of-daphnes.json.orig"
    live = lab / "boat-of-daphnes.json"
    if not orig.exists():
        shutil.copy2(live, orig)
    return json.loads(orig.read_text())


def write_settings(lab: Path, cvars: list[str]):
    root = read_settings(lab)
    for item in cvars:
        name, value = item.split("=", 1)
        try:
            value = int(value)
        except ValueError:
            try:
                value = float(value)
            except ValueError:
                pass
        node = root["CVars"]
        parts = name.split(".")
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = value
    (lab / "boat-of-daphnes.json").write_text(json.dumps(root, indent=4) + "\n")


def base_env(a) -> dict:
    env = {k: v for k, v in os.environ.items() if not k.startswith(("BLUEWAKE_", "DOL_", "LLVM_PROFILE_"))}
    env.update({
        "BLUEWAKE_WINDOW_POS": a.pos, "BLUEWAKE_WINDOW_NOACTIVATE": "1", "SDL_AUDIO_DRIVER": "dummy",
        "BLUEWAKE_NO_DIALOG": "1", "BLUEWAKE_LIVE_PAD": "0", "BLUEWAKE_SESSION_LOG": "0",
        "DOL_AURORA_WINDOW": a.size, "DOL_AURORA_RENDER_SCALE": "0",
        "BLUEWAKE_STATE_DIR": str(Path(a.lab) / "Save" / "states"),
    })
    for e in a.env:
        k, v = e.split("=", 1)
        env[k] = v
    return env


def run_game(a, env, label, locks=True) -> tuple[Path, dict]:
    lab = Path(a.lab).resolve()
    exe = Path(a.exe).resolve() if a.exe else lab / "boat-of-daphnes.exe"
    OUT.mkdir(parents=True, exist_ok=True)
    log = OUT / f"{label}.log"
    cmd = [str(exe)]
    # Always name the module: with none, the exe looks around for one, and a run once picked up another build's
    # (a list "betterww" without the "+", mask 0x0, no file select hook) while another agent's build was writing.
    module = Path(a.module).resolve() if a.module else lab / "gGZLE01_recomp.dll"
    cmd += ["--module", str(module)]
    info: dict = {}

    def go():
        busy, bad = quiet_check()
        info["pre_cpu_pct"] = round(busy, 1)
        if bad or busy > 15.0:
            print(f"WARNING: PC not quiet before the run: cpu {busy:.0f}% {bad}", flush=True)
        with open(log, "wb") as stream:
            p = subprocess.Popen(cmd, env=env, stdout=stream, stderr=subprocess.STDOUT, cwd=str(lab))
            i0, t0 = system_times()
            h = ctypes.windll.kernel32.OpenProcess(0x1000 | 0x0400, False, p.pid)
            try:
                p.wait(timeout=a.timeout)
            except subprocess.TimeoutExpired:
                p.kill()
                info["timeout"] = True
            i1, t1 = system_times()
            sys_busy = 100.0 * (1.0 - (i1 - i0) / max(1, t1 - t0))
            ncpu = os.cpu_count() or 1
            game = 100.0 * proc_times(h) / max(1, t1 - t0)  # same units: % of whole machine
            info["total_cpu_pct"] = round(sys_busy, 1)
            info["game_cpu_pct"] = round(game, 1)
            info["others_cpu_pct"] = round(max(0.0, sys_busy - game), 1)
            info["exit"] = p.returncode
            info["ncpu"] = ncpu

    if locks:
        with machine_lock("scenario"), machine_lock("build"), machine_lock("app-build"), machine_lock("perf"):
            go()
    else:
        go()
    (OUT / f"{label}.json").write_text(json.dumps(info, indent=2))
    return log, info


FPS = re.compile(r"\[fps\] shown=([\d.]+) game=([\d.]+)")


def pct(vals, p):
    if not vals:
        return float("nan")
    s = sorted(vals)
    return s[min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))]


def table(log: Path, skip: int) -> str:
    text = log.read_text(encoding="utf-8", errors="replace")
    cut = text.find("[teleport] to")
    if cut > 0:
        text = text[cut:]  # runs that boot through the file select: only the seconds after the teleport count
    shown, game = [], []
    for m in FPS.finditer(text):
        shown.append(float(m.group(1)))
        game.append(float(m.group(2)))
    shown, game = shown[skip:], game[skip:]
    if not shown:
        return f"{log.name}: no [fps] samples"
    f = lambda v: f"mean {sum(v)/len(v):5.1f} p1 {pct(v,1):5.1f} p10 {pct(v,10):5.1f} min {min(v):5.1f}"
    under = sum(1 for s in shown if s < 58.0)
    return f"{log.name}: n={len(shown)}s shown[{f(shown)}] game[{f(game)}] seconds<58: {under}"


def run_plan(a, plan_file: Path) -> int:
    """A JSON list of runs, all under one hold of the locks: [{"label","exe","state","seconds","cvar":[],"env":[]}...].
    Fields not given come from the command line. Runs go one after another with a pause between them."""
    plan = json.loads(plan_file.read_text())
    lab = Path(a.lab)
    results = []
    with machine_lock("scenario"), machine_lock("build"), machine_lock("app-build"), machine_lock("perf"):
        for item in plan:
            b = argparse.Namespace(**vars(a))
            for k, v in item.items():
                setattr(b, k, v)
            b.cvar = list(item.get("cvar", []))
            b.env = list(item.get("env", []))
            write_settings(lab, b.cvar)
            env = base_env(b)
            state = lab / "Save" / "states" / f"{b.state or b.name}.bwstate"
            if item.get("boot_rando"):
                # no state: the real file select, file 1 switching in place to its seed (as make-state --rando), then
                # the run; unpaced, so the three minutes of boot take seconds and fps is what the game can do
                env["BLUEWAKE_RANDO_INPLACE"] = "1"
                env["BLUEWAKE_WALL_PACE"] = "0"
                env["BLUEWAKE_PAD_SCRIPT"] = ",".join(f"{r}:{bt:#x}:{n}" for r, bt, n in [(1000, 0x1000, 4), (1900, 0x0100, 3), (3400, 0x0100, 3)])
                env.setdefault("BLUEWAKE_MAX_RETRACES", str(11100 + int(b.seconds) * 60 * 3))
            else:
                env.update({"BLUEWAKE_LOAD_STATE": str(state), "BLUEWAKE_LOAD_STATE_FORCE": "1", "BLUEWAKE_WALL_PACE": "1"})
            # the state's own retrace (windfall: 2701, windfall_rando: 11000) plus the run
            env.setdefault("BLUEWAKE_MAX_RETRACES", str(int(getattr(b, "base_retrace", 11100)) + int(b.seconds) * 60 + 3600))
            if getattr(b, "script", None):
                env["BLUEWAKE_PAD_SCRIPT"] = b.script
            log, info = run_game(b, env, b.label, locks=False)
            line = table(log, int(getattr(b, "skip", 8)))
            print(b.label, json.dumps(info), chr(10) + "  " + line, flush=True)
            results.append((b.label, info, line))
            time.sleep(5)
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("cmd", choices=["make-state", "run", "table", "plan"])
    p.add_argument("log", nargs="?")
    p.add_argument("--lab", default=str(scratch() / "lab_user"))
    p.add_argument("--exe")
    p.add_argument("--module")
    p.add_argument("--name", default="windfall")
    p.add_argument("--state")
    p.add_argument("--label", default="run")
    p.add_argument("--seconds", type=int, default=60)
    p.add_argument("--size", default="3440x1381")
    p.add_argument("--pos", default="0,0")
    p.add_argument("--cvar", action="append", default=[])
    p.add_argument("--env", action="append", default=[])
    p.add_argument("--script", help="extra BLUEWAKE_* pad script entries (retrace:buttons:len,...), after the state loads")
    p.add_argument("--timeout", type=int, default=900)
    p.add_argument("--skip", type=int, default=8)
    p.add_argument("--no-locks", action="store_true")
    p.add_argument("--rando", action="store_true", help="make-state: through the real file select, file 1 switching in place to its randomizer seed")
    a = p.parse_args()
    if a.cmd == "table":
        print(table(Path(a.log), a.skip))
        return 0
    lab = Path(a.lab)
    if a.cmd == "plan":
        return run_plan(a, Path(a.log))
    write_settings(lab, a.cvar)
    env = base_env(a)
    if a.cmd == "make-state":
        at = MAKE_SAVE_AT
        pads = MAKE_PADS
        if a.rando:
            # the real file select (scenarios/fs_inplace.txt): start at the title, A on file 1, A on its menu; the
            # session switches in place to the file's seed, and the game resumes from the file
            env["BLUEWAKE_RANDO_INPLACE"] = "1"
            pads = [(1000, 0x1000, 4), (1900, 0x0100, 3), (3400, 0x0100, 3)]
            at = 11000
        else:
            env["BLUEWAKE_NO_FILE_SELECT_UI"] = "1"
        env["BLUEWAKE_WALL_PACE"] = "0"
        env["BLUEWAKE_MAX_RETRACES"] = str(at + 60)
        env["BLUEWAKE_PAD_SCRIPT"] = ",".join(f"{r}:{b:#x}:{n}" for r, b, n in pads)
        env["BLUEWAKE_SAVE_STATE"] = f"{lab / 'Save' / 'states' / (a.name + '.bwstate')}@{at}"
        (lab / "Save" / "states").mkdir(parents=True, exist_ok=True)
        log, info = run_game(a, env, f"make-{a.name}", not a.no_locks)
        print(info, log)
        return 0
    state = lab / "Save" / "states" / f"{a.state or a.name}.bwstate"
    env.update({"BLUEWAKE_LOAD_STATE": str(state), "BLUEWAKE_LOAD_STATE_FORCE": "1", "BLUEWAKE_WALL_PACE": "1",
                "BLUEWAKE_MAX_RETRACES": str(a.seconds * 60 + 3600)})
    if a.script:
        env["BLUEWAKE_PAD_SCRIPT"] = a.script
    log, info = run_game(a, env, a.label, not a.no_locks)
    print(json.dumps(info), "\n" + table(log, a.skip))
    return 0


if __name__ == "__main__":
    sys.exit(main())
