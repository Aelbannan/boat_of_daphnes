#!/usr/bin/env python3
"""The randomizer variant's option sites and seed hooks, generated (R2b preparation).

    python scripts/rando/variant_sites.py --vanilla build/windows/game --reference REF_RUN --out build/windows/rando/sites
    python scripts/rando/variant_sites.py ... --verify-matrix W     # also check the model against patch_matrix.py's runs

Inputs
  scripts/rando/seed_sites.json   what classify_patch.py found: the 88 seed-dependent sites and the 101 option-dependent rows
  --vanilla DIR                   the port's extracted game (main.dol, rels/): the values an option that is off leaves
  --reference DIR                 patch_game.py --files code run with REFERENCE_OPTIONS (the build's `rando-patch` step)
  --wwrando DIR                   wwrando's checkout (asm/custom_symbols.txt: the addresses of the game variables)

Outputs (in --out)
  rando-options.txt   options.txt grammar (mods/betterww/options.txt): the `option` switches of the randomizer, the `site`
                      and `write` lines of the option words and the `hook` lines of the seed's code sites. game_options.py
                      turns the merged file into DolRecomp's site list and the composite's option table.
  rando_sites.inc     the host's tables (runtime/host/src/rando_seed.c includes it): hook id -> what to compute, the data
                      bytes a seed writes, the flag predicates, the custom symbols.
  rando-sites.json    the same as data, with every decision (what was skipped and why), for the report and the tests.

How the reference makes the sites few (docs/fork/rando-inhouse.md, as corrected by R2b)
  wwrando's optional code is applied from fixed free-space slots (asm/patch_diffs), independent of the other options, and
  an option that is off leaves its slot zero and its in-place words vanilla. The reference is therefore a run with every
  block-adding option ON (REFERENCE_OPTIONS): all blocks present, every in-place word in its "on" form. Each option then
  gets one switch, named rando_no_<option>, which when set restores the off form (`site WHERE rando_no_x insn OFFWORD`,
  `write WHERE rando_no_x u8 OFFBYTE`); the host sets it when the seed has the option off. Block words themselves need
  nothing: unreferenced, they cost nothing. wwrando's swift sail, instant text and chart reveal are Better Wind Waker's
  too (the same words, its own sites): the reference has them off and their rows are left to Better Wind Waker.

Needs no embedded Python: it reads files only.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]

# wwrando options of the reference run: every option that adds a block or changes an in-place word is on, except the five
# Better Wind Waker also has (swift sail, instant text, chart reveal, invert camera, hero mode: a seed with hero mode turns on its option, with wwrando's multiplier) (their sites are its own: wwrando's would share the addresses).
REFERENCE_OPTIONS = {
    "swift_sail": False, "instant_text_boxes": False, "reveal_full_sea_chart": False,
    "hero_mode": False, "sword_mode": "Swordless", "invert_camera_x_axis": False, "invert_sea_compass_x_axis": True,
    "remove_music": True, "switch_targeting_mode": True,
}
REFERENCE_SEED = "RV1"
# Rows labelled only with these belong to Better Wind Waker's options (same addresses, its own sites).
BWW_LABELS = {"swift_sail=off", "instant_text_boxes=off", "reveal_full_sea_chart=off", "hero_mode",
              "invert_camera_x_axis"}
# Options forced off when the app generates a seed for in-house play, so the seed never asks for what the reference lacks.
# (The app now makes every player-preference option at REFERENCE_OPTIONS' value: windows/src/randomizer_menu.cpp player_options(); the
# player's settings, not the seed's, decide them at play: runtime/host/src/rando_host.c.)
FORCED_OFF_OPTIONS = ("swift_sail", "instant_text_boxes", "reveal_full_sea_chart")

OPTION_FIRST = 128        # option switch indexes (dolrecomp_option_flags[]); Better Wind Waker and the port use 1..~30
HOOK_FIRST = 1000         # native hook ids of the seed's code sites
SEED_FLAG = "rando_seed"  # on while a seed is playing: gates the seed hooks

# Hook kinds (rando_seed.h).
KIND_LI, KIND_CMPLWI, KIND_CMPWI = 1, 2, 3


def be32(data: bytes, off: int) -> int:
    return struct.unpack_from(">I", data, off)[0]


class Game:
    """Words of a main.dol and RELs, from a folder in the port's layout (main.dol, rels/) or patch_game.py's output
    (sys/main.dol, files/rels/)."""

    def __init__(self, folder: Path, fallback: "Game | None" = None):
        self.folder = Path(folder)
        self.fallback = fallback  # a run holds only the RELs wwrando changed; the others are the disc's
        self.dol = None
        self.rels: dict[str, bytes] = {}

    def dol_bytes(self) -> bytes:
        if self.dol is None:
            for candidate in (self.folder / "sys" / "main.dol", self.folder / "main.dol"):
                if candidate.is_file():
                    self.dol = candidate.read_bytes()
                    break
            else:
                sys.exit("no main.dol in %s" % self.folder)
        return self.dol

    def rel_bytes(self, name: str) -> bytes:
        if name not in self.rels:
            for candidate in (self.folder / "files" / "rels" / name, self.folder / "rels" / name):
                if candidate.is_file():
                    self.rels[name] = candidate.read_bytes()
                    break
            else:
                if self.fallback is None:
                    sys.exit("no %s in %s" % (name, self.folder))
                self.rels[name] = self.fallback.rel_bytes(name)
        return self.rels[name]

    def read(self, row: dict, length: int = 4) -> bytes:
        """The bytes at a table row's file offset (DOL: its file offset, REL: its file offset)."""
        data = self.dol_bytes() if row["file"] == "main.dol" else self.rel_bytes(row["file"])
        off = row["file_offset"]
        return data[off:off + length]


