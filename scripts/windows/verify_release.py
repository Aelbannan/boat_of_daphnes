#!/usr/bin/env python3
"""One-command release check: the release zip, verified the way the player plays it.

    python scripts/windows/verify_release.py PATH\\boat-of-daphnes-2026.10.10c-windows-x64.zip [--name rc1] [--only fresh_v1,fs_inplace]

What it does (all in <BOD_SCRATCH>\\rc_<name>, nothing of the checkout's build folder is used):
 1. unzips the release into a fresh folder, no Save;
 2. first launch with the disc pick (the player's first run, ISO from --iso), and checks that it prepared main.dol, the RELs,
    swift_sail, link_colors and the randomizer's RELs;
 3. runs the player-path matrix with that copy's own exe, module and prepared game folder, each path a pad-script scenario through
    the real UI (scripts/windows/scenarios/*.txt, with their data folders, cards and checkout paths rewritten into the rc folder):
      vanilla  fresh_b_v1 -> fresh_b_v2   new game, play, save, quit; relaunch, load, play
      rando    fresh_b_r1 -> fresh_b_r2   generate from the menu (music sequence), in-place new game, play, save; relaunch, load, play
      fs_inplace, fs_inplace_new, fs_rando_reset, music_inplace_gen, shop_labels
      roam     10 minutes on Windfall (day) and Outset from a lab save (a copy of a played card)
 4. writes summary.md / summary.json / index.html (a contact sheet per path) into the rc folder, prints the summary, and exits non-zero
    when any path failed.

Every scenario is its own scenario.py run, so the machine's scenario lock is taken and released per scenario, and the runner waits while
%LOCALAPPDATA%\\bluewake-perf.lock exists. A pass is: the scenario's expectations met, no `unmapped pc`, no PPCHalt, a clean exit.
Frame rates are recorded (min/mean of the log's [fps] lines), not judged.
"""
from __future__ import annotations

import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SCENARIOS = HERE / "scenarios"
SCRATCH = Path(os.environ.get("BLUEWAKE_SCRATCH", str(scratch())))
DEFAULT_ISO = r"D:\GCN\Favorites\Legend of Zelda, The - The Wind Waker (USA).iso"
LOCAL = Path(os.environ.get("LOCALAPPDATA") or Path.home())
PERF_LOCK = LOCAL / "bluewake-perf.lock"
SIZE = "1280x960"

# the order is the matrix: (id, scenario file, data folder key, timeout in seconds, what it proves)
PATHS = [
    ("vanilla_new", "fresh_b_v1", "van", 900, "vanilla: new game, play, save"),
    ("vanilla_load", "fresh_b_v2", "van", 900, "vanilla: relaunch, load, play"),
    ("rando_new", "fresh_b_r1", "rand", 1100, "randomizer: generate (music), in-place new game, play, save"),
    ("rando_load", "fresh_b_r2", "rand", 900, "randomizer: relaunch, load (in place), play"),
    ("fs_inplace", "fs_inplace", None, 900, "in place on and back off, vanilla file loads after"),
    ("fs_inplace_new", "fs_inplace_new", None, 900, "vanilla session, a NEW randomizer game in place, past Outset loading"),
    ("fs_rando_reset", "fs_rando_reset", None, 900, "WW's reset in a randomizer session, the other file loads"),
    ("music_inplace_gen", "music_inplace_gen", "music", 900, "Outset's music after generating and switching in place"),
    ("shop_labels", "shop_labels", None, 900, "the seed's message data after the in-place switch"),
    ("roam", None, None, 1500, "10 minutes free roam, Windfall by day then Outset, from the lab save"),
]
# a scenario's own fixed paths, rewritten into the rc folder
DATA_TOKENS = [("userdata10b_van", "ud_van"), ("userdata10b_music", "ud_music"), ("userdata10b", "ud_rand")]
R1_EXTRA = [r"expect \[rando\] file select sub-BGM 0x8000005D", r"expect \[music\] main BGM 0x8000002A"]
# The Bunny Hood from this package's own prepared files (11d shipped with a generated model the game could not use and
# its texts missing from the randomizer's messages): its model served from VamuY.arc and its texts placed, in a vanilla
# and a randomizer session.
HOOD_EXTRA = [r"expect \[bunny\] item model: file \d+ of VamuY\.arc", r"expect \[bunny\] messages in the file at 0x",
              r"expect_not \[bunny\] no VamuY\.arc or index", r"expect_not Hero's Charm's model is used instead"]
