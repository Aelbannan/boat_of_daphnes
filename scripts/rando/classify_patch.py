#!/usr/bin/env python3
"""Classify what wwrando changes in the game as seed-independent, option-dependent or seed-dependent (R2/R3 input).

Reads the runs made by patch_matrix.py and compares them with the vanilla main.dol and RELs (the port's extracted
copies, `build/windows/game`):

    python scripts/rando/classify_patch.py --work DIR [--vanilla-game DIR] --out-classes patch_classes.json \
                                           --out-sites seed_sites.json

Classes (per byte for main.dol and REL sections, per file or per archive member for everything else):
  seed        differs between seeds under some option set of the matrix (the seed pack's values: items, spawn, ...)
  option      identical for every seed, but differs between option sets of the matrix
  independent changed from vanilla, and identical in every run of the matrix
Ranges carry the option names (from the one-option-at-a-time runs) or the seed groups that explain them.

Outputs contain offsets, sizes, option names and single instruction words only; no game data. Needs the embedded
Python 3.12 (numpy, gclib): python scripts/windows/rando_runtime.py
"""
from __future__ import annotations

import argparse
import io
import json
import os
import struct
import subprocess
import sys
from collections import defaultdict
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
REPO = HERE.parents[1]
RUNTIME = Path(os.environ.get("RANDO_RUNTIME") or REPO / "build" / "windows" / "rando_runtime")
if sys.version_info[:2] != (3, 12):
    _py = RUNTIME / "python" / "python.exe"
    if not _py.is_file():
        sys.exit(f"rando runtime not found at {RUNTIME}; run: python scripts/windows/rando_runtime.py")
    sys.exit(subprocess.run([str(_py), str(Path(__file__).resolve()), *sys.argv[1:]]).returncode)
for _p in (RUNTIME / "wwrando", RUNTIME / "gclib", RUNTIME / "site-packages"):
    sys.path.insert(0, str(_p))

import numpy as np  # noqa: E402
from gclib.dol import DOL  # noqa: E402
from gclib.rel import REL  # noqa: E402

DEFAULT_VANILLA = str(Path(__file__).resolve().parents[2] / "build" / "windows" / "game")
CLASS_NAMES = {1: "independent", 2: "option", 3: "seed"}
ABSENT = 256
DOL_BASE, DOL_END = 0x80000000, 0x80500000


# ---------------------------------------------------------------------------------------------------
# A tiny PowerPC decoder: just what the seed sites need (immediates, compares, stores, branches)
# ---------------------------------------------------------------------------------------------------

def disasm(w: int) -> str:
    op = w >> 26
    rd, ra, rb = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    simm = w & 0xFFFF
    if simm >= 0x8000:
        simm -= 0x10000
    uimm = w & 0xFFFF
    if op == 14:
        return f"li r{rd}, {simm}" if ra == 0 else f"addi r{rd}, r{ra}, {simm}"
    if op == 15:
        return f"lis r{rd}, 0x{uimm:X}" if ra == 0 else f"addis r{rd}, r{ra}, 0x{uimm:X}"
    if op == 24:
        return "nop" if w == 0x60000000 else f"ori r{ra}, r{rd}, 0x{uimm:X}"
    if op == 25:
        return f"oris r{ra}, r{rd}, 0x{uimm:X}"
    if op == 28:
        return f"andi. r{ra}, r{rd}, 0x{uimm:X}"
    if op == 11:
        return f"cmpwi r{ra}, {simm}"
    if op == 10:
        return f"cmplwi r{ra}, {uimm}"
    names = {32: "lwz", 34: "lbz", 40: "lhz", 44: "sth", 36: "stw", 38: "stb", 42: "lha", 48: "lfs", 50: "lfd", 52: "stfs"}
    if op in names:
        return f"{names[op]} {'f' if op in (48, 50, 52) else 'r'}{rd}, {simm}(r{ra})"
    if op == 18:
        li = w & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        return f"{'bl' if w & 1 else 'b'} {'+' if li >= 0 else '-'}0x{abs(li):X}"
    if op == 16:
        return f"bc {rd},{ra},{simm & ~3}"
    return f".long 0x{w:08X}"


# ---------------------------------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------------------------------

