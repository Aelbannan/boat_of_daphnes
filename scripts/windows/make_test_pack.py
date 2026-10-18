#!/usr/bin/env python3
"""Makes a small Dolphin-format texture pack for testing Enable Texture Packs, into build/testpack/.

    python scripts/windows/make_test_pack.py

The pack replaces the sail's textures (and the sail's icon) with a solid green: the files are named as Dolphin
names the textures (tex1_WxH_hash[_tlut]_fmt.png), copied from the names of the Swift Sail's textures in
assets/swift_sail of a package. Nothing is read from a disc and the pack is never committed (build/ is ignored).
Scenarios use it with `env BLUEWAKE_TEXTURES_DIR {root}/build/testpack`.
"""
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
NAMES = {
    "tex1_128x128_301e8acf5dcf785d_14.png": (128, 128),
    "tex1_128x128_3f34314685b4cb4d_e9a9f7e04ef9893f_9.png": (128, 128),
    "tex1_48x48_22e7a81247c4caa5_fe1212280187026e_9.png": (48, 48),
}


def main() -> int:
    out = ROOT / "build/testpack"
    out.mkdir(parents=True, exist_ok=True)
    for name, size in NAMES.items():
        Image.new("RGBA", size, (0, 200, 0, 255)).save(out / name)
    print(f"{len(NAMES)} textures in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
