#!/usr/bin/env python3
"""Golden-image check of the UI at 21:9 (scenarios/ui_wide.txt): the HUD and two pause screens.

    python scripts/windows/ui_golden.py --exe EXE --module DLL --make   # from a build known to be right
    python scripts/windows/ui_golden.py --exe EXE --module DLL           # check a build against them

Each frame is compared with the golden one at the same retrace (the run is deterministic). A frame passes
when fewer than 0.5% of its pixels differ by more than 24 levels. 2026.10.16a's pause backdrop stopped at 4:3
and nothing caught it; this would have. A deliberate UI change means making the golden images again.
Testing only: the images stay in GOLDEN (not in the repository).
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops
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
GOLDEN = Path(os.environ.get("UI_GOLDEN", str(scratch() / "ui_golden")))
LIMIT = 0.005


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--exe", required=True)
    p.add_argument("--module", required=True)
    p.add_argument("--make", action="store_true")
    a = p.parse_args()
    tag = "golden" if a.make else "check"
    cmd = [sys.executable, str(ROOT / "scripts/windows/scenario.py"), "ui_wide", "--exe", a.exe, "--module", a.module,
           "--tag", tag]
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    frames = sorted((ROOT / "build/windows/scenarios/ui_wide").glob(f"*-{tag}/frame-*.png"))
    if len(frames) != 3:
        sys.exit(f"ui_wide: {len(frames)} frames, not 3\n" + "\n".join(res.stdout.splitlines()[-5:]))
    if a.make:
        GOLDEN.mkdir(parents=True, exist_ok=True)
        for f in GOLDEN.glob("*.png"):
            f.unlink()
        for k, f in enumerate(frames):
            shutil.copy2(f, GOLDEN / f"{k}.png")
        print(f"ui_golden: 3 golden frames saved in {GOLDEN}")
        return
    bad = 0
    for k, f in enumerate(frames):
        g = GOLDEN / f"{k}.png"
        x, y = Image.open(f).convert("RGB"), Image.open(g).convert("RGB")
        d = ImageChops.difference(x, y).convert("L").point(lambda v: 255 if v > 24 else 0)
        share = sum(1 for v in d.get_flattened_data() if v) / (x.size[0] * x.size[1])
        ok = share < LIMIT
        bad += not ok
        print(f"  frame {k} ({f.name}): {share:.2%} of pixels differ: {'ok' if ok else 'FAIL'}")
    print("ui_golden: PASS" if bad == 0 else f"ui_golden: FAIL ({bad} of 3)")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