def load_dol(path: Path):
    """-> (uint16 image over DOL_BASE..DOL_END with ABSENT for unmapped bytes, section list)."""
    data = path.read_bytes()
    dol = DOL()
    dol.read(io.BytesIO(data))
    img = np.full(DOL_END - DOL_BASE, ABSENT, dtype=np.uint16)
    secs = []
    for i, s in enumerate(dol.sections):
        if s.size == 0:
            continue
        a = s.address - DOL_BASE
        img[a:a + s.size] = np.frombuffer(data, dtype=np.uint8, count=s.size, offset=s.offset)
        secs.append({"index": i, "kind": "text" if i < 7 else "data", "address": s.address, "size": s.size, "file_offset": s.offset})
    return img, secs


def load_rel(path: Path):
    rel = REL()
    rel.read(io.BytesIO(path.read_bytes()))
    secs = {}
    for i, s in enumerate(rel.sections):
        if s.is_uninitialized or s.length == 0:
            continue
        secs[i] = {"offset": s.offset, "length": s.length, "exec": bool(s.is_executable),
                   "data": np.frombuffer(s.data.getvalue(), dtype=np.uint8).astype(np.uint16)}
    relocs = {}
    for module, lst in rel.relocation_entries_for_module.items():
        for r in lst:
            relocs[(module, r.curr_section_num, r.relocation_offset)] = (r.relocation_type.name, r.section_num_to_relocate_against,
                                                                         r.symbol_address)
    header = {"id": rel.id, "bss_size": rel.bss_size, "prolog": (rel.prolog_section, rel.prolog_offset),
              "epilog": (rel.epilog_section, rel.epilog_offset), "unresolved": (rel.unresolved_section, rel.unresolved_offset),
              "num_sections": len(rel.sections), "file_size": len(path.read_bytes()),
              "section_table": [(s.offset, s.length, bool(s.is_executable), bool(s.is_bss)) for s in rel.sections]}
    return secs, relocs, header


# ---------------------------------------------------------------------------------------------------
# Classification
# ---------------------------------------------------------------------------------------------------

class Matrix:
    def __init__(self, work: Path):
        self.work = work
        self.cfgs = {c["name"]: c for c in json.loads((work / "configs.json").read_text())}
        self.names = [n for n in self.cfgs if (work / "runs" / n / "done.json").is_file()]
        self.baseline = "base_1"
        groups = defaultdict(list)
        for n in self.names:
            groups[self.cfgs[n]["group"]].append(n)
        self.seed_groups = {g: m for g, m in groups.items() if g != "single" and len(m) >= 2}
        self.option_runs = [n for n in self.names if n != self.baseline]

    def run_dir(self, n):
        return self.work / "runs" / n

    def labels(self, V, arrs: dict):
        """arrs: run name -> array like V. Returns uint8 label array and the helper masks."""
        changed = np.zeros(V.shape, bool)
        for a in arrs.values():
            changed |= a != V
        seed = np.zeros(V.shape, bool)
        for g, members in self.seed_groups.items():
            first = arrs[members[0]]
            for m in members[1:]:
                seed |= arrs[m] != first
        base = arrs[self.baseline]
        opt = np.zeros(V.shape, bool)
        for n in self.option_runs:
            opt |= arrs[n] != base
        lab = np.zeros(V.shape, np.uint8)
        lab[changed] = 1
        lab[changed & opt] = 2
        lab[seed] = 3
        return lab

    def explain(self, arrs: dict, lo: int, hi: int, label: int):
        """Which seed groups / option runs explain a range."""
        if label == 3:
            out = []
            for g, members in self.seed_groups.items():
                first = arrs[members[0]][lo:hi]
                if any((arrs[m][lo:hi] != first).any() for m in members[1:]):
                    out.append(g)
            return out
        if label == 2:
            return self.option_names(arrs, lo, hi)
        return []

    def option_names(self, arrs: dict, lo: int, hi: int):
        """The one-option runs that change [lo,hi) from the baseline; the option sets (qol/game/rand) only when no single
        option explains it (the range then depends on a combination)."""
        base = arrs[self.baseline][lo:hi]
        singles = sorted(self.cfgs[n].get("label", n) for n in self.option_runs
                         if self.cfgs[n]["group"] == "single" and (arrs[n][lo:hi] != base).any())
        if singles:
            return singles
        return sorted(n for n in self.option_runs if (arrs[n][lo:hi] != base).any())


