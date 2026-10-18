#!/usr/bin/env python3
"""Headless Wind Waker Randomizer seed generation (milestone R1): options + seed in, seed pack + logs out.

Runs wwrando's own generator with dry_run=True: no ISO, no game files, no Qt. Writes into --out:

    seed.json                                  the seed pack (schema documented in docs/fork/research/rando-seedgen.md)
    WW Random <seed> - Spoiler Log.txt         wwrando's spoiler log (not written when do_not_generate_spoiler_log is on)
    WW Random <seed> - Non-Spoiler Log.txt     wwrando's non-spoiler log
    WW Random <seed> - Error Log.txt           only on failure

Usage (any Python 3 can launch it; it re-runs itself under the embedded Python 3.12 runtime):

    python scripts/rando/generate_seed.py --out DIR --seed SEED [--options opts.json] [--set name=value ...]
    python scripts/rando/generate_seed.py --out DIR --permalink PERMALINK

stdout gets exactly one JSON object (the result); progress and wwrando's chatter go to stderr.
Exit codes: 0 ok, 2 bad arguments / options / permalink, 3 generation failed (e.g. fill dead end, too few
progression locations), 4 runtime missing or internal error.

Fetch the runtime first: python scripts/windows/rando_runtime.py
"""
from __future__ import annotations

import argparse
import contextlib
import json
import os
import subprocess
import sys
import time
import traceback
from collections import defaultdict
from pathlib import Path

SCHEMA = "bod.seedpack"
SCHEMA_VERSION = 1

REPO = Path(__file__).resolve().parents[2]
RUNTIME = Path(os.environ.get("RANDO_RUNTIME") or REPO / "build" / "windows" / "rando_runtime")

EXIT_OK, EXIT_ARGS, EXIT_GENERATION, EXIT_RUNTIME = 0, 2, 3, 4


def fail(code: int, message: str, **extra) -> "NoReturn":
    print(json.dumps({"ok": False, "error": message, **extra}), flush=True)
    print(f"generate_seed: {message}", file=sys.stderr, flush=True)
    sys.exit(code)


def relaunch_under_runtime() -> None:
    """The generator needs Python 3.12 (wwrando breaks on newer versions); hand over to the embedded one."""
    py = RUNTIME / "python" / "python.exe"
    if not py.is_file():
        fail(EXIT_RUNTIME, f"rando runtime not found at {RUNTIME}; run: python scripts/windows/rando_runtime.py")
    r = subprocess.run([str(py), str(Path(__file__).resolve()), *sys.argv[1:]])
    sys.exit(r.returncode)


if sys.version_info[:2] != (3, 12):
    relaunch_under_runtime()

# --- running under the embedded 3.12 from here on; its ._pth already lists wwrando, gclib and site-packages ---
for _p in (RUNTIME / "wwrando", RUNTIME / "gclib", RUNTIME / "site-packages"):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))


def parse_args(argv):
    ap = argparse.ArgumentParser(description="Generate a Wind Waker Randomizer seed pack without an ISO.")
    ap.add_argument("--out", required=True, help="output folder (created)")
    ap.add_argument("--seed", help="seed string (wwrando sanitises it: letters, digits and -_'%%. only, 42 chars max); "
                    "a random seed name is made when neither --seed nor --permalink is given")
    ap.add_argument("--permalink", help="wwrando permalink; supplies the seed and the permalink options")
    ap.add_argument("--options", help="JSON file: an object of wwrando option names to values (see docs); others stay default")
    ap.add_argument("--set", action="append", default=[], metavar="NAME=VALUE",
                    help="one option as JSON or a bare string, e.g. keylunacy=true sword_mode=Swordless; applied after --options")
    return ap.parse_args(argv)


def parse_set(items):
    out = {}
    for item in items:
        if "=" not in item:
            raise ValueError(f"--set expects NAME=VALUE, got {item!r}")
        name, raw = item.split("=", 1)
        try:
            out[name] = json.loads(raw)
        except json.JSONDecodeError:
            out[name] = raw
    return out


def build_options(Options, user_options: dict):
    """Defaults plus the given values, checked: unknown names raise, enum strings are converted by validate()."""
    from wwr_ui.inventory import REGULAR_ITEMS, PROGRESSIVE_ITEMS

    options = Options()
    known = Options.by_name()
    for name, value in user_options.items():
        if name not in known:
            raise ValueError(f"unknown option {name!r}")
        options[name] = value
    if "starting_gear" in user_options and "randomized_gear" not in user_options:
        # The UI derives randomized_gear as the complement of starting_gear; do the same.
        pool = list(REGULAR_ITEMS) + list(PROGRESSIVE_ITEMS)
        for item in options.starting_gear:
            if item not in pool:
                raise ValueError(f"starting_gear contains an unknown or surplus item: {item!r}")
            pool.remove(item)
        options.randomized_gear = sorted(pool)
    options.validate()
    return options


