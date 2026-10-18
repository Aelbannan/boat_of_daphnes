"""Runs the tests of windows/src/model_colors.cpp (the colors of a player model).

    python tests/model_colors_test.py [--iso ISO] [--wwrando DIR] [--xxhash DIR] [--work DIR]
                                      [--model FOLDER[=HERO,CASUAL,HERO_PRESETS,CASUAL_PRESETS,EYEBROWS]]...

What it does, with the Visual Studio clang (the environment build.py uses; it never runs cmake or
ninja and never touches a build directory):

  1. Compiles tests/model_colors_test.cpp with windows/src/{model_colors,gx_texture,png_decode,rarc}.cpp
     into --work. xxhash is the header only (XXH_INLINE_ALL): give --xxhash the folder with xxhash.h
     (default: the one a build of this checkout fetched, build/windows/app/_deps/xxhash-src).
  2. Vanilla Link: scripts/windows/link_colors.py makes its index, RGBA textures and masks from the
     disc (--iso, the USA GZLE01; read only) and wwrando's color masks (--wwrando); Link.arc is read
     from the same disc. The C++ builds the vanilla Link from Link.arc, wwrando's link_metadata.txt and
     the masks, and must give the same keys, the same RGBA and the same masks.
  3. Each --model folder (Link.arc, metadata.txt, color_masks\\, extracted from a .zip/.rar/.7z first;
     the expected color and preset counts follow "=" when given): the metadata parses (old format
     too), every picture in color_masks is used, every mask is the size of its texture. Then both
     recolor scenarios are run in C++ and in Python (tests/model_colors_reference.py: wwrando's
     customizer logic and color exchange on the pure Python decoders and Pillow) and the recolored
     pixels must be identical.

Nothing is written outside --work (default: a temporary folder, deleted afterwards). The disc, the
models and the masks are read only and never copied into the repository.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import types
from pathlib import Path
import sys as _bod_sys
from pathlib import Path as _BodPath
_bod_sys.path.insert(0, str(_BodPath(__file__).resolve().parents[1] / 'scripts'))
try:
    from bod_paths import local_path, scratch  # noqa: E402  (scripts/bod_paths.py)
except ImportError:  # a release's copy of this script, without the repository's scripts/ beside it
    import os as _bod_os

    def local_path(name, fallback=""):
        return _bod_os.environ.get(name) or fallback

    def scratch():
        return _BodPath(local_path("BOD_SCRATCH", "scratch"))

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts" / "windows"))
sys.path.insert(0, str(ROOT / "tests"))

import build  # noqa: E402
import link_colors as L  # noqa: E402
import model_colors_reference as R  # noqa: E402
import swift_sail as S  # noqa: E402

DEFAULT_ISO = local_path("BOD_DISC")
DEFAULT_WWRANDO = local_path("BOD_WWRANDO")

failures = 0


def check(ok: bool, message: str):
    global failures
    if not ok:
        failures += 1
        print("FAIL " + message)


def compile_test(env: dict, xxhash: Path, exe: Path):
    clang = shutil.which("clang++", path=env["PATH"])
    sources = [ROOT / "tests" / "model_colors_test.cpp"] + [
        ROOT / "windows" / "src" / n for n in ("model_colors.cpp", "gx_texture.cpp", "png_decode.cpp", "rarc.cpp")]
    command = [clang, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter", "-DXXH_INLINE_ALL",
               "-I" + str(xxhash), "-I" + str(ROOT / "runtime" / "recompcore" / "Externals" / "picojson")] + \
        [str(s) for s in sources] + ["-o", str(exe)]
    done = subprocess.run(command, env=env, capture_output=True, text=True)
    print(done.stdout + done.stderr, end="")
    if done.returncode != 0:
        sys.exit("compile failed")


def run(exe: Path, *args) -> bool:
    done = subprocess.run([str(exe)] + [str(a) for a in args], capture_output=True, text=True)
    print(done.stdout, end="")
    print(done.stderr, end="")
    return done.returncode == 0


def test_vanilla(exe: Path, iso: str, wwrando: str, work: Path):
    print("== vanilla Link: C++ against scripts/windows/link_colors.py ==")
    masks = os.path.join(wwrando, "assets", "link_color_masks")
    out = work / "link_colors"
    L.build(iso, str(out), masks, warn=lambda m: check(False, "link_colors.py: " + m))
    disc = S.Disc(iso)
    try:
        arc = S.yaz0_decompress(disc.read(L.LINK_ARC))
    finally:
        disc.close()
    (work / "Link_vanilla.arc").write_bytes(arc)
    check(run(exe, "vanilla", work / "Link_vanilla.arc", out, os.path.join(wwrando, "assets", "link_metadata.txt"), masks),
          "vanilla comparison")


def test_model(exe: Path, spec: str, work: Path):
    folder, _, expect = spec.partition("=")
    print("== model %s ==" % folder)
    counts = expect.split(",") if expect else ["-1"] * 5
    if len(counts) != 5:
        sys.exit("--model FOLDER=HERO,CASUAL,HERO_PRESETS,CASUAL_PRESETS,EYEBROWS")
    check(run(exe, "model", folder, *counts), "%s: metadata and masks" % folder)
    model = R.Model(folder)
    for scenario in ("both", "casual"):
        dump = work / ("recolor_%s_%s.bin" % (Path(folder).name, scenario))
        check(run(exe, "recolor", folder, scenario, dump), "%s %s: recolor" % (folder, scenario))
        got = R.read_dump(str(dump))
        want = model.scenario(scenario)
        check(set(got) == set(want), "%s %s: textures %s, reference %s" % (folder, scenario, sorted(got), sorted(want)))
        same = 0
        for name in sorted(set(got) & set(want)):
            if got[name] == want[name]:
                same += 1
            else:
                check(False, "%s %s: %s differs from the reference" % (folder, scenario, name))
        print("  %s recolor: %d of %d textures identical to the Python reference (%s)" % (
            scenario, same, len(want), ", ".join(sorted(want))))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iso", default=DEFAULT_ISO)
    ap.add_argument("--wwrando", default=DEFAULT_WWRANDO)
    ap.add_argument("--xxhash", type=Path, default=ROOT / "build" / "windows" / "app" / "_deps" / "xxhash-src")
    ap.add_argument("--work", type=Path)
    ap.add_argument("--model", action="append", default=[])
    args = ap.parse_args()
    if not (args.xxhash / "xxhash.h").exists():
        sys.exit("xxhash.h not found in %s: pass --xxhash" % args.xxhash)
    env = build.Builder(types.SimpleNamespace(out=ROOT / "build" / "unused")).visual_studio_env()
    work = args.work or Path(tempfile.mkdtemp(prefix="model_colors_test_"))
    work.mkdir(parents=True, exist_ok=True)
    try:
        exe = work / "model_colors_test.exe"
        compile_test(env, args.xxhash, exe)
        if os.path.exists(args.iso):
            test_vanilla(exe, args.iso, args.wwrando, work)
        else:
            print("no disc at %s: the vanilla comparison is skipped" % args.iso)
        for spec in args.model:
            test_model(exe, spec, work)
    finally:
        if args.work is None:
            shutil.rmtree(work, ignore_errors=True)
    print("FAILED: %d" % failures if failures else "ALL PASSED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