def site_where(row: dict) -> str:
    """DolRecomp's WHERE: a DOL address, or MODULE.rel+OFFSET (an offset in the REL file)."""
    return row["guest_address"] if row["file"] == "main.dol" else row["rel_site"]


def label_option(label: str) -> tuple[str, str]:
    """('sword_mode=Swordless' -> ('sword_mode', 'Swordless')); a bare name is a boolean option that is on."""
    if "=" in label:
        name, value = label.split("=", 1)
        return name, value
    return label, "true"


def flag_name(label: str) -> str:
    name, value = label_option(label)
    return "rando_no_" + (name if value == "true" else (name + "_" + "".join(c if c.isalnum() else "_" for c in value.lower())))


def free_space_start(row: dict) -> bool:
    """Words in a slot only an option fills (vanilla has nothing there): a block word."""
    if row["kind"] == "data":
        return bool(row.get("vanilla_value_is_added_data"))
    return row.get("vanilla_insn") is None


def decode_seed_word(word: int) -> tuple[int, int, int]:
    """(kind, register, immediate) of an instruction a seed site holds: li, cmplwi or cmpwi."""
    op = word >> 26
    rd = (word >> 21) & 31
    ra = (word >> 16) & 31
    if op == 14 and ra == 0:
        return KIND_LI, rd, word & 0xFFFF
    if op == 10 and rd == 0:   # cmplwi crf0, rA
        return KIND_CMPLWI, ra, word & 0xFFFF
    if op == 11 and rd == 0:   # cmpwi crf0, rA
        return KIND_CMPWI, ra, word & 0xFFFF
    raise ValueError("not a li/cmpli/cmpi word: %#010x" % word)


def custom_symbols(wwrando: Path) -> dict[str, int]:
    """sys/main.dol's custom symbols (wwrando's asm/custom_symbols.txt: `  name: 0xADDR` lines under `sys/main.dol:`)."""
    text = (wwrando / "asm" / "custom_symbols.txt").read_text()
    out, in_dol = {}, False
    for line in text.splitlines():
        if not line.startswith(" ") and line.strip():
            in_dol = line.strip() == "sys/main.dol:"
            continue
        if in_dol and ":" in line:
            name, value = line.split(":", 1)
            out[name.strip()] = int(value.strip(), 0)
    return out