def runs_of(lab: np.ndarray):
    """Yield (start, end, label) for each maximal run of equal non-zero labels."""
    if lab.size == 0:
        return
    edges = np.flatnonzero(np.diff(lab.astype(np.int16))) + 1
    starts = np.concatenate(([0], edges))
    ends = np.concatenate((edges, [lab.size]))
    for s, e in zip(starts, ends):
        if lab[s]:
            yield int(s), int(e), int(lab[s])


def word_align(lab: np.ndarray, text_ranges):
    """In code sections the unit is an instruction: a changed byte makes its whole word that class (the max)."""
    for lo, hi in text_ranges:
        lo4, hi4 = lo & ~3, (hi + 3) & ~3
        seg = lab[lo4:hi4]
        if seg.size % 4 == 0 and seg.size:
            lab[lo4:hi4] = np.repeat(seg.reshape(-1, 4).max(axis=1), 4)


def dump_json(obj, line_lists=("ranges", "files", "seed_sites", "option_sites"), level=0) -> str:
    """json.dumps(indent=1), except that the long lists named in line_lists get one compact item per line."""
    nl = chr(10)
    pad = " " * (level + 1)
    if isinstance(obj, dict):
        parts = []
        for k, v in obj.items():
            if k in line_lists and isinstance(v, list) and v:
                body = ("," + nl).join(pad + " " + json.dumps(x, separators=(",", ":")) for x in v)
                parts.append(pad + json.dumps(str(k)) + ": [" + nl + body + nl + pad + "]")
            else:
                parts.append(pad + json.dumps(str(k)) + ": " + dump_json(v, line_lists, level + 1))
        return "{" + nl + ("," + nl).join(parts) + nl + " " * level + "}"
    return json.dumps(obj, indent=1).replace(nl, nl + " " * level)


def class_bytes(lab):
    return {CLASS_NAMES[c]: int((lab == c).sum()) for c in (1, 2, 3)}


# ---------------------------------------------------------------------------------------------------
# Per file
# ---------------------------------------------------------------------------------------------------

def analyse_dol(mx: Matrix, vanilla: Path):
    V, vsecs = load_dol(vanilla / "main.dol")
    arrs, secs_by_run = {}, {}
    for n in mx.names:
        arrs[n], secs_by_run[n] = load_dol(mx.run_dir(n) / "sys" / "main.dol")
    lab = mx.labels(V, arrs)
    text = [(s["address"] - DOL_BASE, s["address"] - DOL_BASE + s["size"]) for s in secs_by_run[mx.baseline] if s["kind"] == "text"]
    text += [(s["address"] - DOL_BASE, s["address"] - DOL_BASE + s["size"]) for s in vsecs if s["kind"] == "text"]
    word_align(lab, text)
    base_secs = secs_by_run[mx.baseline]

    def sec_of(addr):
        for s in base_secs + vsecs:
            if s["address"] <= addr < s["address"] + s["size"]:
                return s
        return None

    ranges = []
    for s, e, c in runs_of(lab):
        # split at section boundaries so a range never straddles two sections
        cur = s
        while cur < e:
            addr = DOL_BASE + cur
            sec = sec_of(addr)
            end = e if sec is None else min(e, sec["address"] + sec["size"] - DOL_BASE)
            if end <= cur:
                end = e
            r = {"address": addr, "length": end - cur, "class": CLASS_NAMES[c], "section": None if sec is None else sec["index"]}
            if sec is not None:
                r["text"] = sec["kind"] == "text"
                r["file_offset"] = addr - sec["address"] + sec["file_offset"]
            if (V[cur:end] == ABSENT).all():
                r["added"] = True  # not in the vanilla image: new section
            why = mx.explain(arrs, cur, end, c)
            if why:
                r["seed_groups" if c == 3 else "options"] = why
            ranges.append(r)
            cur = end
    summary = {"bytes": class_bytes(lab), "ranges": {CLASS_NAMES[c]: sum(1 for r in ranges if r["class"] == CLASS_NAMES[c]) for c in (1, 2, 3)},
               "vanilla_sections": vsecs, "patched_sections": base_secs,
               "text_instruction_words": {CLASS_NAMES[c]: int((lab[np.concatenate([np.arange(lo & ~3, (hi + 3) & ~3, 4) for lo, hi in text])] == c).sum())
                                          for c in (1, 2, 3)}}
    return ranges, summary, {"V": V, "arrs": arrs, "lab": lab, "secs": base_secs}


