"""verify_release.py: the pure parts (scenario rewriting, log analysis, the summary)."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts" / "windows"))
import verify_release as vr  # noqa: E402

LOG = """[scene] stage=sea_T room=44 point=0 layer=0 (stage change)
[fps] shown=61.0 game=61.0
[fps] shown=0.0 game=0.0
[fps] shown=29.0 game=29.0
[mods] the randomizer is on while the game runs: mask=0xA layout=rando, 55 REL data images changed
[music] main BGM 0x80000001 sub 0xFFFFFFFF audio scene 0 (set 0, scene lock 0) stage=sea room=44
[mods] the randomizer is off while the game runs: mask=0x2 layout=base, 18 REL data images changed
"""


BS = chr(92)
NL = chr(10)


class VerifyRelease(unittest.TestCase):
    def test_scenario_paths_are_moved(self):
        rc = Path(tempfile.gettempdir()) / "rc_test"
        text = "start boot raw" + NL + "env BLUEWAKE_DATA_DIR D:" + BS + "x" + BS + "scratch" + BS + "userdata_van" + NL + "env BLUEWAKE_CARD_PATH D:" + BS + "r" + BS + "build" + BS + "fs" + BS + "cards" + BS + "a.card" + NL + "end 5" + NL
        out, copies = vr.derive_scenario(text, rc, "vanilla_new")
        self.assertIn(f"env BLUEWAKE_DATA_DIR {rc / 'ud_van'}", out)
        self.assertIn(str(rc / "cards"), out)
        self.assertEqual(len(copies), 1)
        self.assertEqual(copies[0][0].name, "a.card")
        out, _ = vr.derive_scenario("env BLUEWAKE_DATA_DIR D:" + BS + "x" + BS + "userdata10b" + NL + "end 5" + NL, rc, "rando_new")
        self.assertIn(f"{rc / 'ud_rand'}", out)
        self.assertTrue(out.index("sub-BGM") < out.index("end 5"))  # the music expectations come before the end

    def test_log_analysis(self):
        a = vr.analyze_log(LOG)
        self.assertEqual(a["failures"], [])
        self.assertEqual((a["fps_min"], a["fps_mean"], a["fps_lines"]), (29.0, 45.0, 2))  # a 0 fps line (a pause) is not a rate
        self.assertEqual((a["switch_on"], a["switch_off"], a["stages"], a["outset_bgm"]), (["0xA"], ["0x2"], 1, "0x80000001"))
        self.assertTrue(vr.analyze_log(LOG + "[run] unmapped pc=0x80000000 lr=0x1\n")["failures"])

    def test_roam_has_ten_minutes(self):
        text, copies = vr.roam_scenario(Path("C:/rc"), Path("C:/lab"))
        self.assertEqual(text.count("teleport sea"), 2)
        waits = sum(int(l.split()[1]) for l in text.splitlines() if l.startswith("wait "))
        sticks = sum(int(l.split()[3]) for l in text.splitlines() if l.startswith("stick "))
        self.assertGreater(waits + sticks, 36000)

    def test_summary_and_exit(self):
        results = [{"id": "a", "passed": True, "analysis": vr.analyze_log(LOG)},
                   {"id": "b", "passed": False, "why": "x", "excerpt": ["line"], "analysis": {}}]
        text = vr.render_summary("t", "z.zip", results, {"passed": True, "detail": "d"})
        self.assertIn("RESULT: FAIL (1/2 paths; failed: b)", text)
        self.assertIn("## FAIL b", text)


if __name__ == "__main__":
    unittest.main()