def build(seed_sites: dict, vanilla: Game, reference: Game, symbols: dict[str, int]) -> dict:
    plan = {"flags": [], "sites": [], "writes": [], "hooks": [], "data": [], "skipped": [], "symbols": {}}
    # --- the switches: one per option label that owns an in-place word ---------------------------------------------
    labels = []
    for row in seed_sites["option_sites"]:
        if free_space_start(row):
            continue
        for label in row["options"]:
            if label not in BWW_LABELS and label not in labels:
                labels.append(label)
    flags = {}
    for i, label in enumerate(sorted(labels)):
        name, value = label_option(label)
        flags[label] = {"name": flag_name(label), "index": OPTION_FIRST + 1 + i, "option": name, "value": value,
                        "label": label}
    plan["flags"] = [{"name": SEED_FLAG, "index": OPTION_FIRST, "option": None, "value": None, "label": "seed"}] + list(flags.values())

    # --- option words ------------------------------------------------------------------------------------------------
    for row in seed_sites["option_sites"]:
        where = site_where(row)
        if all(label in BWW_LABELS for label in row["options"]):
            plan["skipped"].append({"where": where, "why": "Better Wind Waker's own option (same address)"})
            continue
        if free_space_start(row):
            plan["skipped"].append({"where": where, "why": "block word: present in the reference, unreferenced when the option is off"})
            continue
        if len(row["options"]) != 1:
            sys.exit("option row %s has %d labels; the model needs one" % (where, len(row["options"])))
        label = row["options"][0]
        flag = flags[label]
        length = row.get("length") or 4
        off = vanilla.read(row, length) if row["kind"] == "data" else struct.pack(">I", int(row["patched_insn"], 16))
        on = reference.read(row, length)
        if on == off:
            plan["skipped"].append({"where": where, "why": "the reference already holds the off form"})
            continue
        if row["kind"] == "data":
            if row["file"] != "main.dol":
                sys.exit("option data row %s is in a REL: the off writes handle DOL data only" % where)
            plan["writes"].append({"where": where, "flag": flag["name"], "kind": "u8", "bytes": off.hex(), "on": on.hex(),
                                   "label": label})
        else:
            plan["sites"].append({"where": where, "flag": flag["name"], "off": "0x%08X" % be32(off, 0),
                                  "on": "0x%08X" % be32(on, 0), "label": label, "row_kind": row["kind"]})

    # --- the seed's code sites: hooks -------------------------------------------------------------------------------
    for row in seed_sites["seed_sites"]:
        if row["kind"] != "code":
            continue
        src = row["sources"][0]
        word = int(row["patched_insn"], 16)
        kind, reg, imm = decode_seed_word(word)
        if row["varying_byte_offsets"] not in ([3], [2]):
            sys.exit("%s: unexpected varying bytes %s" % (site_where(row), row["varying_byte_offsets"]))
        byte = row["varying_byte_offsets"][0]
        hook = {"id": HOOK_FIRST + len(plan["hooks"]), "where": site_where(row), "when": "after",
                "kind": kind, "reg": reg, "byte": byte, "word": "0x%08X" % word,
                "source": src["type"], "location_id": src.get("location_id"),
                "address": int(row["guest_address"], 16) if row["file"] == "main.dol" else None,
                "file": row["file"], "file_offset": row["file_offset"], "note": src.get("location") or src.get("field")}
        if src["type"] == "item_location":
            hook["vanilla_word"] = row["vanilla_insn"]
        plan["hooks"].append(hook)

    # --- the seed's data bytes -------------------------------------------------------------------------------------
    for row in seed_sites["seed_sites"]:
        if row["kind"] == "code":
            continue
        for src in row["sources"]:
            if src["type"] != "item_location":
                continue
            item = {"file": row["file"], "location_id": src["location_id"], "location": src.get("location"),
                    "address": None, "module": 0, "section": row.get("section"), "section_offset": None,
                    "file_offset": row["file_offset"] + src["byte_in_site"]}
            if row["file"] == "main.dol":
                item["address"] = int(row["guest_address"], 16) + src["byte_in_site"]
            else:
                item["section_offset"] = row["section_offset"] + src["byte_in_site"]
                item["module"] = be32(vanilla.rel_bytes(row["file"]), 0)
            plan["data"].append(item)

    # --- the game variables (custom symbols of the new DOL section) ------------------------------------------------
    wanted = ["num_triforce_shards_to_start_with", "should_start_with_heros_clothes", "sword_mode", "skip_rematch_bosses",
              "starting_gear", "starting_quarter_hearts", "starting_magic", "captured_prologue_pigs_bitfield",
              "option_targeting_mode", "damage_multiplier"]
    for name in wanted:
        if name not in symbols:
            sys.exit("custom symbol %s not in wwrando's custom_symbols.txt" % name)
        plan["symbols"][name] = symbols[name]
    gear_next = min(a for a in symbols.values() if a > symbols["starting_gear"])
    plan["gear_slots"] = gear_next - symbols["starting_gear"]
    return plan


