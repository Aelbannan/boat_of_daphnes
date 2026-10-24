"""Load the local MM reference dump (made by a developer-side extractor from the player's own MM ROM; never
committed) into softrender meshes.  Folder: BOD_MM_REF (scripts/bod_paths.py)"""
from __future__ import annotations
import json
import numpy as np
from PIL import Image
import sys as _bod_sys
from pathlib import Path as _BodPath
_bod_sys.path.insert(0, str(_BodPath(__file__).resolve().parents[2]))
try:
    from bod_paths import local_path, scratch  # noqa: E402  (scripts/bod_paths.py)
except ImportError:  # a release's copy of this script, without the repository's scripts/ beside it
    import os as _bod_os

    def local_path(name, fallback=""):
        return _bod_os.environ.get(name) or fallback

    def scratch():
        return _BodPath(local_path("BOD_SCRATCH", "scratch"))

MM_DIR = local_path("BOD_MM_REF")


def load(mm_dir: str = MM_DIR, groups=("body", "hood_world")):
    meta = json.load(open(f"{mm_dir}/mm_link_child_meta.json"))
    z = np.load(f"{mm_dir}/mm_link_child.npz")
    texcache = {}
    out = []
    for m in meta["meshes"]:
        if m["group"] not in groups:
            continue
        k = "m%03d" % m["id"]
        t = m["tex"]
        tex = None
        if t is not None and t >= 0:
            if t not in texcache:
                texcache[t] = Image.open(f"{mm_dir}/tex/tex_{t:02d}.png").convert("RGBA")
            tex = texcache[t]
        # MM: Link faces +Z, left is +X, up +Y.  The hat limb is part of 'body'.
        clr = z[k + "_clr"].copy()
        out.append(dict(pos=z[k + "_pos"], nrm=z[k + "_nrm"], uv=z[k + "_uv"], clr=clr, tex=tex,
                        wrap=m["wrap"], double=m["double"], limb=m["limb"], group=m["group"], vcolor=True))
    return out, z
