"""wwrando's patched game files without an ISO (milestone R2, input step).

Needs the offline runtime (python scripts/windows/rando_runtime.py, which also fetches wwrando's assets) and the
player's GZLE01 disc (the path in scripts/rando/patch_game.py, or the BLUEWAKE_ISO environment variable); skips
when either is missing. Patches two seeds with default options and checks that
  * the same seed gives byte-identical files (the patch step is deterministic),
  * a different seed changes the DOL and RELs only at the sites listed in scripts/rando/seed_sites.json,
  * the DOL gained the free-space text section and two RELs were added.
"""
import hashlib
import json
import os
import subprocess
import tempfile
import unittest
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

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "build" / "windows" / "rando_runtime"
PYTHON = RUNTIME / "python" / "python.exe"
SCRIPT = ROOT / "scripts" / "rando" / "patch_game.py"
ISO = os.environ.get("BLUEWAKE_ISO") or local_path("BOD_DISC")
HAVE = (PYTHON.is_file() and (RUNTIME / "wwrando" / "assets" / "Vscroll.arc").is_file() and Path(ISO).is_file())


def patch(out: Path, seed: str):
    r = subprocess.run([str(PYTHON), str(SCRIPT), "--iso", ISO, "--out", str(out), "--seed", seed, "--files", "code"],
                       capture_output=True, text=True, timeout=600)
    if r.returncode != 0:
        raise AssertionError(r.stdout + r.stderr[-2000:])
    return json.loads(r.stdout.strip().splitlines()[-1])


def tree_hash(out: Path) -> dict:
    return {p.relative_to(out).as_posix(): hashlib.sha1(p.read_bytes()).hexdigest()
            for p in sorted(out.rglob("*")) if p.is_file() and p.name != "patch_manifest.json"}


@unittest.skipUnless(HAVE, "rando runtime (with assets) or the disc is missing")
class PatchGame(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        t = Path(cls.tmp.name)
        cls.a1, cls.a2, cls.b = t / "a1", t / "a2", t / "b"
        cls.res = patch(cls.a1, "RVA")
        patch(cls.a2, "RVA")
        patch(cls.b, "RVB")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_shape(self):
        self.assertEqual(self.res["counts"]["dol"], 1)
        self.assertGreaterEqual(self.res["counts"]["rel"], 80)
        self.assertTrue((self.a1 / "files/rels/d_a_switch_op.rel").is_file())
        self.assertTrue((self.a1 / "files/rels/d_a_dungeon_flag_sw.rel").is_file())
        dol = (self.a1 / "sys/main.dol").read_bytes()
        self.assertEqual(int.from_bytes(dol[0x08:0x0C], "big"), 0x003A52C0)  # text section 2 file offset
        self.assertEqual(int.from_bytes(dol[0x50:0x54], "big"), 0x803FCFA8)  # at the free space

    def test_deterministic(self):
        self.assertEqual(tree_hash(self.a1), tree_hash(self.a2))

    def test_seed_changes_only_listed_sites(self):
        sites = json.loads((ROOT / "scripts" / "rando" / "seed_sites.json").read_text())["seed_sites"]
        allowed = {}
        for s in sites:
            lo = s["file_offset"]
            allowed.setdefault(s["file"], []).append((lo, lo + (4 if s["kind"] != "data" else s["length"])))
        h1, h2 = tree_hash(self.a1), tree_hash(self.b)
        self.assertEqual(set(h1), set(h2))
        for path in h1:
            if h1[path] == h2[path]:
                continue
            name = "main.dol" if path == "sys/main.dol" else Path(path).name
            d1, d2 = (self.a1 / path).read_bytes(), (self.b / path).read_bytes()
            if name != "main.dol":
                self.assertEqual(len(d1), len(d2), name)
            # REL differences outside the listed sites would be new seed-dependent sites (or relocation-table churn)
            diffs = [i for i in range(min(len(d1), len(d2))) if d1[i] != d2[i]]
            stray = [i for i in diffs if not any(a <= i < b for a, b in allowed.get(name, []))]
            self.assertEqual(stray, [], f"{name}: bytes outside the known seed sites differ: {stray[:8]}")


if __name__ == "__main__":
    unittest.main()
