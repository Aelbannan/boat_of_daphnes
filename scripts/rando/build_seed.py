#!/usr/bin/env python3
"""One command: a randomizer seed in, a playable per-seed build of the game out (docs/randomizer.md).

    python scripts/rando/build_seed.py --seed SEED [--options opts.json] [--set name=value ...] [--disc DISC]
    python scripts/rando/build_seed.py --permalink PERMALINK
    python scripts/rando/build_seed.py --seed SEED --reuse build/windows-rando/OTHER     # a second seed, faster

1. wwrando's own patcher runs on your disc and writes its randomized disc (GZLE99), spoiler log and
   non-spoiler log into build/windows-rando/SEED/ (about 10 seconds).
2. scripts/windows/build.py --rando-iso builds the game from that disc's patched main.dol and RELs: translate,
   composite, widescreen and Better Wind Waker variants, compile, app, package. The first build takes about an
   hour; a build made with --reuse (another seed's folder; it is copied) recompiles only the chunks the seed changed.

The package is build/windows-rando/SEED/boat-of-daphnes/ (run boat-of-daphnes.exe); the spoiler log is next to the
disc in build/windows-rando/SEED/. Everything under build/ is git-ignored: the build holds your disc's code.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
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

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DISC = local_path("BOD_DISC")
MAIN_PROFILE = str(Path(__file__).resolve().parents[2] / "build" / "windows" / "pgo-local")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed")
    ap.add_argument("--permalink")
    ap.add_argument("--options")
    ap.add_argument("--set", action="append", default=[], metavar="NAME=VALUE")
    ap.add_argument("--disc", default=DEFAULT_DISC, help="your vanilla GZLE01 disc (.iso)")
    ap.add_argument("--out", type=Path, help="build folder (default build/windows-rando/SEED)")
    ap.add_argument("--reuse", type=Path, help="a finished rando build folder to start from (copied)")
    ap.add_argument("--profile", type=Path, help="a pgo-local folder with a trained composite.profdata "
                                                  "(default: the one in this folder, --reuse's, or the main checkout's)")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--no-mods", action="store_true", help="without widescreen and Better Wind Waker's options")
    ap.add_argument("--generate-only", action="store_true", help="only the disc and logs, no build")
    args = ap.parse_args()
    if not (args.seed or args.permalink):
        ap.error("--seed or --permalink")

    patch = [sys.executable, str(ROOT / "scripts/rando/patch_game.py"), "--iso", args.disc]
    if args.permalink:
        patch += ["--permalink", args.permalink]
    else:
        patch += ["--seed", args.seed]
        if args.options:
            patch += ["--options", args.options]
        for s in args.set:
            patch += ["--set", s]
    # The folder is named by the seed (the permalink's seed is read from wwrando's answer).
    staging = ROOT / "build/windows-rando/_new"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)
    done = subprocess.run(patch + ["--out", str(staging), "--iso-out", str(staging / "seed.iso")],
                          capture_output=True, text=True)
    sys.stderr.write(done.stderr[-3000:])
    if done.returncode != 0:
        print(done.stdout or done.stderr)
        sys.exit(done.returncode)
    result = json.loads(done.stdout.strip().splitlines()[-1])
    seed = result["seed"]
    out = (args.out or ROOT / "build/windows-rando" / seed).resolve()
    out.mkdir(parents=True, exist_ok=True)
    for f in staging.iterdir():
        target = out / ("disc.iso" if f.name == "seed.iso" else f.name)
        if target.exists():
            target.unlink()
        shutil.move(str(f), str(target))
    shutil.rmtree(staging, ignore_errors=True)
    (out / "permalink.txt").write_text(result["permalink"] + "\n")
    print(f"seed {seed}: disc and logs in {out}\npermalink {result['permalink']}", flush=True)
    if args.generate_only:
        return

    if args.reuse and args.reuse.resolve() != out:
        for part in ("app", "dolrecomp", "tools", "composite", "game", "game.json", "translated", "composite-src",
                     "composite-final.digest", "composite-inputs.digest", "composite-src.digest", "mods.done",
                     "mods", "prepared-blocks.json", "rando_runtime", "pgo-local"):
            src, dst = args.reuse / part, out / part
            if src.is_dir() and not dst.exists():
                shutil.copytree(src, dst)
            elif src.is_file() and not dst.exists():
                shutil.copy2(src, dst)
    profile = args.profile or next((p for p in (out / "pgo-local", Path(MAIN_PROFILE)) if (p / "composite.profdata").is_file()),
                                   None)
    if profile and not (out / "pgo-local" / "composite.profdata").is_file():
        (out / "pgo-local").mkdir(exist_ok=True)
        for name in ("composite.profdata", "training.json"):
            shutil.copy2(profile / name, out / "pgo-local" / name)
    command = [sys.executable, str(ROOT / "scripts/windows/build.py"), str(out / "disc.iso"), "--rando-iso", "--out",
               str(out), "--no-app-pgo", "--jobs", str(args.jobs)]
    if (out / "pgo-local" / "composite.profdata").is_file():
        command.append("--reuse-profile")
    else:
        command.append("--no-train")
        print("note: no trained optimization profile found; the build compiles without one (slower to build)")
    if args.no_mods:
        command.append("--no-mods")
    sys.exit(subprocess.run(command).returncode)


if __name__ == "__main__":
    main()
