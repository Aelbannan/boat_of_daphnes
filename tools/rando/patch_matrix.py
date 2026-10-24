#!/usr/bin/env python3
"""Run patch_game.py (and generate_seed.py) over a matrix of seeds and options, for classify_patch.py.

The matrix (see CONFIGS) is built to answer one question per byte of every file wwrando changes: does it depend on
the seed, on the options, or on neither?

    base_1..3   default options, three seeds                     (seed variation of the default option set)
    qol_1..2    all quality-of-life options flipped, two seeds   (seed variation under those options)
    game_1..2   swordless, hero mode, trap chests, ...           (same)
    rand_1..2   entrance, starting-island, chart randomisation   (same)
    opt_*       one option changed at a time, seed 1             (which option a difference comes from)

    python scripts/rando/patch_matrix.py --work DIR [--iso DISC] [--jobs 2] [--only NAME ...]

Each run lands in DIR/runs/NAME/: patch_manifest.json, seed.json, and the patched main.dol and RELs (not the
stage archives: their per-file SHA-1s are in the manifest). Runs that are already complete are skipped.
Runs under any Python 3 (it only launches the other two scripts).
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
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
DEFAULT_ISO = local_path("BOD_DISC")

SEEDS = ["RV1", "RV2", "RV3"]

QOL = {"swift_sail": False, "instant_text_boxes": False, "reveal_full_sea_chart": False, "skip_rematch_bosses": False,
       "remove_title_and_ending_videos": False, "add_shortcut_warps_between_dungeons": True,
       "invert_camera_x_axis": True, "invert_sea_compass_x_axis": True, "remove_music": True,
       "switch_targeting_mode": True}
GAME = {"sword_mode": "Swordless", "hero_mode": True, "chest_type_matches_contents": True, "trap_chests": True,
        "required_bosses": True, "keylunacy": True}
RAND = {"randomize_dungeon_entrances": True, "randomize_secret_cave_entrances": True,
        "randomize_miniboss_entrances": True, "randomize_boss_entrances": True,
        "randomize_secret_cave_inner_entrances": True, "randomize_fairy_fountain_entrances": True,
        "randomize_starting_island": True, "randomize_charts": True, "num_starting_triforce_shards": 2,
        "starting_pohs": 3, "num_extra_starting_items": 2}

SINGLES = {
    "sword_mode=No Starting Sword": {"sword_mode": "No Starting Sword"},
    "sword_mode=Swordless": {"sword_mode": "Swordless"},
    "hero_mode": {"hero_mode": True},
    "swift_sail=off": {"swift_sail": False},
    "instant_text_boxes=off": {"instant_text_boxes": False},
    "reveal_full_sea_chart=off": {"reveal_full_sea_chart": False},
    "skip_rematch_bosses=off": {"skip_rematch_bosses": False},
    "remove_title_and_ending_videos=off": {"remove_title_and_ending_videos": False},
    "add_shortcut_warps_between_dungeons": {"add_shortcut_warps_between_dungeons": True},
    "invert_camera_x_axis": {"invert_camera_x_axis": True},
    "invert_sea_compass_x_axis": {"invert_sea_compass_x_axis": True},
    "remove_music": {"remove_music": True},
    "switch_targeting_mode": {"switch_targeting_mode": True},
    "chest_type_matches_contents": {"chest_type_matches_contents": True},
    "trap_chests": {"trap_chests": True},
    "keylunacy": {"keylunacy": True},
    "required_bosses": {"required_bosses": True},
    "randomize_dungeon_entrances": {"randomize_dungeon_entrances": True},
    "randomize_secret_cave_entrances": {"randomize_secret_cave_entrances": True},
    "randomize_miniboss_entrances": {"randomize_miniboss_entrances": True},
    "randomize_boss_entrances": {"randomize_boss_entrances": True},
    "randomize_fairy_fountain_entrances": {"randomize_fairy_fountain_entrances": True},
    "randomize_secret_cave_inner_entrances": {"randomize_secret_cave_entrances": True,
                                              "randomize_secret_cave_inner_entrances": True},
    "randomize_starting_island": {"randomize_starting_island": True},
    "randomize_charts": {"randomize_charts": True},
    "fishmen_hints=off": {"fishmen_hints": False},
    "hoho_hints=off": {"hoho_hints": False},
    "korl_hints=on": {"korl_hints": True},
    "num_starting_triforce_shards=3": {"num_starting_triforce_shards": 3},
    "starting_pohs=4": {"starting_pohs": 4},
    "starting_hcs=0": {"starting_hcs": 0},
    "num_extra_starting_items=2": {"num_extra_starting_items": 2},
    "starting_gear+hookshot": {"starting_gear": ["Ballad of Gales", "Progressive Magic Meter", "Progressive Shield",
                                                 "Song of Passing", "Hookshot"]},
    "progression_savage_labyrinth": {"progression_savage_labyrinth": True},
    "progression_long_sidequests": {"progression_long_sidequests": True, "progression_short_sidequests": True},
    "progression_treasure_charts": {"progression_treasure_charts": True, "progression_triforce_charts": True},
    "player_in_casual_clothes": {"player_in_casual_clothes": True},
    "custom_color_preset": {"custom_color_preset": "Random"},
    "do_not_generate_spoiler_log": {"do_not_generate_spoiler_log": True},
}


def configs() -> list[dict]:
    out = []
    for i, seed in enumerate(SEEDS, 1):
        out.append({"name": f"base_{i}", "group": "base", "seed": seed, "options": {}})
    for group, opts in (("qol", QOL), ("game", GAME), ("rand", RAND)):
        for i, seed in enumerate(SEEDS[:2], 1):
            out.append({"name": f"{group}_{i}", "group": group, "seed": seed, "options": dict(opts)})
    for label, opts in SINGLES.items():
        out.append({"name": "opt_" + label.replace("=", "-").replace("+", "_plus_"), "group": "single",
                    "seed": SEEDS[0], "options": dict(opts), "label": label})
    return out


def run_one(cfg: dict, work: Path, iso: str) -> tuple[str, bool, str]:
    out = work / "runs" / cfg["name"]
    done = out / "done.json"
    if done.is_file() and json.loads(done.read_text()) == cfg:
        return cfg["name"], True, "cached"
    out.mkdir(parents=True, exist_ok=True)
    opts = out / "options.json"
    opts.write_text(json.dumps(cfg["options"]), encoding="utf-8")
    common = ["--seed", cfg["seed"], "--options", str(opts)]
    logs = []
    for script, extra in (("generate_seed.py", ["--out", str(out)]),
                          ("patch_game.py", ["--out", str(out), "--iso", iso, "--files", "code"])):
        r = subprocess.run([sys.executable, str(HERE / script), *common, *extra], capture_output=True, text=True)
        logs.append(f"== {script} rc={r.returncode}\n{r.stdout[-2000:]}\n{r.stderr[-3000:]}")
        if r.returncode != 0:
            (out / "failed.log").write_text("\n".join(logs), encoding="utf-8")
            return cfg["name"], False, logs[-1][-600:]
    done.write_text(json.dumps(cfg), encoding="utf-8")
    return cfg["name"], True, "ok"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", required=True)
    ap.add_argument("--iso", default=DEFAULT_ISO)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--only", nargs="*")
    args = ap.parse_args()
    work = Path(args.work).resolve()
    cfgs = [c for c in configs() if not args.only or c["name"] in args.only]
    (work).mkdir(parents=True, exist_ok=True)
    (work / "configs.json").write_text(json.dumps(configs(), indent=1), encoding="utf-8")
    bad = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for name, ok, msg in ex.map(lambda c: run_one(c, work, args.iso), cfgs):
            print(f"{'ok  ' if ok else 'FAIL'} {name}: {msg if not ok else msg}", flush=True)
            bad += not ok
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