def analyse_rel(mx: Matrix, vanilla: Path, name: str):
    vp = vanilla / "rels" / name
    vs, vr, vh = load_rel(vp) if vp.is_file() else ({}, {}, None)
    runs = {}
    for n in mx.names:
        p = mx.run_dir(n) / "files" / "rels" / name
        runs[n] = load_rel(p) if p.is_file() else (vs, vr, vh)  # a run that left the REL alone has the vanilla one
    section_ids = sorted(set(vs) | {i for s, _, _ in runs.values() for i in s})
    out_secs, ranges = {}, []
    tot = {1: 0, 2: 0, 3: 0}
    for si in section_ids:
        length = max([vs[si]["length"] if si in vs else 0] + [r[0][si]["length"] for r in runs.values() if si in r[0]])
        V = np.full(length, ABSENT, np.uint16)
        if si in vs:
            V[:vs[si]["length"]] = vs[si]["data"]
        arrs = {}
        for n, (s, _, _) in runs.items():
            a = np.full(length, ABSENT, np.uint16)
            if si in s:
                a[:s[si]["length"]] = s[si]["data"]
            arrs[n] = a
        lab = mx.labels(V, arrs)
        ex = (vs.get(si) or runs[mx.baseline][0].get(si))["exec"]
        if ex:
            word_align(lab, [(0, length)])
        info = runs[mx.baseline][0].get(si) or vs.get(si)
        out_secs[si] = {"file_offset": info["offset"], "length_vanilla": vs[si]["length"] if si in vs else None,
                        "length_patched": runs[mx.baseline][0][si]["length"] if si in runs[mx.baseline][0] else None,
                        "exec": ex, "new_section": si not in vs}
        lengths = {n: (s[si]["length"] if si in s else None) for n, (s, _, _) in runs.items()}
        if len(set(lengths.values())) > 1:
            out_secs[si]["length_varies_by_run"] = True
        for s, e, c in runs_of(lab):
            tot[c] += e - s
            r = {"section": si, "offset": s, "length": e - s, "file_offset": info["offset"] + s,
                 "exec": ex, "class": CLASS_NAMES[c]}
            if (V[s:e] == ABSENT).all():
                r["added"] = True
            why = mx.explain(arrs, s, e, c)
            if why:
                r["seed_groups" if c == 3 else "options"] = why
            ranges.append(r)
        out_secs[si]["_arrs"] = (V, arrs, lab)
    # relocations
    keys = set(vr)
    for _, r, _ in runs.values():
        keys |= set(r)
    rl = {1: 0, 2: 0, 3: 0}
    reloc_removed = reloc_added = 0
    for k in keys:
        vals = {n: runs[n][1].get(k) for n in mx.names}
        v = vr.get(k)
        changed = any(x != v for x in vals.values())
        if not changed:
            continue
        seed = any(len({vals[m] for m in members}) > 1 for members in mx.seed_groups.values())
        opt = any(vals[n] != vals[mx.baseline] for n in mx.option_runs)
        c = 3 if seed else 2 if opt else 1
        rl[c] += 1
        if v is None:
            reloc_added += 1
        elif all(x is None for x in vals.values()):
            reloc_removed += 1
    hdr_changed = [k for k in ("id", "bss_size", "prolog", "epilog", "unresolved", "num_sections") if vh and vh[k] != runs[mx.baseline][2][k]]
    bh = runs[mx.baseline][2]
    summary = {"file": name, "new_rel": vh is None, "bytes": {CLASS_NAMES[c]: tot[c] for c in tot}, "relocations": {CLASS_NAMES[c]: rl[c] for c in rl},
               "relocations_added": reloc_added, "relocations_removed": reloc_removed, "header_changed": hdr_changed,
               "file_size_vanilla": vh["file_size"] if vh else None, "file_size_patched": bh["file_size"],
               "file_size_varies_by_run": len({r[2]["file_size"] for r in runs.values()}) > 1,
               "bss_size_vanilla": vh["bss_size"] if vh else None, "bss_size_patched": bh["bss_size"],
               "sections": {si: {k: v for k, v in d.items() if k != "_arrs"} for si, d in out_secs.items()}}
    return ranges, summary, out_secs


