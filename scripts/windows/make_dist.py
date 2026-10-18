#!/usr/bin/env python3
"""Zip the packaged build for distribution: the release zip, and optionally a personal one.

    python scripts/windows/make_dist.py [--package build/windows/boat-of-daphnes] [--out build/windows/dist]
                                        [--version NAME] [--personal [--with-disc]]

release   (boat-of-daphnes-<version>-windows-x64.zip, the default and the one to publish): the way upstream BlueWake
          ships its Windows build. It holds the app, gGZLE01_recomp.dll (the game module, code translated from the
          game), the runtime DLLs, the embedded Python and wwrando runtime under rando\\ (which makes the randomizer's
          seeds and, on first launch, the files that come from the player's disc), licenses and a README that says
          "choose your disc on first launch". It holds nothing taken from a disc, no saves and no keys:
            - no game\\ folder (main.dol, the RELs, the disc image, the randomizer variant's RELs: all made on the
              player's PC at first launch from the disc they choose),
            - no assets\\swift_sail or assets\\link_colors (made from the player's disc at first launch),
            - no nodtool.exe (it embeds Wii keys; .rvz images are converted to ISO in Dolphin first),
            - no Save, mods, Randomizer, logs or cache folders (the player's),
            - no BuilderProvenance.json (the builder's optimization profile and build record).
personal  (boat-of-daphnes-<version>-personal.zip, with --personal): the whole package folder as the builder made
          it: the module, the prepared game files and the assets made from your disc, so it runs without first
          launch work. Without --with-disc the disc image is left out. Code and data from your own disc: for your own
          machines only, never to be shared or uploaded.
"""
from __future__ import annotations

import argparse
import datetime
import json
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "rando"))

USER_FOLDERS = {"save", "mods", "randomizer", "logs", "cache"}
# Made from a disc (at build time, or on the player's PC at first launch): never in the release.
DISC_DERIVED_DIRS = ("game", "assets/swift_sail", "assets/link_colors", "assets/bunnyhood")
# Builder-only files: the build record with the optimization profile's digest, the tool with keys, test scenarios.
BUILDER_ONLY = {"BuilderProvenance.json", "nodtool.exe", "bluewake_disc_extract.exe", "README.txt"}
BUILDER_ONLY_DIRS = ("scenarios",)
# The files first launch needs beside the embedded Python (scripts/rando/prepare_extras.py and what it runs).
EXTRAS_SCRIPTS = [("scripts/rando/prepare_extras.py", "prepare_extras.py"),
                  ("scripts/rando/patch_game.py", "patch_game.py"),
                  ("scripts/rando/generate_seed.py", "generate_seed.py"),
                  ("scripts/rando/describe_options.py", "describe_options.py"),
                  ("scripts/rando/check_logic_server.py", "check_logic_server.py"),  # the Check Tracker's reachability helper
                  ("scripts/rando/check_flags.json", "check_flags.json"),
                  ("scripts/windows/swift_sail.py", "swift_sail.py"),
                  ("scripts/windows/swift_sail_data.json", "swift_sail_data.json"),
                  ("scripts/windows/link_colors.py", "link_colors.py"),
                  ("scripts/rando/bunny_hood.py", "bunny_hood.py")] + [
    (f"scripts/rando/bunny/{n}", f"bunny/{n}")
    for n in ("bmd_write.py", "bti_c8.py", "bunny_hood_params.json", "hoodgen.py", "icon.py", "j3d_geom.py", "matgen.py",
              "rarc_write.py", "refmodel.py", "softrender.py", "vamuy.py", "matgen2.py", "surface.py", "restpose.py", "bck.py")]


def reference_options() -> str:
    """The reference run's options (variant_sites.REFERENCE_OPTIONS), as build.py's rando-patch step writes them."""
    import variant_sites
    return json.dumps(variant_sites.REFERENCE_OPTIONS, indent=1) + "\n"


def install_extras(rando: Path) -> None:
    """Put the first-launch tools into a package's rando\\ folder (build.py calls this too)."""
    rando.mkdir(parents=True, exist_ok=True)
    for source, name in EXTRAS_SCRIPTS:
        (rando / name).parent.mkdir(parents=True, exist_ok=True)
        (rando / name).write_bytes((ROOT / source).read_bytes())
    (rando / "reference-options.json").write_text(reference_options())


README = """boat-of-daphnes: The Legend of Zelda: The Wind Waker (GameCube, USA) for Windows,
statically recompiled. A fork of BlueWake, made to behave like Ship of Harkinian.

You need your own copy of the game: a disc image of the USA GameCube disc (GZLE01, revision 0), as an .iso or
.gcm file. A Dolphin .rvz must first be converted to ISO in Dolphin (right-click the game, Convert File).

To play:
  1. Unpack the whole zip into a folder you can write to (not Program Files).
  2. Run boat-of-daphnes.exe.
  3. On first launch, choose your disc image. The game checks it and prepares what it needs from it, once: the
     game's executable and modules, Swift Sail's textures, the Cosmetics Editor's data for Link's colors and the
     randomizer's game files. A progress window shows it (about a minute). The disc is read, never changed or
     copied into this folder. Your choice is remembered; choose another disc and it is prepared again.
  If a part cannot be made, the game still starts and only that feature says what is missing.

F1 or Esc opens the menu (Settings, Enhancements, Randomizer, Network, Dev Tools).

Kept beside the executable:
  Save\\         the memory card and save states
  mods\\         mods: folders or .zip/.rar/.7z (Settings > Mod Menu); Dolphin texture packs in mods\\textures\\
  Randomizer\\   randomizer seeds and spoiler logs (Randomizer > General > Generate Randomizer)
  logs\\         session logs (logs\\session-*.log: settings, scenes, events, frame rate, online play) and crash logs
  cache\\        shader and pipeline caches, and the files made from your disc (cache\\disc-import-*)
  rando\\        wwrando's seed generator with its own Python (see licenses\\)

Requires a Direct3D 12 GPU and a CPU with AVX2 (Intel Haswell from 2013, AMD Ryzen, or newer).

Online play: Network > Anchor. The default server is wwserver.viosarcade.xyz; players who type the same Room Name
play together, and players on the same Team share items and progress.

This zip contains code translated from the game (gGZLE01_recomp.dll) but no disc image, game assets, saves or
console keys. See licenses\\RIGHTS_AND_LICENSES.md.
"""