FAIL_PATTERNS = [r"\[run\] unmapped pc=", r"\[run\] stopped: unmapped", r"\[halt\] PPCHalt"]


# --- pure helpers (unit-tested) -------------------------------------------------------------------------------------------------------

def derive_scenario(text: str, rc: Path, name: str) -> tuple[str, list[tuple[Path, Path]]]:
    """The scenario's text with its data folders and card paths moved into the rc folder; returns the text and the cards to copy
    (source, destination)."""
    copies: list[tuple[Path, Path]] = []
    out = []
    for line in text.splitlines():
        if line.startswith("env BLUEWAKE_DATA_DIR "):
            value = line.split(" ", 2)[2]
            for token, folder in DATA_TOKENS:
                if value.rstrip("\\/").endswith(token):
                    line = f"env BLUEWAKE_DATA_DIR {rc / folder}"
                    break
        elif line.startswith("env BLUEWAKE_CARD_PATH "):
            src = Path(line.split(" ", 2)[2])
            dst = rc / "cards" / f"{name}_{src.name}"
            copies.append((src, dst))
            # A card's randomizer slots sidecar (X.slots.json next to X.card) says which files are randomizer files;
            # without it a randomizer file loads as vanilla.
            sidecar = src.with_suffix(".slots.json")
            if sidecar.is_file():
                copies.append((sidecar, dst.with_suffix(".slots.json")))
            line = f"env BLUEWAKE_CARD_PATH {dst}"
        out.append(line)
    idx = max((i for i, l in enumerate(out) if l.startswith("end")), default=len(out))
    if name == "rando_new":
        # append the music expectations before the final end line
        out[idx:idx] = R1_EXTRA
    if name in ("vanilla_new", "rando_new"):
        out[idx:idx] = HOOD_EXTRA
    return "\n".join(out) + "\n", copies


def analyze_log(text: str) -> dict:
    """What a run's log says: failures, switch lines, music ids, fps."""
    failures = [p for p in FAIL_PATTERNS if re.search(p, text)]
    fps = [float(m.group(1)) for m in re.finditer(r"^\[fps\] shown=([0-9.]+)", text, re.M) if float(m.group(1)) > 0]
    on = re.findall(r"is on while the game runs: mask=(0x[0-9A-F]+)", text)
    off = re.findall(r"is off while the game runs: mask=(0x[0-9A-F]+)", text)
    music = re.findall(r"^\[music\] main BGM (0x[0-9A-F]+) .*stage=(\w+)", text, re.M)
    return {
        "failures": failures,
        "fps_min": min(fps) if fps else None,
        "fps_mean": round(sum(fps) / len(fps), 1) if fps else None,
        "fps_lines": len(fps),
        "switch_on": on,
        "switch_off": off,
        "stages": len(re.findall(r"^\[scene\] stage=.*\(stage change\)", text, re.M)),
        "outset_bgm": next((m[0] for m in music if m[1] == "sea"), None),
    }


def excerpt(text: str, patterns: list[str], limit: int = 8) -> list[str]:
    lines = []
    for line in text.splitlines():
        if any(re.search(p, line) for p in patterns):
            lines.append(line[:200])
            if len(lines) >= limit:
                break
    return lines


def roam_scenario(rc: Path, lab: Path) -> tuple[str, list[tuple[Path, Path]]]:
    """Ten minutes (36000 retraces) of walking: the lab save's file 1 loads through the file select (in place), then Windfall by day for
    half the time and Outset for the other half. The time of day is held (the poke is made again every few hundred retraces)."""
    card_src = lab / "Save" / "GZLE01.card"
    card_dst = rc / "cards" / "roam_lab.card"
    copies = [(card_src, card_dst)]
    sidecar = lab / "Save" / "GZLE01.slots.json"
    if sidecar.is_file():
        copies.append((sidecar, rc / "cards" / "roam_lab.slots.json"))
    day = "poke 0x803C4C2C=0x43340000/4"
    lines = [
        "description Release check: 10 minutes of free roam on Windfall (day) and Outset from the lab save.",
        "start boot raw", f"size {SIZE}",
        f"env BLUEWAKE_CARD_PATH {card_dst}",
        f"env BLUEWAKE_RANDOMIZER_DIR {rc / 'lab_rz'}",
        "wait 1000", "press start 4", "wait 1500", "press A 3", "wait 3000", "press A 6", "wait 2500", "press A 6", "wait 3000",
        "press A 6", "wait 6000", "shot",
    ]
    for place, teleport in (("windfall", "teleport sea 11 -2531 176 -199682"), ("outset", "teleport sea 44 -200242 3720 321928")):
        lines += [teleport, "wait 400", day, "shot"]
        for i in range(36):  # 36 x 500 retraces = 18000 = 5 minutes
            lines += [day, "stick 0 100 120", "stick 100 0 80", "wait 100", "stick 0 -100 100", "stick -100 0 80", "wait 100",
                      "press A 2"]
            if i % 9 == 8:
                lines.append("shot")
    lines += ["end"]
    return "\n".join(lines) + "\n", copies


