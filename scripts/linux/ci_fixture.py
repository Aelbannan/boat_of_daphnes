#!/usr/bin/env python3
"""The CI's stand-in for a built app folder, to package and audit without a disc.

    python3 scripts/linux/ci_fixture.py APP_BUILD_DIR RANDO_RUNTIME OUT

APP_BUILD_DIR holds the built boat-of-daphnes (the CMake build folder); RANDO_RUNTIME is the randomizer runtime made by
scripts/windows/rando_runtime.py. OUT/boat-of-daphnes gets the program, the DSP ROMs, the randomizer's generator with its
first-launch tools and menu data, and gGZLE01_recomp.so as an EMPTY shared object: the packaging and its audit are
exercised, the game is not. There is no disc, main.dol, REL or any other file made from the game here, and the
stand-in is far too small to be a module (the audit refuses a bigger one).
"""
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    build, runtime, out = (Path(a).resolve() for a in argv)
    app = out / "boat-of-daphnes"
    shutil.rmtree(out, ignore_errors=True)
    (app / "dsp").mkdir(parents=True)
    shutil.copy2(build / "boat-of-daphnes", app / "boat-of-daphnes")
    if (build / "initial_pipeline_cache.db").is_file():
        shutil.copy2(build / "initial_pipeline_cache.db", app / "initial_pipeline_cache.db")
    for name in ("dsp_rom.bin", "dsp_coef.bin"):
        shutil.copy2(ROOT / "runtime/recompcore/Data/Sys/GC" / name, app / "dsp" / name)

    # The randomizer exactly as the builder packages it (scripts/linux/build.py's LinuxBuilder.randomizer).
    spec = importlib.util.spec_from_file_location("bod_linux_build", ROOT / "scripts/linux/build.py")
    linux = importlib.util.module_from_spec(spec)
    sys.modules["bod_linux_build"] = linux
    spec.loader.exec_module(linux)
    builder = linux.LinuxBuilder.__new__(linux.LinuxBuilder)
    builder.out = runtime.parent
    if runtime.name != "rando_runtime":
        raise SystemExit("the runtime folder must be named rando_runtime")
    builder.randomizer(app)
    if not (app / "assets/randomizer/options.json").is_file():
        raise SystemExit("the randomizer's menu data was not made: the runtime does not work")

    # The empty stand-in for the game module.
    stub = app / linux.wb.MODULE
    subprocess.run(["clang", "-shared", "-fPIC", "-x", "c", "/dev/null", "-o", str(stub)], check=True)
    print(f"ci_fixture: {app} (stand-in module {stub.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
