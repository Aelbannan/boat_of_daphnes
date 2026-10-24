#!/usr/bin/env python3
"""Check a randomizer variant tree's module layout (R2a): the consistency the host's REL staging relies on.

    python scripts/rando/check_variant.py --base BASE_TREE --mod MOD_TREE [--variants VARIANT_TREE --name rando --layout-rels RELS]

BASE_TREE and MOD_TREE are generate_composite.py outputs (the vanilla game, and wwrando's patched game translated under
the repo's DolRecomp settings). VARIANT_TREE is the base tree after scripts/mods/build_mod_variants.py ran with
`--mod NAME:MOD_TREE:... --layout NAME`. Checks, for the mod's module layout:

  1. no two non-empty linked sections of different modules overlap (the host aliases each one to storage);
  2. every REL data entry names a section of its module with the same address and size;
  3. every chunk of the mod tree (variant, new, or the replaced chest actor's) lies inside one section of one module;
  4. every prolog address lies inside a section of its module;
  5. the layout's modules are the base's plus the new ones, in the same order, and a module of unchanged shape keeps
     the base's sections (a grown REL's code stays where it was: only BSS and appended sections move);
  6. with VARIANT_TREE: mod_variants.inc carries exactly these tables, and the base tables of the variant tree are
     the base tree's own (so the verified composite is untouched).

Prints a JSON summary and exits 1 on any failure. Needs no disc: it reads generated trees only.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "mods"))
import build_mod_variants as bmv  # noqa: E402


def linked_sections(layout):
    """[(module, index, start, end)] of every non-empty linked section of a layout."""
    out = []
    for mid, secs in layout["sections"].items():
        for index, start, size in secs:
            if size and start:
                out.append((mid, index, start, start + size))
    return sorted(out, key=lambda s: s[2])


def check_layout(layout, chunk_ranges=None):
    """The problems of one layout (a list of strings)."""
    problems = []
    secs = linked_sections(layout)
    for (m1, i1, s1, e1), (m2, i2, s2, e2) in zip(secs, secs[1:]):
        if s2 < e1:
            problems.append("sections overlap: module %d section %d [%#x,%#x) and module %d section %d [%#x,%#x)"
                            % (m1, i1, s1, e1, m2, i2, s2, e2))
    table = {mid: {i: (start, size) for i, start, size in s} for mid, s in layout["sections"].items()}
    for (mid, si), (linked, size, _) in layout["data"].items():
        if table.get(mid, {}).get(si) != (linked, size):
            problems.append("data entry (%d,%d) %#x+%#x is not that section of the module's table" % (mid, si, linked, size))
    for mid, address in layout["lifecycle"]:
        if not any(m == mid and s <= address < e for m, _, s, e in secs):
            problems.append("module %d's prolog %#x is outside its sections" % (mid, address))
    for mid, _, count, _, _ in layout["modules"]:
        if len(layout["sections"].get(mid, [])) != count:
            problems.append("module %d: section_count %d but %d table entries" % (mid, count, len(layout["sections"].get(mid, []))))
    for start, end in sorted((chunk_ranges or {}).items()):
        if start >= 0xC0000000 and not any(s <= start and end <= e for _, _, s, e in secs):
            problems.append("chunk [%#x,%#x) is in no section" % (start, end))
    return problems


def compare(base, mod):
    """How the mod's layout differs from the base's."""
    base_ids = [m[0] for m in base["modules"]]
    mod_ids = [m[0] for m in mod["modules"]]
    problems = []
    if mod_ids[:len(base_ids)] != base_ids:
        problems.append("the mod's modules are not the base's, in order, plus new ones")
    new = [m for m in mod_ids if m not in set(base_ids)]
    grown, moved_text = [], []
    for mid in base_ids:
        a, b = base["sections"][mid], mod["sections"].get(mid)
        if a == b:
            continue
        grown.append(mid)
        if b is None:
            problems.append("module %d is missing" % mid)
            continue
        # A file-backed data section that exists in both and moved (BSS follows the file's end, so it moves whenever
        # the file grows; code sections have no data entry).
        for (i, s1, n1), (j, s2, n2) in zip(a, b):
            entry = base["data"].get((mid, i))
            if i == j and s1 != s2 and entry is not None and entry[2] is not None:
                moved_text.append((mid, i))
    return {"new_modules": new, "changed_modules": grown, "moved_sections": moved_text, "problems": problems}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True)
    ap.add_argument("--mod", required=True)
    ap.add_argument("--variants")
    ap.add_argument("--name", default="rando")
    ap.add_argument("--layout-rels", help="the REL files the mod tree was translated from (build_mod_variants --layout-rels)")
    args = ap.parse_args()
    base, mod = bmv.rel_layout(args.base), bmv.rel_layout(args.mod)
    if args.layout_rels:
        for key, image in sorted(bmv.appended_code_images(base, mod, args.layout_rels).items()):
            mod["data"][key] = image
            mod["order"].append(key)
    report = {"base_modules": len(base["modules"]), "mod_modules": len(mod["modules"]), "problems": []}
    report["problems"] += ["base: " + p for p in check_layout(base, bmv.chunk_ranges(args.base))]
    report["problems"] += ["mod: " + p for p in check_layout(mod, bmv.chunk_ranges(args.mod))]
    diff = compare(base, mod)
    report.update({k: v for k, v in diff.items() if k != "problems"})
    report["problems"] += diff["problems"]
    if args.variants:
        v = Path(args.variants)
        want = "\n".join(bmv.layout_lines(args.name, base, mod))
        have = (v / "mod_variants.inc").read_text()
        if want not in have:
            report["problems"].append("mod_variants.inc does not carry the layout of the mod tree")
        for name in ("rel_modules.inc", "rel_data.inc"):
            if (v / name).read_bytes() != (Path(args.base) / name).read_bytes():
                report["problems"].append("%s of the variant tree differs from the base tree's" % name)
        if "MODULE_MOD_LAYOUT_COUNT" not in have:
            report["problems"].append("mod_variants.inc has no layout table")
    print(json.dumps(report, indent=1))
    return 1 if report["problems"] else 0


if __name__ == "__main__":
    sys.exit(main())