def render_summary(name: str, zip_path: str, results: list[dict], first: dict) -> str:
    out = [f"# Release check {name}", f"zip: {zip_path}", f"first launch: {'PASS' if first['passed'] else 'FAIL'} {first['detail']}", ""]
    out.append("| path | result | stages | switches on/off | Outset BGM | fps min/mean (lines) | note |")
    out.append("|---|---|---|---|---|---|---|")
    for r in results:
        a = r.get("analysis", {})
        out.append("| {id} | {res} | {st} | {on}/{off} | {bgm} | {fmin}/{fmean} ({fl}) | {note} |".format(
            id=r["id"], res="PASS" if r["passed"] else "FAIL", st=a.get("stages", ""), on=len(a.get("switch_on", [])),
            off=len(a.get("switch_off", [])), bgm=a.get("outset_bgm") or "", fmin=a.get("fps_min"), fmean=a.get("fps_mean"),
            fl=a.get("fps_lines", 0), note=r.get("note", "")))
    out.append("")
    for r in results:
        if r["passed"]:
            continue
        out.append(f"## FAIL {r['id']}: {r['why']}")
        out += ["```"] + r.get("excerpt", []) + ["```", ""]
    failed = [r["id"] for r in results if not r["passed"]]
    out.append(f"RESULT: {'PASS' if not failed and first['passed'] else 'FAIL'} ({len(results) - len(failed)}/{len(results)} paths"
               + (f"; failed: {', '.join(failed)}" if failed else "") + ")")
    return "\n".join(out) + "\n"


# --- the run ---------------------------------------------------------------------------------------------------------------------------

def wait_perf_lock(limit: int = 4 * 3600) -> None:
    started = time.time()
    announced = False
    while PERF_LOCK.exists() and time.time() - started < limit:
        if not announced:
            print(f"waiting: {PERF_LOCK} exists (the perf agent is measuring)", flush=True)
            announced = True
        time.sleep(15)


def unzip_release(zip_path: Path, rc: Path) -> Path:
    folder = rc / "pkg"
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(folder)
    entries = [p for p in folder.iterdir()]
    if len(entries) == 1 and entries[0].is_dir():  # a zip with one top folder
        return entries[0]
    return folder


