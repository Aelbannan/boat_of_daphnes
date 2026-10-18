#!/usr/bin/env python3
"""HUD audit: run HUD scenarios across window shapes and the picture settings, and make one contact sheet
per scenario for review.

    python scripts/windows/hud_audit.py [scenario ...] [--configs a43,a169,...] [--sheets-only]

Each config is a window size plus settings (Force Aspect Ratio, Internal Resolution). The scenario runner
(scenario.py) runs each scenario under each config into build/windows/scenarios/<name>/<size>-<tag>/; this
script then lays the frames of every config out as rows of build/windows/hud_audit/<name>.png, labelled,
each frame at the same height so the HUD's placement compares across shapes. Without scenario names it runs
the audit list below. Pillow is needed for the sheets.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# (tag, size, settings, label). "Adaptive" is Force Aspect Ratio off: the picture follows the window.
CONFIGS = [
    ("a54", "1280x1024", {}, "adaptive 5:4 (narrower than 4:3)"),
    ("a43", "1440x1080", {}, "adaptive 4:3"),
    ("a1610", "1920x1200", {}, "adaptive 16:10"),
    ("a169", "1920x1080", {}, "adaptive 16:9"),
    ("a219", "2560x1080", {}, "adaptive 21:9"),
    ("a329", "3840x1080", {}, "adaptive 32:9"),
    ("o43", "1920x1080", {"gSettings.Graphics.ForceAspectRatio": 1}, "16:9 window, forced Original 4:3"),
    ("f169", "2560x1080", {"gSettings.Graphics.ForceAspectRatio": 3}, "21:9 window, forced 16:9"),
    ("ir50", "1920x1080", {"gSettings.Graphics.InternalResolution": 50}, "adaptive 16:9, internal resolution 50%"),
    ("ir200", "2560x1080", {"gSettings.Graphics.InternalResolution": 200}, "adaptive 21:9, internal resolution 200%"),
]

# The scenarios that put the HUD in each state that changes it: the hudq_* scenarios
# (docs/fork/hud-audit-conditions.md).
AUDIT = sorted(p.stem for p in (ROOT / "scripts/windows/scenarios").glob("hudq_*.txt"))

ROW_HEIGHT = 270


def run(names, configs, extra) -> None:
    for tag, size, settings, _ in configs:
        cmd = [sys.executable, str(ROOT / "scripts/windows/scenario.py"), *names, "--size", size, "--tag", tag, *extra]
        for key, value in settings.items():
            cmd += ["--cvar", f"{key}={value}"]
        print(f"== {tag}: {' '.join(names)}", flush=True)
        subprocess.run(cmd, cwd=ROOT)


def sheets(names, configs, out: Path) -> None:
    from PIL import Image, ImageDraw

    out.mkdir(parents=True, exist_ok=True)
    for name in names:
        rows = []
        for tag, size, _, label in configs:
            folder = ROOT / "build/windows/scenarios" / name / f"{size}-{tag}"
            frames = sorted(folder.glob("frame-*.png"), key=lambda p: int(p.stem.split("-")[1]))
            images = []
            for f in frames:
                im = Image.open(f).convert("RGB")
                images.append(im.resize((max(1, im.width * ROW_HEIGHT // im.height), ROW_HEIGHT)))
            rows.append((f"{tag}  {size}  {label}" + ("" if images else "  (no frames)"), images))
        width = max([sum(i.width + 6 for i in imgs) for _, imgs in rows] + [400])
        sheet = Image.new("RGB", (width, len(rows) * (ROW_HEIGHT + 22)), (24, 24, 24))
        draw = ImageDraw.Draw(sheet)
        for r, (label, images) in enumerate(rows):
            y = r * (ROW_HEIGHT + 22)
            draw.text((4, y + 4), label, fill=(240, 240, 240))
            x = 0
            for im in images:
                sheet.paste(im, (x, y + 20))
                x += im.width + 6
        path = out / f"{name}.png"
        sheet.save(path)
        print(f"sheet: {path}")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("scenarios", nargs="*")
    p.add_argument("--configs", help="comma-separated tags (default: all)")
    p.add_argument("--sheets-only", action="store_true", help="only make the sheets from earlier runs")
    p.add_argument("--out", type=Path, default=ROOT / "build/windows/hud_audit")
    args, extra = p.parse_known_args()
    names = args.scenarios or AUDIT
    configs = [c for c in CONFIGS if not args.configs or c[0] in args.configs.split(",")]
    if not args.sheets_only:
        run(names, configs, extra)
    sheets(names, configs, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