def options_text(plan: dict) -> str:
    out = ["# Generated by scripts/rando/variant_sites.py from scripts/rando/seed_sites.json -- do not edit.",
           "# The randomizer's switches (indexes %d and up), its option words and its seed hooks." % OPTION_FIRST, ""]
    for flag in plan["flags"]:
        if flag["option"] is None:
            out.append("option %s %d off A randomizer seed is playing" % (flag["name"], flag["index"]))
        else:
            out.append("option %s %d off %s is off in this seed (restores the vanilla word)" % (
                flag["name"], flag["index"], flag["label"]))
    out.append("")
    for site in plan["sites"]:
        out.append("site  %s %s insn %s   # %s" % (site["where"], site["flag"], site["off"], site["label"]))
    for write in plan["writes"]:
        for i in range(len(write["bytes"]) // 2):
            where = write["where"]
            if "+" in where:
                base, off = where.split("+")
                where = "%s+0x%X" % (base, int(off, 0) + i)
            else:
                where = "0x%X" % (int(where, 0) + i)
            out.append("write %s %s u8 0x%s   # %s" % (where, write["flag"], write["bytes"][2 * i:2 * i + 2], write["label"]))
    out.append("")
    for hook in plan["hooks"]:
        out.append("hook  %s %s %s %d   # %s" % (hook["where"], SEED_FLAG, hook["when"], hook["id"], hook["note"]))
    return "\n".join(out) + "\n"


def c_string(text: str) -> str:
    return json.dumps(text)


def sites_inc(plan: dict) -> str:
    """The host's tables. The REL data bytes are by module file name and section offset: the host resolves them against the
    module layout in force (runtime/host/src/rando_seed.c), so nothing here depends on a build."""
    out = ["// Generated by scripts/rando/variant_sites.py -- do not edit.",
           "#define BLUEWAKE_RANDO_SEED_FLAG %du" % [f for f in plan["flags"] if f["name"] == SEED_FLAG][0]["index"],
           "#define BLUEWAKE_RANDO_HOOK_FIRST %du" % HOOK_FIRST,
           "#define BLUEWAKE_RANDO_HOOK_COUNT %du" % len(plan["hooks"]),
           "static const BlueWakeRandoHook kRandoHooks[] = {"]
    for h in plan["hooks"]:
        out.append("    {%du, %d, %d, %d, %s, 0x%08Xu, %s, %s}, // %s" % (
            h["id"], h["kind"], h["reg"], h["byte"], "%du" % h["location_id"] if h["location_id"] is not None else "BLUEWAKE_RANDO_STARTING_ROOM",
            int(h["word"], 16), "0x%08Xu" % h["address"] if h["address"] is not None else "0u", c_string(h["file"]), h["note"]))
    out.append("};")
    out.append("#define BLUEWAKE_RANDO_DATA_COUNT %du" % len(plan["data"]))
    out.append("static const BlueWakeRandoData kRandoData[] = {")
    for d in plan["data"]:
        out.append("    {%du, 0x%08Xu, %du, %du, %du}, // %s %s" % (
            d["location_id"], d["address"] or 0, d["module"] or 0, d["section"] if d["file"] != "main.dol" else 0,
            d["section_offset"] or 0, d["file"], d.get("location") or ""))
    out.append("};")
    out.append("#define BLUEWAKE_RANDO_FLAG_COUNT %du" % len(plan["flags"]))
    out.append("static const BlueWakeRandoFlag kRandoFlags[] = {")
    for f in plan["flags"]:
        out.append("    {%s, %du, %s, %s}," % (c_string(f["name"]), f["index"],
                                               c_string(f["option"] or ""), c_string(f["value"] or "")))
    out.append("};")
    for i, w in enumerate(plan["writes"]):
        out.append("static const u8 kRandoOffBytes%d[] = {%s};" % (i, ", ".join("0x%02Xu" % b for b in bytes.fromhex(w["bytes"]))))
    out.append("#define BLUEWAKE_RANDO_WRITE_COUNT %du" % len(plan["writes"]))
    out.append("static const BlueWakeRandoOffWrite kRandoOffWrites[] = {")
    for i, w in enumerate(plan["writes"]):
        flag = [f for f in plan["flags"] if f["name"] == w["flag"]][0]
        out.append("    {%du, 0x%08Xu, kRandoOffBytes%d, %du}, // %s" % (
            flag["index"], int(w["where"], 0), i, len(w["bytes"]) // 2, w["label"]))
    if not plan["writes"]:
        out.append("    {0u, 0u, NULL, 0u}, // none: C has no empty array")
    out.append("};")
    s = plan["symbols"]
    out.append("#define BLUEWAKE_RANDO_SYM_SHARDS 0x%08Xu" % s["num_triforce_shards_to_start_with"])
    out.append("#define BLUEWAKE_RANDO_SYM_CLOTHES 0x%08Xu" % s["should_start_with_heros_clothes"])
    out.append("#define BLUEWAKE_RANDO_SYM_SWORD_MODE 0x%08Xu" % s["sword_mode"])
    out.append("#define BLUEWAKE_RANDO_SYM_SKIP_REMATCH 0x%08Xu" % s["skip_rematch_bosses"])
    out.append("#define BLUEWAKE_RANDO_SYM_GEAR 0x%08Xu" % s["starting_gear"])
    out.append("#define BLUEWAKE_RANDO_GEAR_SLOTS %du" % plan["gear_slots"])
    out.append("#define BLUEWAKE_RANDO_SYM_QUARTER_HEARTS 0x%08Xu" % s["starting_quarter_hearts"])
    out.append("#define BLUEWAKE_RANDO_SYM_MAGIC 0x%08Xu" % s["starting_magic"])
    out.append("#define BLUEWAKE_RANDO_SYM_PIGS 0x%08Xu" % s["captured_prologue_pigs_bitfield"])
    out.append("#define BLUEWAKE_RANDO_SYM_TARGETING 0x%08Xu" % s["option_targeting_mode"])
    out.append("#define BLUEWAKE_RANDO_SYM_DAMAGE 0x%08Xu" % s["damage_multiplier"])
    return "\n".join(out) + "\n"


def model_word(plan: dict, site: dict, options: dict) -> str:
    """The word the model says a run with these wwrando options has at an option site."""
    flag = [f for f in plan["flags"] if f["name"] == site["flag"]][0]
    value = options.get(flag["option"])
    active = (value is True) if flag["value"] == "true" else (str(value) == flag["value"])
    return site["on"] if active else site["off"]


def file_offset(game: Game, where: str) -> int:
    """The file offset of a DolRecomp WHERE in a game's files (DOL: through its section table)."""
    if "+" in where:
        return int(where.split("+")[1], 0)
    addr = int(where, 0)
    dol = game.dol_bytes()
    for i in range(18):
        off, a, size = be32(dol, i * 4), be32(dol, 0x48 + i * 4), be32(dol, 0x90 + i * 4)
        if size and a <= addr < a + size:
            return off + addr - a
    sys.exit("%s is in no section of %s" % (where, game.folder))


def hook_word(hook: dict, pack: dict) -> int:
    """The word a seed hook leaves the instruction as (what rando_seed.c computes): the baseline word with the item id in
    its varying byte, or the vanilla word where the seed holds no item (an Ice Trap chest keeps the vanilla item)."""
    if hook["location_id"] is None:
        value = pack["starting"]["island"]["room"]
    else:
        value = pack["locations"][hook["location_id"]]["item_id"]
        if value is None:
            return int(hook["vanilla_word"], 16)
    word = int(hook["word"], 16)
    shift = 8 * (3 - hook["byte"])
    return (word & ~(0xFF << shift)) | ((value & 0xFF) << shift)


def verify_seed_sites(plan: dict, run: Path, game: Game) -> list[str]:
    """The run's seed.json says what each seed hook and data byte holds: check the words and bytes wwrando wrote."""
    pack = json.loads((run / "seed.json").read_text())
    problems = []
    for hook in plan["hooks"]:
        row = {"file": hook["file"], "file_offset": file_offset(game, hook["where"])}
        got = be32(game.read(row), 0)
        want = hook_word(hook, pack)
        if got != want:
            problems.append("%s: hook %d %s has %#010x, the pack says %#010x" % (run.name, hook["id"], hook["where"], got, want))
    for item in plan["data"]:
        item_id = pack["locations"][item["location_id"]]["item_id"]
        if item["file"] == "main.dol":
            row = {"file": "main.dol", "file_offset": file_offset(game, "0x%X" % item["address"])}
        else:
            row = {"file": item["file"], "file_offset": item["file_offset"]}
        got = game.read(row, 1)[0]
        if item_id is not None and got != item_id:
            problems.append("%s: byte of location %d at %s has %#x, the pack says %#x" % (
                run.name, item["location_id"], item["file"] + hex(item["file_offset"]), got, item_id))
    return problems


def verify_matrix(plan: dict, work: Path, vanilla: Game) -> list[str]:
    """For every run of patch_matrix.py: the words at the option sites are what the model predicts from the run's options."""
    problems, runs = [], sorted((work / "runs").glob("*/options.json"))
    checked = 0
    for options_path in runs:
        run = options_path.parent
        options = json.loads(options_path.read_text())
        game = Game(run, vanilla)
        # wwrando's defaults for the options a run leaves out
        defaults = {"hero_mode": False, "sword_mode": "Start with Hero's Sword", "invert_camera_x_axis": False,
                    "invert_sea_compass_x_axis": False, "remove_music": False, "switch_targeting_mode": False}
        merged = dict(defaults)
        merged.update(options)
        for site in plan["sites"]:
            row = {"file": "main.dol" if "+" not in site["where"] else site["where"].split("+")[0],
                   "file_offset": None}
            if row["file"] == "main.dol":
                # DOL: translate the guest address through the run's own section table
                addr = int(site["where"], 0)
                dol = game.dol_bytes()
                for i in range(18):
                    off, a, size = be32(dol, i * 4), be32(dol, 0x48 + i * 4), be32(dol, 0x90 + i * 4)
                    if size and a <= addr < a + size:
                        row["file_offset"] = off + addr - a
                        break
            else:
                row["file_offset"] = int(site["where"].split("+")[1], 0)
            got = "0x%08X" % be32(game.read(row), 0)
            want = model_word(plan, site, merged)
            checked += 1
            if got != want:
                problems.append("%s: %s has %s, the model says %s (options %s)" % (run.name, site["where"], got, want, options))
        problems += verify_seed_sites(plan, run, game)
    print("verify-matrix: %d runs, %d option words and the seed's hooks and bytes checked, %d mismatches" % (
        len(runs), checked, len(problems)), file=sys.stderr)
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed-sites", default=str(HERE / "seed_sites.json"))
    ap.add_argument("--vanilla", required=True, help="the extracted game folder (main.dol, rels/)")
    ap.add_argument("--reference", required=True, help="patch_game.py output of the reference run")
    ap.add_argument("--wwrando", default=str(REPO / "build" / ("windows" if os.name == "nt" else "linux") / "rando_runtime" / "wwrando"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--verify-matrix", metavar="WORK")
    args = ap.parse_args()
    seed_sites = json.loads(Path(args.seed_sites).read_text())
    plan = build(seed_sites, Game(args.vanilla), Game(args.reference), custom_symbols(Path(args.wwrando)))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "rando-options.txt").write_text(options_text(plan), newline="\n")
    (out / "rando_sites.inc").write_text(sites_inc(plan), newline="\n")
    (out / "rando-sites.json").write_text(json.dumps(plan, indent=1) + "\n", newline="\n")
    print("flags %d, option sites %d, off writes %d, seed hooks %d, seed data bytes %d, skipped %d" % (
        len(plan["flags"]), len(plan["sites"]), len(plan["writes"]), len(plan["hooks"]), len(plan["data"]), len(plan["skipped"])))
    if args.verify_matrix:
        problems = verify_matrix(plan, Path(args.verify_matrix), Game(args.vanilla))
        for p in problems[:20]:
            print(p, file=sys.stderr)
        return 1 if problems else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