def first_launch(pkg: Path, iso: str, rc: Path) -> dict:
    """The player's first run: the exe with the disc, in the unzipped folder (its own Save, cache and settings)."""
    sys.path.insert(0, str(HERE))
    from scenario import Lock  # the machine's scenario lock

    for stale in ("Save", "cache", "logs"):
        shutil.rmtree(pkg / stale, ignore_errors=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(("BLUEWAKE_", "DOL_", "LLVM_PROFILE_"))}
    env.update({
        "BLUEWAKE_WINDOW_POS": os.environ.get("BLUEWAKE_TEST_WINDOW_POS", "4060,40"), "BLUEWAKE_WINDOW_NOACTIVATE": "1",
        "SDL_AUDIO_DRIVER": "dummy", "BLUEWAKE_MAX_RETRACES": "1500", "BLUEWAKE_WALL_PACE": "0", "BLUEWAKE_LIVE_PAD": "0",
        "DOL_AURORA_WINDOW": SIZE, "DOL_AURORA_RENDER_SCALE": "0", "BLUEWAKE_NO_DIALOG": "1",
    })
    log = rc / "first_launch.log"
    wait_perf_lock()
    with Lock():
        with open(log, "wb") as stream:
            try:
                code = subprocess.call([str(pkg / "boat-of-daphnes.exe"), "--disc", iso], env=env, stdout=stream,
                                       stderr=subprocess.STDOUT, timeout=900, cwd=str(pkg))
            except subprocess.TimeoutExpired:
                code = "timeout"
    prepared = sorted((pkg / "cache").glob("disc-import-*"))
    if not prepared:
        return {"passed": False, "detail": f"exit {code}: no prepared folder in cache", "game": None, "log": str(log)}
    game = prepared[-1]
    need = {
        "main.dol": game / "main.dol", "rels/": game / "rels", "swift_sail": game / "assets" / "swift_sail",
        "link_colors": game / "assets" / "link_colors", "rando rels": game / "rando" / "rels",
    }
    missing = [k for k, p in need.items() if not (p.is_file() if k == "main.dol" else (p.is_dir() and any(p.iterdir())))]
    rels = len(list((game / "rels").glob("*.rel"))) if (game / "rels").is_dir() else 0
    rando_rels = len(list((game / "rando" / "rels").glob("*.rel"))) if (game / "rando" / "rels").is_dir() else 0
    detail = f"exit {code}; prepared {game.name}: {rels} rels, {rando_rels} rando rels" + (f"; MISSING {missing}" if missing else "")
    return {"passed": not missing and code == 0, "detail": detail, "game": game, "log": str(log)}


def run_scenario(pkg: Path, game: Path, rc: Path, iso: str, path: Path, timeout: int) -> tuple[int, Path]:
    wait_perf_lock()
    out = rc / "out"
    # The runs name the game files directly (--game), so the app does not find its prepared folder on its own as a
    # player's launch does; without this they load none of its extras (the Bunny Hood, Link's colours, the Swift Sail)
    # and the check could not see a broken one (11d).
    cmd = [sys.executable, str(HERE / "scenario.py"), str(path), "--out", str(out), "--game", str(game),
           "--env", f"BLUEWAKE_EXTRAS_DIR={game}",
           "--exe", str(pkg / "boat-of-daphnes.exe"), "--module", str(pkg / "gGZLE01_recomp.dll"), "--iso", iso,
           "--timeout", str(timeout)]
    code = subprocess.call(cmd, cwd=str(ROOT))
    return code, out / "scenarios" / path.stem / SIZE


def check_one(spec, pkg: Path, game: Path, rc: Path, iso: str, lab: Path) -> dict:
    pid, scenario, data, timeout, what = spec
    sdir = rc / "scenarios"
    sdir.mkdir(exist_ok=True)
    if scenario is None:
        text, copies = roam_scenario(rc, lab)
        if not (rc / "lab_rz").exists() and (lab / "Randomizer").is_dir():
            shutil.copytree(lab / "Randomizer", rc / "lab_rz")
    else:
        text, copies = derive_scenario((SCENARIOS / f"{scenario}.txt").read_text(encoding="utf-8"), rc, pid)
    for src, dst in copies:
        if dst.exists():
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        if src.is_file():
            shutil.copy2(src, dst)
    if pid in ("vanilla_new", "rando_new", "music_inplace_gen"):
        shutil.rmtree(rc / {"vanilla_new": "ud_van", "rando_new": "ud_rand", "music_inplace_gen": "ud_music"}[pid], ignore_errors=True)
    path = sdir / f"{pid}.txt"
    path.write_text(text, encoding="utf-8")
    code, rdir = run_scenario(pkg, game, rc, iso, path, timeout)
    res: dict = {"id": pid, "what": what, "exit": code, "dir": str(rdir)}
    log = (rdir / "run.log").read_text(encoding="utf-8", errors="replace") if (rdir / "run.log").is_file() else ""
    result = json.loads((rdir / "result.json").read_text()) if (rdir / "result.json").is_file() else {}
    analysis = analyze_log(log)
    res["analysis"] = analysis
    unmet = [e.get("log") or ("not: " + e.get("not_log", "")) for e in result.get("expect", []) if ("log" in e) != e["found"]]
    passed = bool(result.get("passed")) and code == 0 and not analysis["failures"]
    res["passed"] = passed
    if not passed:
        res["why"] = (f"scenario exit {code}; " + (f"failures {analysis['failures']}; " if analysis["failures"] else "")
                      + (f"expectations not met: {unmet}" if unmet else ("no result.json" if not result else "")))
        res["excerpt"] = excerpt(log, [r"unmapped", r"PPCHalt", r"\[mods\]", r"\[music\] main", r"\[randomizer\] (session|file)"], 14)
    sheet = rdir / "sheet.png"
    if sheet.is_file():
        (rc / "sheets").mkdir(exist_ok=True)
        shutil.copy2(sheet, rc / "sheets" / f"{pid}.png")
        res["sheet"] = f"sheets/{pid}.png"
    return res