# ---------------------------------------------------------------------------------------------------
# Files other than code: one entry per file or archive member
# ---------------------------------------------------------------------------------------------------

def classify_files(mx: Matrix):
    vals = defaultdict(dict)  # (path, inner|None) -> run -> sha1
    meta = {}
    for n in mx.names:
        m = json.loads((mx.run_dir(n) / "patch_manifest.json").read_text())
        for row in m["files"]:
            if row["kind"] in ("dol", "rel"):
                continue
            if row["kind"] == "arc":
                for inner, h in (row.get("inner_sha1") or {}).items():
                    vals[(row["path"], inner)][n] = h
                    meta[(row["path"], inner)] = {"added": inner in row.get("inner_added", []), "arc_added": row["added"]}
                for inner in row.get("inner_removed", []):
                    vals[(row["path"], inner)][n] = "removed"
                    meta[(row["path"], inner)] = {"added": False, "arc_added": row["added"]}
            else:
                vals[(row["path"], None)][n] = row["sha1"]
                meta[(row["path"], None)] = {"added": row["added"], "arc_added": False}
    out = []
    for key, per in sorted(vals.items(), key=lambda kv: (kv[0][0], kv[0][1] or "")):
        v = {n: per.get(n, "vanilla") for n in mx.names}
        seed = any(len({v[m] for m in members}) > 1 for members in mx.seed_groups.values())
        opt = any(v[n] != v[mx.baseline] for n in mx.option_runs)
        c = 3 if seed else 2 if opt else 1
        e = {"path": key[0], "member": key[1], "class": CLASS_NAMES[c], "added": meta[key]["added"] or meta[key]["arc_added"],
             "in_runs": sum(1 for x in per.values() if x != "vanilla")}
        if c == 3:
            e["seed_groups"] = [g for g, members in mx.seed_groups.items() if len({v[m] for m in members}) > 1]
        if c == 2:
            singles = sorted(mx.cfgs[n].get("label", n) for n in mx.option_runs
                             if mx.cfgs[n]["group"] == "single" and v[n] != v[mx.baseline])
            e["options"] = singles or sorted(n for n in mx.option_runs if v[n] != v[mx.baseline])
        out.append(e)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", required=True)
    ap.add_argument("--vanilla-game", default=DEFAULT_VANILLA)
    ap.add_argument("--out-classes", required=True)
    ap.add_argument("--out-sites")
    args = ap.parse_args()
    mx = Matrix(Path(args.work))
    vanilla = Path(args.vanilla_game)
    if mx.baseline not in mx.names:
        sys.exit("the baseline run base_1 is missing; run patch_matrix.py first")
    print(f"{len(mx.names)} runs; seed groups: { {g: m for g, m in mx.seed_groups.items()} }", file=sys.stderr)

    dol_ranges, dol_summary, dol_data = analyse_dol(mx, vanilla)
    print("dol:", dol_summary["bytes"], file=sys.stderr)

    rel_names = sorted({p.name for n in mx.names for p in (mx.run_dir(n) / "files" / "rels").glob("*.rel")})
    rel_summaries, rel_ranges, rel_data = {}, [], {}
    for name in rel_names:
        ranges, summary, secs = analyse_rel(mx, vanilla, name)
        rel_summaries[name] = summary
        rel_ranges += [dict(r, file=name) for r in ranges]
        rel_data[name] = secs
    files = classify_files(mx)

    def tally(items, key="class"):
        t = defaultdict(int)
        for i in items:
            t[i[key]] += 1
        return dict(t)

    by_option = defaultdict(lambda: {"dol_ranges": 0, "dol_bytes": 0, "rel_ranges": 0, "rel_bytes": 0, "rel_files": set(), "non_code_files": 0})
    for r in dol_ranges:
        for o in r.get("options", []) if r["class"] == "option" else []:
            by_option[o]["dol_ranges"] += 1
            by_option[o]["dol_bytes"] += r["length"]
    for r in rel_ranges:
        for o in r.get("options", []) if r["class"] == "option" else []:
            by_option[o]["rel_ranges"] += 1
            by_option[o]["rel_bytes"] += r["length"]
            by_option[o]["rel_files"].add(r["file"])
    for f in files:
        for o in f.get("options", []) if f["class"] == "option" else []:
            by_option[o]["non_code_files"] += 1
    by_option = {o: {k: (sorted(v) if isinstance(v, set) else v) for k, v in d.items()} for o, d in sorted(by_option.items())}

    result = {
        "schema": "bod.patchclasses", "schema_version": 1,
        "matrix": [{k: v for k, v in c.items()} for c in mx.cfgs.values() if c["name"] in mx.names],
        "baseline": mx.baseline, "seed_groups": mx.seed_groups,
        "summary": {
            "dol_bytes": dol_summary["bytes"], "dol_ranges": dol_summary["ranges"],
            "dol_instruction_words": dol_summary["text_instruction_words"],
            "rel_files_changed": len(rel_names), "rel_files_new": sum(1 for s in rel_summaries.values() if s["new_rel"]),
            "rel_bytes": {c: sum(s["bytes"][c] for s in rel_summaries.values()) for c in ("independent", "option", "seed")},
            "rel_ranges": tally(rel_ranges),
            "rel_relocations": {c: sum(s["relocations"][c] for s in rel_summaries.values()) for c in ("independent", "option", "seed")},
            "non_code_files": tally(files), "non_code_archives": len({f["path"] for f in files if f["member"]}),
        },
        "by_option": by_option,
        "dol": {"vanilla_sections": dol_summary["vanilla_sections"], "patched_sections": dol_summary["patched_sections"], "ranges": dol_ranges},
        "rels": {"files": rel_summaries, "ranges": rel_ranges},
        "files": files,
    }
    Path(args.out_classes).write_text(dump_json(result) + "\n", encoding="utf-8")
    print(json.dumps(result["summary"], indent=1))
    if args.out_sites:
        import pickle
        sys.setrecursionlimit(10000)
        # sites are built by seed_sites() below (kept separate so the classes run is cheap to repeat)
        build_seed_sites(mx, vanilla, dol_ranges, dol_data, rel_ranges, rel_data, Path(args.out_sites))
    return 0


