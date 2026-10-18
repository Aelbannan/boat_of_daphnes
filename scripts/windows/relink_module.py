#!/usr/bin/env python3
"""Rebuild only what changed in the game module, and relink it. Minutes, not the 70 minute full build.

    python scripts/windows/relink_module.py [--out build/windows] [--jobs N] [--install DLL_PATH ...] [--max-objects 40]

build.py re-runs the translation, the mods and the block preparation before it compiles. The compile itself is
already incremental (Ninja, in OUT/composite), so when a change touches only statically linked runtime sources
(runtime/recompcore/GXRuntime/src/core/cpu.c and its siblings, cmake/composite/*.c that no chunk includes, for
example direct_calls.c, native_*.c, guest_cpu.c, module_export.c) the translated chunks stay as they are and Ninja
recompiles those few objects and relinks gGZLE01_recomp.dll. This script does exactly that, from the build tree
the last full build left (same flags, same optimization profile: the CMake cache is reused as it is).

It counts what Ninja would rebuild first (`ninja -n`) and stops when that is more than --max-objects: a header
that every chunk includes (direct_calls.h, cpu.h, inline_*.h ...) or a changed composite-src means the chunks
recompile (about an hour), and that is a build.py job. The sources must be those of the checkout that owns OUT
(the CMake cache holds absolute paths, so a build tree cannot be used from another worktree).

--install copies the new module to the given paths (a lab folder, the packaged app). The module is not a new
package: BuilderProvenance.json beside it is not updated.
"""
import argparse
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build as builder  # noqa: E402
from machine_lock import machine_lock  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", type=Path, default=builder.ROOT / "build/windows")
    p.add_argument("--jobs", type=int, default=10)
    p.add_argument("--max-objects", type=int, default=40, help="refuse to rebuild more objects than this")
    p.add_argument("--install", type=Path, nargs="*", default=[], help="copy the new module to these files")
    p.add_argument("--dry-run", action="store_true", help="only list what would be rebuilt")
    a = p.parse_args()
    build = a.out.resolve() / "composite"
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        sys.exit(f"{build} has no CMake cache: run build.py once first")
    home = re.search(r"(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", cache.read_text(errors="replace")).group(1)
    if Path(home).resolve() != (builder.ROOT / "cmake/composite").resolve():
        sys.exit(f"this build tree was configured from {home}; run the script from that checkout")
    namespace = argparse.Namespace(out=a.out.resolve(), jobs=a.jobs)
    b = builder.Builder(namespace)
    env = b.visual_studio_env()
    ninja = shutil.which("ninja", path=env["PATH"])
    cmake = shutil.which("cmake", path=env["PATH"])
    dry = subprocess.run([ninja, "-C", str(build), "-n", "-k", "0"], capture_output=True, text=True, env=env).stdout
    todo = re.findall(r"(?m)^\[\d+/\d+\] (.*)$", dry)
    objects = [t for t in todo if "Building C object" in t]
    print(f"Ninja would run {len(todo)} steps, {len(objects)} of them compiles:")
    for t in objects[:12]:
        print("  " + t[:160])
    if a.dry_run:
        return
    if len(objects) > a.max_objects:
        sys.exit(f"{len(objects)} objects are out of date (> {a.max_objects}): a header or the composite source "
                 f"changed; use build.py")
    start = time.monotonic()
    with machine_lock("build"):
        # re-run CMake only when its inputs changed (Ninja does that on its own); here just build
        status = subprocess.run([cmake, "--build", str(build), "-j", str(a.jobs)], env=env).returncode
    if status != 0:
        sys.exit(f"the build failed ({status})")
    module = build / builder.MODULE
    print(f"{module} ({module.stat().st_size >> 20} MB) in {int(time.monotonic() - start)} s")
    for target in a.install:
        shutil.copy2(module, target)
        print(f"installed {target}")


if __name__ == "__main__":
    main()