def reject_unsupported(options) -> None:
    if options.randomize_enemies:
        # wwrando hides this option outside source runs and flags it unbeatable; its result (8,000 lines of
        # enemy placements applied to stage files) is not part of the seed pack.
        raise ValueError("randomize_enemies is not supported (hidden and unbeatable in wwrando; no seed pack data for it)")


# ---------------------------------------------------------------------------------------------------
# Hint messages without the disc font
# ---------------------------------------------------------------------------------------------------

class _Msg:
    """Stand-in for gclib's BMG message: records the unwrapped text instead of word-wrapping with the disc font."""

    def __init__(self):
        self.string = ""
        self.parts = None

    def word_wrap_string(self, font, extra_line_length=0):
        pass  # needs rock_24_20_4i_usa.bfn from the disc; text stays unwrapped

    def construct_string_from_parts(self, font, parts, extra_line_length=0):
        self.parts = list(parts)
        self.string = "".join(parts)


class _Bmg:
    def __init__(self):
        self.messages_by_id = defaultdict(_Msg)


# Item ids that wwrando only registers while patching the game (tweaks.py: make_items_progressive,
# allow_dungeon_items_to_appear_anywhere, make_tingle_statue_reward_rupee_rainbow_colored), which a dry run skips.
# Copied from wwrando at the pinned commit; make_pack fails loudly if any placed item still has no id.
RENAMED_ITEMS = [
    (0x38, "Progressive Sword"), (0x3B, "Progressive Shield"), (0x27, "Progressive Bow"), (0xAB, "Progressive Wallet"),
    (0xAD, "Progressive Bomb Bag"), (0xAF, "Progressive Quiver"), (0x23, "Progressive Picto Box"),
    (0xB1, "Progressive Magic Meter"), (0xB8, "Rainbow Rupee"),
    (0x13, "DRC Small Key"), (0x14, "DRC Big Key"), (0x1B, "DRC Dungeon Map"), (0x1C, "DRC Compass"),
    (0x1D, "FW Small Key"), (0x40, "FW Big Key"), (0x41, "FW Dungeon Map"), (0x5A, "FW Compass"),
    (0x5B, "TotG Small Key"), (0x5C, "TotG Big Key"), (0x5D, "TotG Dungeon Map"), (0x5E, "TotG Compass"),
    (0x5F, "FF Dungeon Map"), (0x60, "FF Compass"),
    (0x73, "ET Small Key"), (0x74, "ET Big Key"), (0x75, "ET Dungeon Map"), (0x76, "ET Compass"),
    (0x77, "WT Small Key"), (0x81, "WT Big Key"), (0x84, "WT Dungeon Map"), (0x85, "WT Compass"),
]


# ---------------------------------------------------------------------------------------------------
# The Bunny Hood (docs/fork/bunny-hood.md): our own item, id 0x44, one copy in the pool like the Hero's Charm
# ---------------------------------------------------------------------------------------------------

BUNNY_HOOD = (0x44, "Bunny Hood")
BUNNY_HOOD_OPTION = "bunny_hood"  # a key of the options JSON / --set, not one of wwrando's options
BUNNY_HOOD_PERMALINK = "~BH"      # appended to wwrando's permalink: wwrando knows nothing of the item, so the flag rides after it


def take_bunny_hood(user_options: dict, permalink: str | None = None):
    """Pops our option from the given options and our mark from the permalink. Returns (in the pool, starting gear, options, permalink)."""
    on = bool(user_options.pop(BUNNY_HOOD_OPTION, False))
    if permalink and permalink.strip().endswith(BUNNY_HOOD_PERMALINK):
        permalink = permalink.strip()[: -len(BUNNY_HOOD_PERMALINK)]
        on = True
    # The Starting Items list may hold the hood: wwrando does not know it, so it is taken out of the list here and
    # granted through the pack's starting gear (make_pack); it then is not also placed in the pool.
    start = False
    gear = user_options.get("starting_gear")
    if isinstance(gear, list) and BUNNY_HOOD[1] in gear:
        user_options["starting_gear"] = [g for g in gear if g != BUNNY_HOOD[1]]
        start = True
    return on, start, user_options, permalink