PERSONAL_README = """boat-of-daphnes: The Legend of Zelda: The Wind Waker (GameCube, USA) for Windows,
statically recompiled from the player's own disc. A fork of BlueWake, made to behave like Ship of Harkinian.

Run boat-of-daphnes.exe. F1 or Esc opens the menu (Settings, Enhancements, Randomizer, Network, Dev Tools).

This is a personal build: gGZLE01_recomp.dll is code translated from your own disc{disc}, and game\\, assets\\swift_sail
and assets\\link_colors are made from it. Keep it to your own machines: never share or upload it.
"""


def include(rel: str, release: bool, with_disc: bool) -> bool:
    parts = rel.split("/")
    if parts[0].lower() in USER_FOLDERS or "__pycache__" in parts:
        return False
    if parts[0] in BUILDER_ONLY_DIRS:
        return False
    if len(parts) == 1 and (parts[0] in BUILDER_ONLY if release else parts[0] in ("README.txt", "bluewake_disc_extract.exe")):
        return False  # the personal zip keeps the builder's own record and tools
    if rel.lower().endswith(".whl"):  # the wheels installed into site-packages, kept by the runtime builder
        return False
    if rel.lower().endswith((".iso", ".gcm", ".rvz")):
        return not release and with_disc
    if release and any(rel == d or rel.startswith(d + "/") for d in DISC_DERIVED_DIRS):
        return False
    return True


def write_zip(package: Path, path: Path, release: bool, with_disc: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    seen = set()
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for p in sorted(package.rglob("*")):
            if not p.is_file():
                continue
            rel = p.relative_to(package).as_posix()
            if not include(rel, release, with_disc):
                continue
            if release and rel.startswith("rando/") and \
                    rel[len("rando/"):] in {n for _, n in EXTRAS_SCRIPTS} | {"reference-options.json"}:
                continue  # written below from the source tree, so the release never carries a stale copy
            seen.add(rel)
            stored = rel.lower().endswith(".iso")
            z.write(p, rel, compress_type=zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED)
        if release:
            for source, name in EXTRAS_SCRIPTS:
                z.write(ROOT / source, f"rando/{name}")
            z.writestr("rando/reference-options.json", reference_options())
            z.writestr("README.txt", README)
        else:
            z.writestr("README.txt", PERSONAL_README.format(
                disc=" and game\\GZLE01.iso is your disc image" if with_disc else
                " (put your disc image at game\\GZLE01.iso)"))
        for notice in ("LICENSE", "RIGHTS_AND_LICENSES.md"):
            if (ROOT / notice).is_file():
                z.write(ROOT / notice, f"licenses/{notice}")
        if release:
            # The bundled runtime's own licenses, gathered where a player looks for them.
            for name, rel in (("wwrando-LICENSE.txt", "rando/wwrando/LICENSE.txt"),
                              ("gclib-LICENSE.txt", "rando/gclib/LICENSE.txt"),
                              ("Python-LICENSE.txt", "rando/python/LICENSE.txt")):
                if (package / rel).is_file():
                    z.write(package / rel, f"licenses/{name}")
    print(f"{'release' if release else 'personal'}: {path} ({path.stat().st_size / 1e6:.0f} MB)")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--package", type=Path, default=ROOT / "build/windows/boat-of-daphnes")
    p.add_argument("--out", type=Path, default=ROOT / "build/windows/dist")
    p.add_argument("--personal", action="store_true", help="also make the personal zip")
    p.add_argument("--with-disc", action="store_true", help="the personal zip also holds game\\GZLE01.iso")
    p.add_argument("--version", default=datetime.date.today().strftime("%Y%m%d"))
    args = p.parse_args()
    if not (args.package / "boat-of-daphnes.exe").is_file():
        sys.exit(f"no package at {args.package}")
    if not (args.package / "gGZLE01_recomp.dll").is_file():
        sys.exit("the package has no gGZLE01_recomp.dll")
    if not (args.package / "rando" / "python" / "python.exe").is_file():
        sys.exit("the package has no rando\\python (python scripts/windows/rando_runtime.py, then build.py)")
    write_zip(args.package, args.out / f"boat-of-daphnes-{args.version}-windows-x64.zip", True, False)
    if args.personal:
        write_zip(args.package, args.out / f"boat-of-daphnes-{args.version}-personal.zip", False, args.with_disc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
