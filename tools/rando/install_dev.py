#!/usr/bin/env python3
"""Puts the randomizer beside a developer build of the game (Boat of Daphnes' build.py `randomizer` step and its
prepared game folder, for this pipeline).

    python tools/rando/install_dev.py [BUILD_DIR]        (default build/windows-clang)

Makes, in BUILD_DIR:
  rando/                       the seed generator: the embedded Python, wwrando and gclib (build/windows/rando_runtime,
                               made by tools/rando/rando_runtime.py), linked, and its scripts (generate_seed.py,
                               patch_game.py, describe_options.py, prepare_extras.py, reference-options.json)
  assets/randomizer/options.json   wwrando's options for the Randomizer menu (describe_options.py)
  game/rando/GZLE99.iso        the randomizer's disc: the reference run (build/rando/reference.iso), hard-linked
  game/rando/rels/             its patched RELs (build/rando/orig/rel), linked: the file select checks they were made
  game/rando/logo/TlogoE.arc   wwrando's title logo (the file select's quest picture)
  game/rando/changed.txt       the files the two discs have differently
  tww_game_rando.diff          the functions the two programs have differently (module_diff.py)
The game module of the randomizer, tww_game_rando.dll, is the CMake target of that name.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))


def link_dir(source: Path, target: Path) -> None:
    if target.exists():
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cmd", "/c", "mklink", "/J", str(target), str(source)], check=True, capture_output=True)


def link_file(source: Path, target: Path) -> None:
    if target.exists():
        if os.path.samefile(source, target):
            return
        target.unlink()
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.link(source, target)
    except OSError:
        shutil.copy2(source, target)


def fst_files(iso: Path) -> dict[str, tuple[int, int]]:
    """path -> (offset, length) of every file on a GameCube disc image (an uncompressed .iso)."""
    import struct
    with open(iso, "rb") as f:
        f.seek(0x424)
        off, size = struct.unpack(">II", f.read(8))
        f.seek(off)
        d = f.read(size)
    n = struct.unpack(">I", d[8:12])[0]
    strtab = n * 12
    out: dict[str, tuple[int, int]] = {}

    def name(i: int) -> str:
        o = strtab + (struct.unpack(">I", d[i * 12:i * 12 + 4])[0] & 0xFFFFFF)
        return d[o:d.index(b"\0", o)].decode("shift-jis")

    def walk(i: int, end: int, prefix: str) -> None:
        while i < end:
            w0, w1, w2 = struct.unpack(">III", d[i * 12:i * 12 + 12])
            if w0 >> 24:
                walk(i + 1, w2, prefix + name(i) + "/")
                i = w2
            else:
                out[prefix + name(i)] = (w1, w2)
                i += 1

    walk(1, n, "")
    return out


def changed_files(a_iso: Path, b_iso: Path) -> list[str]:
    a, b = fst_files(a_iso), fst_files(b_iso)
    out = []
    with open(a_iso, "rb") as fa, open(b_iso, "rb") as fb:
        for path, (ao, al) in a.items():
            if path not in b:
                continue
            bo, bl = b[path]
            if al != bl:
                out.append(path)
                continue
            fa.seek(ao)
            fb.seek(bo)
            if fa.read(al) != fb.read(bl):
                out.append(path)
    return out


def main() -> int:
    build = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "build" / "windows-clang"
    runtime = REPO / "build" / "windows" / "rando_runtime"
    if not (runtime / "python" / "python.exe").is_file():
        sys.exit(f"no randomizer runtime at {runtime}: run tools/rando/rando_runtime.py first")
    rando = build / "rando"
    for part in ("python", "site-packages", "wwrando", "gclib"):
        link_dir(runtime / part, rando / part)
    shutil.copy2(runtime / "manifest.json", rando / "manifest.json")
    for name in ("generate_seed.py", "patch_game.py", "describe_options.py", "prepare_extras.py", "link_colors.py",
                 "swift_sail.py", "swift_sail_data.json", "bunny_hood.py", "check_flags.json"):
        if (HERE / name).is_file():
            shutil.copy2(HERE / name, rando / name)
    if (rando / "bunny").exists():
        shutil.rmtree(rando / "bunny")
    shutil.copytree(HERE / "bunny", rando / "bunny", ignore=shutil.ignore_patterns("blender", "__pycache__"))
    import variant_sites
    (rando / "reference-options.json").write_text(json.dumps(variant_sites.REFERENCE_OPTIONS, indent=1) + "\n")

    options = build / "assets" / "randomizer" / "options.json"
    options.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, RANDO_RUNTIME=str(runtime))
    subprocess.run([str(runtime / "python" / "python.exe"), str(HERE / "describe_options.py"), str(options)],
                   check=True, env=env)

    # The Cosmetics Editor's Link colors and Swift Sail's textures, made from the disc (Boat of Daphnes' build.py steps).
    game_disc = build / "game" / "GZLE01.iso"
    if game_disc.is_file():
        penv = dict(os.environ, PYTHONPATH=str(runtime / "site-packages"))
        py = str(runtime / "python" / "python.exe")
        subprocess.run([py, str(HERE / "link_colors.py"), str(game_disc), str(build / "assets" / "link_colors"),
                        "--masks", str(runtime / "wwrando" / "assets" / "link_color_masks")], check=True, env=penv,
                       stdout=subprocess.DEVNULL)
        subprocess.run([py, str(HERE / "swift_sail.py"), str(game_disc), str(build / "assets" / "swift_sail")], check=True,
                       env=penv, stdout=subprocess.DEVNULL)
        # Boat of Daphnes' Bunny Hood: its model and icon from the disc's wall mask and Link (bunny_hood.py)
        subprocess.run([py, str(HERE / "bunny_hood.py"), str(game_disc), str(build / "assets" / "bunnyhood")], check=True,
                       env=penv, stdout=subprocess.DEVNULL)

    game = build / "game" / "rando"
    link_file(REPO / "build" / "rando" / "reference.iso", game / "GZLE99.iso")
    link_dir(REPO / "build" / "rando" / "orig" / "rel", game / "rels")
    # The files the two discs have differently (the game switches between them while it runs: hle_dvd.cpp).
    game_iso = build / "game" / "GZLE01.iso"
    if game_iso.is_file():
        changed = changed_files(game_iso, REPO / "build" / "rando" / "reference.iso")
        (game / "changed.txt").write_text("".join(p + "\n" for p in changed))
        print(f"{len(changed)} files differ between the discs")
    else:
        print(f"note: no {game_iso}: the randomizer cannot be switched to while the game runs")
    logo = REPO / "build" / "rando" / "ref" / "files" / "res" / "Object" / "TlogoE.arc"
    if logo.is_file():
        (game / "logo").mkdir(parents=True, exist_ok=True)
        shutil.copy2(logo, game / "logo" / "TlogoE.arc")
    subprocess.run([sys.executable, str(HERE / "module_diff.py"), str(REPO / "build" / "gen"), str(REPO / "build" / "gen_rando"),
                    str(build / "tww_game_rando.diff")], check=True)
    print(f"randomizer installed in {build}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
