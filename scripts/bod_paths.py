"""Where the developer tools find the things that are not in the repository.

Each name is read from the environment, then from local_paths.json at the repository root (not tracked: copy
local_paths.example.json and fill it in), then the tool's own fallback:

    BOD_DISC        your Wind Waker (USA) disc image, GZLE01 (.iso / .gcm / .rvz / ...)
    BOD_TWW_DECOMP  a checkout of the zeldaret/tww decompilation (symbol names, cutscene lists)
    BOD_WWRANDO     a checkout of LagoLunatic/wwrando (the randomizer runtime, Link colour masks)
    BOD_SCRATCH     a folder for test runs, benchmarks and release checks (default: build/scratch)
    BOD_MM_REF      reference meshes for the Bunny Hood generator's development tools (optional)
"""
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_FILE = ROOT / "local_paths.json"
_cache = None


def local_path(name: str, fallback: str = "") -> str:
    global _cache
    value = os.environ.get(name)
    if value:
        return value
    if _cache is None:
        try:
            _cache = json.loads(_FILE.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            _cache = {}
    return str(_cache.get(name) or fallback)


def scratch() -> Path:
    return Path(local_path("BOD_SCRATCH", str(ROOT / "build" / "scratch")))