# ---------------------------------------------------------------------------------------------------
# The site tables (R3's hook table and R2's option sites)
# ---------------------------------------------------------------------------------------------------

# Seed-pack field behind each custom symbol of the DOL that is written per seed or per option (tweaks.py,
# randomizers/pigs.py, asm/custom_symbols.txt; the field names are scripts/rando/generate_seed.py's seed.json).
SYMBOL_FIELDS = {
    "starting_gear": "starting.gear[].item_id (u8 array of 86 slots, 0xFF terminated; progressive ids as in wwrando)",
    "num_triforce_shards_to_start_with": "starting.game_variables.num_triforce_shards_to_start_with",
    "should_start_with_heros_clothes": "options.player_in_casual_clothes (inverted)",
    "sword_mode": "starting.game_variables.sword_mode",
    "skip_rematch_bosses": "starting.game_variables.skip_rematch_bosses",
    "starting_quarter_hearts": "starting.game_variables.starting_quarter_hearts",
    "starting_magic": "starting.game_variables.starting_magic",
    "captured_prologue_pigs_bitfield": "starting.game_variables.captured_prologue_pigs_bitfield",
    "option_targeting_mode": "starting.game_variables.option_targeting_mode",
}


def item_path_table():
    """(file, byte offset) -> (location name, wwrando path) for the hard-coded item bytes wwrando writes
    (logic/item_locations.txt "Paths": rels/NAME.rel@FILEOFFSET, main.dol@ADDRESS, CustomSymbol:NAME)."""
    import re
    from ruamel.yaml import YAML
    yaml = YAML(typ="safe")
    locs = yaml.load((RUNTIME / "wwrando" / "logic" / "item_locations.txt").read_text(encoding="utf-8"))
    syms = yaml.load((RUNTIME / "wwrando" / "asm" / "custom_symbols.txt").read_text(encoding="utf-8"))
    table = {}
    for name, d in locs.items():
        for p in d.get("Paths", []) or []:
            m = re.match(r"^rels/([^.]+\.rel)@([0-9A-F]{4})$", p)
            if m:
                table[(m.group(1), int(m.group(2), 16))] = (name, p)
                continue
            m = re.match(r"^main\.dol@(8[0-9A-F]{7})$", p)
            if m:
                table[("main.dol", int(m.group(1), 16))] = (name, p)
                continue
            m = re.match(r"^CustomSymbol:(.+)$", p)
            if m:
                for fpath, d2 in syms.items():
                    if m.group(1) in d2:
                        table[("main.dol" if fpath == "sys/main.dol" else os.path.basename(fpath), d2[m.group(1)])] = (name, p)
    return table, syms