def write_reports(rc: Path, name: str, zip_path: str, results: list[dict], first: dict) -> str:
    summary = render_summary(name, zip_path, results, first)
    (rc / "summary.md").write_text(summary, encoding="utf-8")
    (rc / "summary.json").write_text(json.dumps({"first_launch": {k: str(v) for k, v in first.items()}, "paths": results}, indent=1,
                                                default=str), encoding="utf-8")
    cells = []
    for r in results:
        a = r.get("analysis", {})
        img = f'<img src="{r["sheet"]}" style="max-width:100%">' if r.get("sheet") else "(no frames)"
        cells.append(f'<h3>{html.escape(r["id"])}: {"PASS" if r["passed"] else "FAIL"}</h3><p>{html.escape(r["what"])} - fps {a.get("fps_min")}/'
                     f'{a.get("fps_mean")}</p>{img}')
    (rc / "index.html").write_text("<!doctype html><meta charset=utf-8><title>Release check " + html.escape(name) + "</title>"
                                   "<body style='font-family:sans-serif;background:#111;color:#eee'>"
                                   f"<h1>Release check {html.escape(name)}</h1><pre>{html.escape(summary)}</pre>" + "".join(cells), encoding="utf-8")
    return summary


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("zip", type=Path)
    ap.add_argument("--name", help="rc folder suffix (default: from the zip's name)")
    ap.add_argument("--iso", default=DEFAULT_ISO)
    ap.add_argument("--lab", type=Path, default=SCRATCH / "lab_user", help="a copy of a played game folder (its Save is the roam's card)")
    ap.add_argument("--only", help="comma-separated path ids to run (the first launch always runs unless --reuse)")
    ap.add_argument("--reuse", action="store_true", help="use the rc folder's existing unzip and prepared game")
    args = ap.parse_args()
    if not args.zip.is_file():
        print(f"no such zip: {args.zip}")
        return 2
    name = args.name or re.sub(r"^boat-of-daphnes-|-windows-x64$", "", args.zip.stem)
    rc = SCRATCH / f"rc_{name}"
    rc.mkdir(parents=True, exist_ok=True)
    if args.reuse and (rc / "pkg").is_dir():
        entries = list((rc / "pkg").iterdir())
        pkg = entries[0] if len(entries) == 1 and entries[0].is_dir() else rc / "pkg"
        prepared = sorted((pkg / "cache").glob("disc-import-*"))
        first = {"passed": bool(prepared), "detail": "reused", "game": prepared[-1] if prepared else None}
    else:
        pkg = unzip_release(args.zip, rc)
        print(f"unzipped to {pkg}", flush=True)
        first = first_launch(pkg, args.iso, rc)
        print(f"first launch: {'PASS' if first['passed'] else 'FAIL'} {first['detail']}", flush=True)
    if first.get("game") is None:
        write_reports(rc, name, str(args.zip), [], first)
        return 1
    only = set(args.only.split(",")) if args.only else None
    results = []
    for spec in PATHS:
        if only and spec[0] not in only:
            continue
        print(f"--- {spec[0]}: {spec[4]}", flush=True)
        try:
            r = check_one(spec, pkg, first["game"], rc, args.iso, args.lab)
        except Exception as e:  # a broken harness step is a failure of that path, and the run goes on
            r = {"id": spec[0], "what": spec[4], "passed": False, "why": f"harness error: {e!r}", "excerpt": [], "analysis": {}}
        print(f"    {'PASS' if r['passed'] else 'FAIL'}" + ("" if r["passed"] else f" {r['why']}"), flush=True)
        results.append(r)
    summary = write_reports(rc, name, str(args.zip), results, first)
    print("\n" + summary)
    print(f"report: {rc / 'summary.md'}  sheets: {rc / 'index.html'}")
    return 0 if first["passed"] and all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
