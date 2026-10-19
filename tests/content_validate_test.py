"""Runs the tests of the custom player content validators (windows/src/content_validate.cpp, player_parts.cpp).

    python tests/content_validate_test.py [--iso ISO] [--mods DIR] [--work DIR]

  1. Compiles tests/content_validate_test.cpp with windows/src/{content_validate,player_parts,rarc}.cpp using the
     Visual Studio clang (as tests/model_colors_test.py: never cmake or ninja) and runs its synthetic cases:
     malformed, truncated, bit-flipped, oversized and path-traversal archives, manifests, Yaz0 bombs, audio files.
  2. With --iso (read only) it reads the game's own Link.arc; with --mods DIR it extracts, to a temporary folder, the
     Link.arc, JaiInit.aaf and *.aw of each wwrando-format mod found in the archives under DIR (7-Zip needed) and
     checks that every real file passes the validators and that the detection of Link / Equipment is printed.

Nothing is written outside --work (default: a temporary folder, deleted afterwards). Mods and the disc are read only
and never copied into the repository.
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

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts" / "windows"))
sys.path.insert(0, str(ROOT / "scripts"))
import build  # noqa: E402

try:
    from bod_paths import local_path  # noqa: E402
except ImportError:
    def local_path(name, fallback=""):
        return os.environ.get(name) or fallback

failures = 0
RUN_ENV = None


def run(exe, *args):
    global failures
    done = subprocess.run([str(exe)] + [str(a) for a in args], capture_output=True, text=True, env=RUN_ENV)
    print(done.stdout, end="")
    print(done.stderr, end="")
    if done.returncode != 0:
        failures += 1
    return done


def compile_test(env, exe):
    clang = shutil.which("clang++", path=env["PATH"])
    src = ROOT / "windows" / "src"
    sources = [ROOT / "tests" / "content_validate_test.cpp"] + [src / n for n in ("content_validate.cpp", "player_parts.cpp", "rarc.cpp", "voice_bank.cpp")]
    command = [clang, "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
               "-Wall", "-Wextra", "-Wno-unused-parameter", "-I" + str(ROOT / "runtime" / "recompcore" / "Externals" / "picojson")] + \
        [str(s) for s in sources] + ["-o", str(exe)]
    done = subprocess.run(command, env=env, capture_output=True, text=True)
    print(done.stdout + done.stderr, end="")
    if done.returncode != 0:
        # AddressSanitizer needs its runtime on the path; fall back to a plain build.
        command = [c for c in command if not c.startswith("-fsanitize") and not c.startswith("-fno-sanitize")]
        done = subprocess.run(command, env=env, capture_output=True, text=True)
        print(done.stdout + done.stderr, end="")
        if done.returncode != 0:
            sys.exit("compile failed")
        print("(built without the sanitizers)")


def vanilla_link(iso, work):
    """The game's Link.arc from the disc (read only), into work."""
    import struct
    out = work / "vanilla_Link.arc"
    with open(iso, "rb") as f:
        f.seek(0x424)
        fst_o, fst_s = struct.unpack(">II", f.read(8))
        f.seek(fst_o)
        fst = f.read(fst_s)
        n = struct.unpack(">I", fst[8:12])[0]
        names = n * 12
        stack = []
        for i in range(1, n):
            e = fst[i * 12:i * 12 + 12]
            name_off = int.from_bytes(e[1:4], "big")
            name = fst[names + name_off:fst.index(b"\0", names + name_off)].decode()
            a, b = struct.unpack(">II", e[4:])
            while stack and i >= stack[-1][0]:
                stack.pop()
            if e[0] == 1:
                stack.append((b, name))
            elif "/".join([s[1] for s in stack] + [name]) == "res/Object/Link.arc":
                f.seek(a)
                out.write_bytes(f.read(b))
                return out
    return None


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--iso", default=local_path("BOD_DISC"))
    p.add_argument("--mods", type=Path)
    p.add_argument("--work", type=Path)
    args = p.parse_args()
    env = build.Builder(types.SimpleNamespace(out=ROOT / "build" / "unused")).visual_studio_env()
    global RUN_ENV
    RUN_ENV = dict(env)  # the sanitizer runtime sits beside the Visual Studio clang
    clang_dir = Path(shutil.which("clang++", path=env["PATH"])).parent
    for rt in clang_dir.parent.glob("lib/clang/*/lib/windows"):
        RUN_ENV["PATH"] = str(rt) + os.pathsep + RUN_ENV["PATH"]
    work = args.work or Path(tempfile.mkdtemp(prefix="content_validate_test_"))
    work.mkdir(parents=True, exist_ok=True)
    try:
        exe = work / "content_validate_test.exe"
        compile_test(env, exe)
        run(exe)
        if args.mods and args.mods.is_dir():
            sevenzip = shutil.which("7z") or r"C:\Program Files\7-Zip\7z.exe"
            arcs, aafs, aws = [], [], []
            for i, archive in enumerate(sorted(args.mods.iterdir())):
                if archive.suffix.lower() not in (".zip", ".rar", ".7z"):
                    continue
                dest = work / f"m{i}"
                subprocess.run([sevenzip, "x", "-y", f"-o{dest}", str(archive), "-xr!preview", "-xr!color_masks", "-xr!*.png"],
                               capture_output=True)
                for f in dest.rglob("*"):
                    n = f.name.lower()
                    if n == "link.arc":
                        arcs.append(f)
                    elif n == "jaiinit.aaf":
                        aafs.append(f)
                    elif n.endswith(".aw"):
                        aws.append(f)
            if arcs:
                run(exe, "arc", *arcs)
            if aafs:
                run(exe, "aaf", *aafs)
            if aws:
                run(exe, "wave", *aws)
            # each voice bank (JaiInit.aaf beside voice_0.aw) must decode
            pairs = []
            for a in aafs:
                w = a.with_name("voice_0.aw")
                if w.exists():
                    pairs += [a, w]
            if pairs:
                run(exe, "voice", *pairs)
            vanilla = vanilla_link(args.iso, work) if args.iso and os.path.exists(args.iso) else None
            if vanilla and arcs:
                run(exe, "parts", vanilla, *arcs)
    finally:
        if args.work is None:
            shutil.rmtree(work, ignore_errors=True)
    print("FAILED: %d" % failures if failures else "ALL PASSED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