def add_bunny_hood_to_pool() -> None:
    """wwrando builds each seed's pool from logic.item_types.NONPROGRESS_ITEMS (copied when the randomizer is made):
    one more entry is one more non-progress item, placed at a random reachable location like the Hero's Charm."""
    from logic import item_types
    if BUNNY_HOOD[1] not in item_types.NONPROGRESS_ITEMS:
        item_types.NONPROGRESS_ITEMS.append(BUNNY_HOOD[1])


def register_bunny_hood(rando) -> None:
    rando.register_renamed_item(*BUNNY_HOOD)


def snake(enum_member):
    return None if enum_member is None else enum_member.name.lower()


def make_pack(rando, ctx) -> dict:
    """Read every randomizer's result off a finished dry-run WWRandomizer into the seed pack."""
    from randomizers import entrances as ent_mod
    from randomizers.boss_reqs import BOSS_NAME_TO_SEA_CHART_QUEST_MARKER_INDEX, BOSS_NAME_TO_STAGE_ID
    from randomizers.hints import HintType
    from options.wwrando_options import SwordMode
    from version import VERSION

    logic = rando.logic
    options = rando.options
    for item_id, item_name in RENAMED_ITEMS:
        rando.register_renamed_item(item_id, item_name)

    # ---- locations: file order of item_locations.txt, id = index ----
    location_names = ctx["location_names"]
    location_id = {name: i for i, name in enumerate(location_names)}
    done = logic.done_item_locations
    missing = [n for n in location_names if n not in done]
    if missing:
        raise RuntimeError(f"{len(missing)} locations were left without an item, e.g. {missing[:3]}")
    progress_locations, _ = logic.get_progress_and_non_progress_locations()
    progress_set = set(progress_locations)
    locations = []
    for i, name in enumerate(location_names):
        item = done[name]
        is_trap = item.endswith(" Trap Chest")
        entry = {
            "id": i,
            "name": name,
            "item": item,
            "item_id": None if is_trap else rando.item_name_to_id[item],
            "trap_chest": is_trap,
            "progress_location": name in progress_set,
            "types": list(logic.item_locations[name]["Types"]),
        }
        # Chest colour when "chest type matches contents" is on (applies to locations that are chests).
        entry["chest_type"] = rando.items.get_ctmc_chest_type_for_item(item) if options.chest_type_matches_contents else None
        locations.append(entry)

    # ---- entrances ----
    er = rando.entrances

    def entrance_info(name):
        e = ent_mod.ZoneEntrance.all[name]
        return {"stage": e.stage_name, "room": e.room_num, "scls_exit_index": e.scls_exit_index, "spawn_id": e.spawn_id,
                "island": e.island_name, "warp_out_stage": e.warp_out_stage_name, "warp_out_room": e.warp_out_room_num,
                "warp_out_spawn_id": e.warp_out_spawn_id, "nested_in_exit": e.nested_in.unique_name if e.nested_in else None}

    def exit_info(name):
        x = ent_mod.ZoneExit.all[name]
        return {"stage": x.stage_name, "room": x.room_num, "scls_exit_index": x.scls_exit_index, "spawn_id": x.spawn_id,
                "boss_stage": x.boss_stage_name, "zone": x.zone_name}

    entrances = {
        "randomized": er.is_enabled(),
        "nested_enabled": er.nesting_enabled,
        "connections": [{"entrance": e, "exit": x} for e, x in er.entrance_connections.items()],
        "nested_paths": [list(p) for p in er.nested_entrance_paths],
        "entrance_table": {n: entrance_info(n) for n in er.entrance_connections},
        "exit_table": {x: exit_info(x) for x in er.entrance_connections.values()},
    }

    # ---- charts ----
    charts_rand = rando.charts
    charts = {
        "randomized": charts_rand.is_enabled(),
        "islands": [
            {"island": n, "island_name": rando.island_number_to_name[n], "chart": charts_rand.island_number_to_chart_name[n],
             "chart_item_id": rando.item_name_to_id[charts_rand.island_number_to_chart_name[n]]}
            for n in range(1, 50)
        ],
    }

    # ---- required bosses ----
    br = rando.boss_reqs
    bosses = []
    if br.is_enabled():
        for loc in logic.item_locations:
            if "Boss" not in logic.item_locations[loc]["Types"]:
                continue
            dungeon, specific = logic.split_location_name_by_zone(loc)
            boss = specific.removesuffix(" Heart Container")
            bosses.append({"boss": boss, "dungeon": dungeon, "location_id": location_id[loc], "required": boss in br.required_bosses,
                           "quest_marker_index": BOSS_NAME_TO_SEA_CHART_QUEST_MARKER_INDEX[boss],
                           "stage_id": BOSS_NAME_TO_STAGE_ID[boss]})
    required_bosses = {
        "enabled": br.is_enabled(),
        "bosses": bosses,
        "required_dungeons": list(br.required_dungeons),
        "banned_dungeons": list(br.banned_dungeons),
        "banned_location_ids": [location_id[n] for n in br.banned_locations],
    }

    # ---- starting state ----
    gear = list(options.starting_gear) + list(rando.extra_start_items.random_starting_items)
    magic_meters = gear.count("Progressive Magic Meter")
    gear_no_magic = [g for g in gear if g != "Progressive Magic Meter"]
    sword_mode_value = {SwordMode.START_WITH_SWORD: 0, SwordMode.NO_STARTING_SWORD: 1, SwordMode.SWORDLESS: 2}[options.sword_mode]
    starting = {
        "logic_items": list(rando.starting_items) + list(rando.extra_start_items.random_starting_items),
        "extra_random_items": list(rando.extra_start_items.random_starting_items),
        "gear": [{"item": g, "item_id": rando.item_name_to_id[g]} for g in gear_no_magic],
        "island": {"randomized": rando.starting_island.is_enabled(), "room": rando.starting_island.room_number,
                   "name": rando.island_number_to_name[rando.starting_island.room_number], "spawn_id": 0},
        # Mirrors what wwrando's tweaks write into the DOL's custom symbols (tweaks.py, asm/custom_symbols.txt).
        "game_variables": {
            "sword_mode": sword_mode_value,
            "num_triforce_shards_to_start_with": options.num_starting_triforce_shards,
            "starting_quarter_hearts": options.starting_hcs * 4 + options.starting_pohs,
            "starting_magic": 16 * magic_meters,
            "skip_rematch_bosses": int(options.skip_rematch_bosses),
            "damage_multiplier": 4.0 if options.hero_mode else None,
            "option_targeting_mode": 1 if options.switch_targeting_mode else None,
            "captured_prologue_pigs_bitfield": rando.pigs.captured_pigs_bitfield,
        },
    }

    # ---- hints ----
    hr = rando.hints
    hint_records, hint_index = [], {}

    def hint_ref(h):
        if h is None:
            return None
        if id(h) not in hint_index:
            loc = ctx["hint_source"].get(id(h))
            if h.type in (HintType.LOCATION, HintType.FIXED_LOCATION):
                loc = h.place
            hint_index[id(h)] = len(hint_records)
            hint_records.append({
                "index": len(hint_records),
                "kind": h.type.name.lower(),
                "place": h.place,
                "reward": h.reward,
                "importance": snake(h.importance),
                "location_id": location_id.get(loc) if loc else None,
                "location": loc,
            })
        return hint_index[id(h)]

    savage = [hint_ref(hr.floor_30_hint), hint_ref(hr.floor_50_hint)]
    octo = hint_ref(hr.octo_fairy_hint)
    messages = {}  # message id -> record

    def msg_out(mid, msg, **extra):
        rec = {"message_id": mid, "text": msg.string, "parts": msg.parts, "wrapped": False}
        rec.update(extra)
        messages[mid] = rec
        return rec

    bmg = _Bmg()
    rando.bmg, rando.bfn = bmg, None
    cap_hoho = {}
    hr.rotate_hoho_to_face_hint = lambda idx, hints: cap_hoho.__setitem__(idx, hoho_target_island(rando, hints))
    hint_msgs = {"fishmen": [], "hoho": [], "korl": None}
    can_hint = any(hr.check_item_can_be_hinted_at(i) for i in rando.all_randomized_progress_items)
    # Same sequence as HintsRandomizer._save (minus the asm patch and the file edits).
    hr.update_savage_labyrinth_hint_tablet(hr.floor_30_hint, hr.floor_50_hint, hr.hint_importance)
    msg_out(837, bmg.messages_by_id[837])
    if can_hint:
        hr.update_big_octo_great_fairy_item_name_hint(hr.octo_fairy_hint, hr.hint_importance)
        for mid in (12015, 12016, 12017):
            msg_out(mid, bmg.messages_by_id[mid])
        for placement, hints in hr.hints_per_placement.items():
            if placement == "fishmen_hints":
                hr.update_fishmen_hints()
                for island, h in sorted(hr.island_to_fishman_hint.items()):
                    mid = 13026 + island
                    hint_msgs["fishmen"].append({"island": island, "island_name": rando.island_number_to_name[island],
                                                 "hint": hint_ref(h), **msg_out(mid, bmg.messages_by_id[mid])})
            elif placement == "hoho_hints":
                hr.update_hoho_hints()
                for idx, hs in sorted(hr.hoho_index_to_hints.items()):
                    mid = 14001 + idx
                    hint_msgs["hoho"].append({"hoho_index": idx, "island": hr.HOHO_INDEX_TO_ISLAND_NUM[idx],
                                              "look_at_island": cap_hoho.get(idx), "hints": [hint_ref(h) for h in hs],
                                              **msg_out(mid, bmg.messages_by_id[mid])})
            elif placement == "korl_hints":
                hr.update_korl_hints(hints)
                ids = (1502, 3443, 3444, 3445, 3446, 3447, 3448)
                hint_msgs["korl"] = {"hints": [hint_ref(h) for h in hints],
                                     "messages": [msg_out(mid, bmg.messages_by_id[mid]) for mid in ids]}

    hints = {
        "wrapped": False,
        "wrap_note": "Texts are unwrapped (the disc font is not available in a dry run); control codes are in wwrando's "
                     "\\{..} text form. Word wrapping and 4-line box padding happen where the disc font is known.",
        "enabled_placements": list(hr.hints_per_placement.keys()),
        "records": hint_records,
        "savage_labyrinth": {"floor_30": savage[0], "floor_50": savage[1], "message": messages[837]},
        "octo_great_fairy": None if not can_hint else {"hint": octo, "messages": [messages[m] for m in (12015, 12016, 12017)]},
        "fishmen": hint_msgs["fishmen"],
        "hoho": hint_msgs["hoho"],
        "korl": hint_msgs["korl"],
    }

    # ---- options ----
    # dry_run is how the generator ran, not a seed property; leave it out of the pack.
    option_values = {o.name: options[o.name] for o in type(options).all() if o.name != "dry_run"}

    manifest = {}
    mpath = RUNTIME / "manifest.json"
    if mpath.is_file():
        manifest = json.loads(mpath.read_text())

    return {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "generator": {
            "wwrando_version": VERSION,
            "wwrando_commit": manifest.get("wwrando", {}).get("commit"),
            "wwrando_tree_sha256": manifest.get("wwrando", {}).get("tree_sha256"),
            "gclib_commit": manifest.get("gclib", {}).get("commit"),
            "python": sys.version.split()[0],
            "spoiler_key": False,
        },
        "seed": rando.seed,
        "permalink": rando.permalink,
        "seed_hash": rando.seed_hash,
        "options": option_values,
        "starting": starting,
        "locations": locations,
        "playthrough": [[{"location_id": location_id[loc], "location": loc, "item": item} if not item.startswith("Defeat ")
                         else {"location_id": None, "location": loc, "item": item} for loc, item in sphere.items()]
                        for sphere in ctx["spheres"]],
        "entrances": entrances,
        "charts": charts,
        "required_bosses": required_bosses,
        "hints": hints,
    }