def _words(arr, lo, hi):
    """Big-endian words of arr[lo:hi) (4-aligned); None where bytes are absent."""
    out = []
    for o in range(lo, hi, 4):
        w = arr[o:o + 4]
        out.append(None if len(w) < 4 or (w == ABSENT).any() else int.from_bytes(bytes(int(x) for x in w), "big"))
    return out


def _reg_of(text):
    import re
    m = re.match(r"^(?:li|lis|addi|addis) r(\d+)", text or "")
    if m:
        return int(m.group(1))
    m = re.match(r"^(?:cmpwi|cmplwi) r(\d+)", text or "")
    return int(m.group(1)) if m else None


def build_seed_sites(mx, vanilla, dol_ranges, dol_data, rel_ranges, rel_data, out_path):
    table, syms = item_path_table()
    packs = {n: json.loads((mx.run_dir(n) / "seed.json").read_text(encoding="utf-8")) for n in mx.names}
    loc_id = {l["name"]: l["id"] for l in packs[mx.baseline]["locations"]}
    loc_item = {n: {l["name"]: l["item_id"] for l in packs[n]["locations"]} for n in mx.names}
    dol_syms = sorted((a, n) for n, a in syms.get("sys/main.dol", {}).items())

    def dol_symbol(addr):
        best = None
        for a, n in dol_syms:
            if a <= addr:
                best = (a, n)
        return best

    def row_for(file, rel_name, V, arrs, lo, hi, exec_, section, sec_off0, file_off0, guest0, cls):
        """Rows for [lo,hi) of one section image."""
        rows = []
        base = arrs[mx.baseline]
        if exec_:
            starts = range(lo & ~3, (hi + 3) & ~3, 4)
        else:
            starts = [lo]
        for w0 in starts:
            w1 = w0 + 4 if exec_ else hi
            row = {"kind": "code" if exec_ else "data", "file": file, "section": section, "file_offset": file_off0 + (w0 - lo)}
            if rel_name:
                row["section_offset"] = sec_off0 + (w0 - lo)
            if file == "main.dol":
                row["guest_address"] = f"0x{guest0 + (w0 - lo):08X}"
            else:
                row["rel_site"] = f"{rel_name}+0x{file_off0 + (w0 - lo):X}"
            vary = [i for i in range(w0, w1) if any(arrs[n][i] != base[i] for n in mx.names)]
            row["varying_byte_offsets"] = [i - w0 for i in vary]
            if exec_:
                vw, bw = _words(V, w0, w0 + 4)[0], _words(base, w0, w0 + 4)[0]
                row["vanilla_insn"] = None if vw is None else f"0x{vw:08X}"
                row["patched_insn"] = None if bw is None else f"0x{bw:08X}"
                row["disasm"] = None if bw is None else disasm(bw)
                row["vanilla_disasm"] = None if vw is None else disasm(vw)
                reg = _reg_of(row["disasm"])
                if reg is not None:
                    row["register"] = reg
                row["distinct_words"] = len({w for n in mx.names for w in _words(arrs[n], w0, w0 + 4)})
            else:
                row["length"] = hi - lo
                row["vanilla_value_is_added_data"] = bool((V[w0:w1] == ABSENT).all())
                row["distinct_values"] = len({tuple(int(x) for x in arrs[n][w0:w1]) for n in mx.names})
            # which seed-pack field gives each varying byte
            sources = []
            for i in vary:
                key = (rel_name or "main.dol", (file_off0 + (i - lo)) if rel_name else (guest0 + (i - lo)))
                src = None
                if key in table:
                    name, path = table[key]
                    got = [int(arrs[n][i]) for n in mx.names]
                    want = [loc_item[n].get(name) for n in mx.names]
                    src = {"type": "item_location", "location": name, "location_id": loc_id[name],
                           "field": f"locations[{loc_id[name]}].item_id", "wwrando_path": path,
                           "runs_checked": len(got), "runs_matching": sum(1 for g, w in zip(got, want) if g == w)}
                elif file == "main.dol":
                    a = guest0 + (i - lo)
                    if a == 0x80058BA7:
                        src = {"type": "starting_room", "field": "starting.island.room",
                               "note": "u8 immediate of the new-game li at 0x80058BA4; the starting spawn id (0x80058BAF) is a constant 0",
                               "runs_checked": len(mx.names),
                               "runs_matching": sum(1 for n in mx.names if int(arrs[n][i]) == packs[n]["starting"]["island"]["room"])}
                    else:
                        s = dol_symbol(a)
                        if s and s[1] in SYMBOL_FIELDS:
                            src = {"type": "custom_symbol", "symbol": s[1], "symbol_address": f"0x{s[0]:08X}",
                                   "field": SYMBOL_FIELDS[s[1]]}
                if src is None:
                    src = {"type": "unattributed"}
                src["byte_in_site"] = i - w0
                sources.append(src)
            row["sources"] = sources
            if exec_ and row["disasm"].startswith(".long"):
                row["kind"] = "data_in_code"  # a data word inside an executable section (custom symbol or table)
            elif any(x["type"] == "custom_symbol" for x in sources):
                row["kind"] = "data_in_code"
            if cls == "seed":
                row["seed_groups"] = r_groups
            else:
                row["options"] = r_opts
                del row["sources"]
            rows.append(row)
        return rows

    seed_rows, option_rows = [], []
    for r in dol_ranges:
        if r["class"] == "independent":
            continue
        lo = r["address"] - DOL_BASE
        r_groups, r_opts = r.get("seed_groups"), r.get("options")
        fo = next((x["file_offset"] + r["address"] - x["address"] for x in dol_data["secs"]
                   if x["address"] <= r["address"] < x["address"] + x["size"]), None)
        rows = row_for("main.dol", None, dol_data["V"], dol_data["arrs"], lo, lo + r["length"], r.get("text", False),
                       r["section"], 0, fo or 0, r["address"], r["class"])
        (seed_rows if r["class"] == "seed" else option_rows).extend(rows)
    for r in rel_ranges:
        if r["class"] == "independent":
            continue
        V, arrs, _ = rel_data[r["file"]][r["section"]]["_arrs"]
        r_groups, r_opts = r.get("seed_groups"), r.get("options")
        rows = row_for(r["file"], r["file"], V, arrs, r["offset"], r["offset"] + r["length"], r["exec"], r["section"],
                       r["offset"], r["file_offset"], None, r["class"])
        (seed_rows if r["class"] == "seed" else option_rows).extend(rows)

    def tally(rows):
        t = defaultdict(int)
        for x in rows:
            t[("dol" if x["file"] == "main.dol" else "rel", x["kind"])] += 1
        return {f"{a}_{b}": n for (a, b), n in sorted(t.items())}

    out = {
        "schema": "bod.seedsites", "schema_version": 1,
        "generator": "scripts/rando/classify_patch.py",
        "convention": {
            "rel_site": "NAME.rel+0xOFF is an offset in the REL file (sections keep their vanilla file offsets in the patched REL): the "
                        "convention of mods/betterww/options.txt; DolRecomp resolves it as the module base plus OFF. wwrando's own "
                        "paths (rels/NAME.rel@XXXX) use the same offsets",
            "guest_address": "main.dol sites are guest addresses",
            "code_sites": "one row per 4-byte instruction word; patched_insn is the baseline run's word (default options, seed RV1), "
                          "register is the destination (li/lis/addi) or compared register (cmpwi); the value of the site is the "
                          "varying byte(s) given by varying_byte_offsets (offset 3 is the low byte of an immediate)",
        },
        "summary": {"seed_dependent": tally(seed_rows), "option_dependent": tally(option_rows),
                    "seed_dependent_bytes_by_source": {k: sum(1 for x in seed_rows for q in x["sources"] if q["type"] == k)
                                                   for k in sorted({q["type"] for x in seed_rows for q in x["sources"]})}},
        "seed_sites": seed_rows,
        "option_sites": option_rows,
    }
    Path(out_path).write_text(dump_json(out) + "\n", encoding="utf-8")
    print(json.dumps(out["summary"], indent=1))


if __name__ == "__main__":
    sys.exit(main())
