#!/usr/bin/env python3
"""Translate wwrando's patched main.dol and RELs with DolRecomp and count what differs from the vanilla translation.

    python scripts/rando/translate_patched.py --dolrecomp EXE --vanilla-game DIR --work DIR --out report.json \
                                              --patched RUN_DIR [RUN_DIR ...] [--jobs 2]

RUN_DIR is a patch_game.py output (sys/main.dol and files/rels/*.rel; patch_matrix.py's runs have them). For each:
  * a game folder is assembled: the vanilla main.dol and RELs with the patched ones replacing them; the RELs wwrando
    adds (d_a_dungeon_flag_sw, d_a_switch_op) are named zz_NAME.rel so they sort after every vanilla REL and the
    modules' load addresses (auto-assigned in sorted order, 64 KiB apart) stay those of the vanilla build;
  * DolRecomp runs exactly as scripts/windows/build.py's translate step does (--gamecube --backend c --cpu gekko,
    --partition-instructions 4096 for the DOL, --rel-base 0xC0400000 for the RELs);
  * the generated chunks are compared with the vanilla translation's (made here once, or --vanilla-translated).
The report lists errors, decoder 'unknown' words, load-address changes and the chunks that differ, per patched run and
as a union (the size of the variant a build would add). Nothing here compiles anything.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


def sha(p: Path) -> str:
    return hashlib.sha1(p.read_bytes()).hexdigest()


def prep_rel(src: Path, dst: Path) -> None:
    """The port's disc import writes alignment 4 at 0x40 of every REL (runtime/host/src/disc_import.c); DolRecomp's
    section-alignment check needs it. Idempotent for patch_game.py output, which already does this."""
    d = src.read_bytes()
    dst.write_bytes(d[:0x40] + (4).to_bytes(4, "big") + d[0x44:])


def run(cmd, log: Path) -> tuple[int, str]:
    t = time.perf_counter()
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    log.write_text(r.stdout + "\n--- stderr ---\n" + r.stderr, encoding="utf-8")
    return r.returncode, f"{time.perf_counter() - t:.1f}s"


def translate(exe: Path, dol: Path, rels: Path, out: Path, jobs: int) -> dict:
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    res = {}
    rc, t = run([exe, "--gamecube", "--backend", "c", "--cpu", "gekko", "--partition-instructions", "4096", dol, out / "dol", "-j", jobs],
                out / "dol.log")
    res["dol"] = {"rc": rc, "time": t}
    rc, t = run([exe, "--gamecube", "--backend", "c", "--cpu", "gekko", "--rel-base", "0xC0400000", rels.as_posix().rstrip("/") + "/",
                 out / "rels", "-j", jobs], out / "rels.log")
    res["rels"] = {"rc": rc, "time": t}
    for k in ("dol", "rels"):
        text = (out / f"{k}.log").read_text(encoding="utf-8", errors="replace")
        res[k]["errors"] = [l for l in text.splitlines() if l.startswith("error")]
        res[k]["unknown_words"] = sum(int(m) for m in re.findall(r"(\d+) unknown", text))
        res[k]["bases"] = dict(re.findall(r"module \d+: .*[/\\]([^/\\]+)\.rel -> base (0x[0-9A-F]+)", text))
        # (module name -> base); the section lists of the DOL
        res[k]["sections"] = re.findall(r"\[(\d+)\] file:0x([0-9A-F]+) -> addr:0x([0-9A-F]+)\s+size:0x([0-9A-F]+)", text) if k == "dol" else None
    return res


def chunk_map(root: Path) -> dict[str, str]:
    """relative chunk path -> sha1, for a dolrecomp output folder (…/generated)."""
    out = {}
    for p in sorted(root.rglob("chunk_*.c")):
        out[p.relative_to(root).as_posix()] = sha(p)
    return out


def compare(vroot: Path, proot: Path) -> dict:
    v, p = chunk_map(vroot), chunk_map(proot)
    changed = sorted(k for k in p if k in v and v[k] != p[k])
    new = sorted(k for k in p if k not in v)
    gone = sorted(k for k in v if k not in p)
    return {"vanilla_chunks": len(v), "patched_chunks": len(p), "changed": changed, "new": new, "gone": gone}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dolrecomp", required=True)
    ap.add_argument("--vanilla-game", required=True, help="folder with main.dol and rels/ (build/windows/game)")
    ap.add_argument("--work", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--patched", nargs="+", required=True)
    ap.add_argument("--jobs", type=int, default=2)
    args = ap.parse_args()
    exe, vg, work = Path(args.dolrecomp), Path(args.vanilla_game), Path(args.work)
    work.mkdir(parents=True, exist_ok=True)

    vt = work / "vanilla"
    if not (vt / "rels.log").is_file():
        translate(exe, vg / "main.dol", vg / "rels", vt, args.jobs)
    report = {"runs": {}, "union": {}}
    union = {"dol": set(), "rels": set(), "new": set()}
    for rd in args.patched:
        rd = Path(rd)
        name = rd.name
        g = work / name / "game"
        shutil.rmtree(g, ignore_errors=True)
        (g / "rels").mkdir(parents=True)
        shutil.copyfile(rd / "sys" / "main.dol" if (rd / "sys" / "main.dol").is_file() else vg / "main.dol", g / "main.dol")
        for f in (vg / "rels").glob("*.rel"):
            shutil.copyfile(f, g / "rels" / f.name)
        added = []
        for f in (rd / "files" / "rels").glob("*.rel"):
            if (vg / "rels" / f.name).is_file():
                prep_rel(f, g / "rels" / f.name)
            else:
                prep_rel(f, g / "rels" / ("zz_" + f.name))
                added.append(f.name)
        tr = work / name / "translated"
        info = translate(exe, g / "main.dol", g / "rels", tr, args.jobs)
        vb = {}
        for line in (vt / "rels.log").read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.search(r"module \d+: .*[/\\]([^/\\]+)\.rel -> base (0x[0-9A-F]+)", line)
            if m:
                vb[m.group(1)] = m.group(2)
        shifted = sorted(n for n, b in info["rels"]["bases"].items() if n in vb and vb[n] != b)
        dol_cmp = compare(vt / "dol" / "generated", tr / "dol" / "generated")
        rel_cmp = compare(vt / "rels" / "generated" / "rels", tr / "rels" / "generated" / "rels")
        mods = sorted({k.split("/")[0] for k in rel_cmp["changed"] + rel_cmp["new"]})
        report["runs"][name] = {
            "added_rels": added, "dolrecomp": {k: {x: y for x, y in v.items() if x not in ("bases",)} for k, v in info.items()},
            "rel_base_shifts_vs_vanilla": shifted,
            "dol": {"vanilla_chunks": dol_cmp["vanilla_chunks"], "patched_chunks": dol_cmp["patched_chunks"],
                    "changed": len(dol_cmp["changed"]), "new": len(dol_cmp["new"]), "gone": len(dol_cmp["gone"]),
                    "changed_names": dol_cmp["changed"], "new_names": dol_cmp["new"]},
            "rels": {"vanilla_chunks": rel_cmp["vanilla_chunks"], "patched_chunks": rel_cmp["patched_chunks"],
                     "changed": len(rel_cmp["changed"]), "new": len(rel_cmp["new"]), "gone": len(rel_cmp["gone"]),
                     "modules_touched": len(mods), "new_modules": len([k for k in rel_cmp["new"] if k.split("/")[0].startswith("zz_")])},
        }
        union["dol"] |= set(dol_cmp["changed"]) | set(dol_cmp["new"])
        union["rels"] |= set(rel_cmp["changed"]) | set(rel_cmp["new"])
        print(name, json.dumps({k: report["runs"][name][k] for k in ("dol", "rels")}, default=str)[:400], file=sys.stderr)
    report["union"] = {"dol_chunks": len(union["dol"]), "rel_chunks": len(union["rels"]),
                       "rel_modules": len({k.split("/")[0] for k in union["rels"]})}
    Path(args.out).write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(report["union"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