def hoho_target_island(rando, hints_for_hoho):
    """Island number an Old Man Ho Ho should face (HintsRandomizer.rotate_hoho_to_face_hint, minus the disc edits)."""
    from randomizers.hints import HintType
    for hint in hints_for_hoho:
        if hint.type in (HintType.PATH, HintType.BARREN, HintType.ITEM):
            zone_name = hint.place
        elif hint.type == HintType.LOCATION:
            zone_name = rando.entrances.get_entrance_zone_for_item_location(hint.place)
        else:
            continue
        if zone_name == "Ganon's Tower":
            zone_name = "Tower of the Gods Sector"
        if zone_name in rando.island_name_to_number:
            return rando.island_name_to_number[zone_name]
    return None


def main(argv=None) -> int:
    args = parse_args(argv)
    out = Path(args.out).resolve()
    t0 = time.perf_counter()

    try:
        from options.wwrando_options import Options
        from randomizer import WWRandomizer
        from wwrando import make_argparser
        from seedgen import seedgen
    except Exception as e:  # runtime half-installed
        fail(EXIT_RUNTIME, f"cannot import wwrando ({e}); run: python scripts/windows/rando_runtime.py --force")

    # ---- options and seed ----
    try:
        user_options = {}
        if args.options:
            user_options.update(json.loads(Path(args.options).read_text(encoding="utf-8")))
        user_options.update(parse_set(args.set))
        bunny_hood, bunny_start, user_options, permalink = take_bunny_hood(user_options, args.permalink)
        if permalink:
            if args.seed or user_options:
                raise ValueError("--permalink carries the seed and options; do not combine it with --seed, --options or --set")
            seed, options = WWRandomizer.decode_permalink(permalink)
        else:
            options = build_options(Options, user_options)
            seed = args.seed if args.seed else seedgen.make_random_seed_name()
        reject_unsupported(options)
    except Exception as e:
        fail(EXIT_ARGS, f"{type(e).__name__}: {e}")
    if bunny_hood and not bunny_start:
        add_bunny_hood_to_pool()
    options.dry_run = True
    out.mkdir(parents=True, exist_ok=True)
    # Never leave files from an earlier run of the same seed behind (a stale error log or spoiler log would mislead).
    stale = [out / "seed.json", *out.glob(f"WW Random {WWRandomizer.sanitize_seed(seed)} - *.txt")]
    for p in stale:
        with contextlib.suppress(FileNotFoundError):
            p.unlink()

    cmd = make_argparser().parse_args(args=["--dry"])
    rando = None
    ctx = {"hint_source": {}, "spheres": None}
    try:
        with contextlib.redirect_stdout(sys.stderr):
            rando = WWRandomizer(seed=seed, clean_iso_path=None, randomized_output_folder=str(out), options=options, cmd_line_args=cmd)
            if bunny_hood or bunny_start:
                register_bunny_hood(rando)
                rando.permalink += BUNNY_HOOD_PERMALINK
            ctx["location_names"] = list(rando.logic.item_locations.keys())

            # Record which item location each hint came from (the Hint objects only keep the zone).
            hr = rando.hints
            for name in ("get_item_hint", "get_path_hint", "get_location_hint"):
                def wrap(orig):
                    def inner(*a, **k):
                        res = orig(*a, **k)
                        if isinstance(res, tuple) and len(res) == 2 and res[0] is not None:
                            ctx["hint_source"][id(res[0])] = res[1]
                        return res
                    return inner
                setattr(hr, name, wrap(getattr(hr, name)))
            # The spoiler log computes the playthrough; keep it instead of computing it twice.
            orig_spheres = rando.items.calculate_playthrough_progression_spheres

            def spheres():
                ctx["spheres"] = orig_spheres()
                return ctx["spheres"]
            rando.items.calculate_playthrough_progression_spheres = spheres

            rando.randomize_all()
            if ctx["spheres"] is None:  # spoiler log was off
                spheres()
            pack = make_pack(rando, ctx)
            pack["bunny_hood"] = bool(bunny_hood or bunny_start)
            if bunny_start:
                pack["starting"]["gear"].append({"item": BUNNY_HOOD[1], "item_id": BUNNY_HOOD[0]})
    except Exception as e:
        tb = traceback.format_exc()
        message = f"{type(e).__name__}: {e}"
        with contextlib.suppress(Exception):
            if rando is not None:
                rando.write_error_log(f"Error on seed {seed}:\n{e}\n\n{tb}")
            else:  # failed while constructing, before wwrando could write its own error log
                name = f"WW Random {WWRandomizer.sanitize_seed(seed)} - Error Log.txt"
                (out / name).write_text(f"Error on seed {seed}:\n{e}\n\n{tb}", encoding="utf-8")
        print(tb, file=sys.stderr)
        fail(EXIT_GENERATION, "generation failed: " + message.strip().splitlines()[0], seed=seed)

    seed_json = out / "seed.json"
    with open(seed_json, "w", encoding="utf-8", newline="\n") as f:
        json.dump(pack, f, indent=1, ensure_ascii=False)
        f.write("\n")
    logs = {
        "spoiler_log": out / f"WW Random {rando.seed} - Spoiler Log.txt",
        "non_spoiler_log": out / f"WW Random {rando.seed} - Non-Spoiler Log.txt",
    }
    result = {
        "ok": True,
        "seed": rando.seed,
        "permalink": rando.permalink,
        "seed_hash": rando.seed_hash,
        "seed_json": str(seed_json),
        **{k: (str(v) if v.is_file() else None) for k, v in logs.items()},
        "seconds": round(time.perf_counter() - t0, 3),
    }
    print(json.dumps(result), flush=True)
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
