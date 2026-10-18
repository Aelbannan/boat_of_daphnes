"""Seed generation through wwrando's own generator (milestone R1).

Needs the offline runtime: python scripts/windows/rando_runtime.py (skips when it is not fetched).
Runs 3 default seeds and 2 non-default ones, each twice in separate processes with different hash seeds, and checks
that the outputs are byte-identical and that seed.json agrees with wwrando's spoiler log.
"""
import json
import os
import re
import subprocess
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "build" / "windows" / "rando_runtime"
PYTHON = RUNTIME / "python" / "python.exe"
SCRIPT = ROOT / "scripts" / "rando" / "generate_seed.py"
HAVE_RUNTIME = PYTHON.is_file() and (RUNTIME / "wwrando" / "randomizer.py").is_file() and (RUNTIME / "gclib" / "gclib").is_dir()

# name -> (seed, wwrando options)
CASES = {
    "default-1": ("bod-alpha", {}),
    "default-2": ("bod-bravo", {}),
    "default-3": ("Magic.Seed_42", {}),
    "keylunacy-swordless": ("bod-kl", {
        "keylunacy": True, "sword_mode": "Swordless", "num_extra_starting_items": 2, "hero_mode": True,
        "num_starting_triforce_shards": 2, "progression_short_sidequests": True, "progression_minigames": True,
        "progression_platforms_rafts": True, "progression_treasure_charts": True, "progression_triforce_charts": True,
    }),
    "entrances-all": ("bod-ent", {
        "randomize_dungeon_entrances": True, "randomize_secret_cave_entrances": True, "randomize_miniboss_entrances": True,
        "randomize_boss_entrances": True, "randomize_secret_cave_inner_entrances": True, "randomize_fairy_fountain_entrances": True,
        "mix_entrances": "Mix Dungeons & Caves & Fountains", "required_bosses": True, "num_required_bosses": 3,
        "randomize_charts": True, "randomize_starting_island": True, "trap_chests": True, "chest_type_matches_contents": True,
        "korl_hints": True, "hint_importance": True, "num_barren_hints": 3, "num_path_hints": 3, "sword_mode": "No Starting Sword",
    }),
}


def run_generator(out: Path, args, hashseed: str = "0", expect: int = 0):
    env = dict(os.environ, PYTHONHASHSEED=hashseed)
    r = subprocess.run([str(PYTHON), str(SCRIPT), "--out", str(out), *args], capture_output=True, text=True, env=env, timeout=300)
    if r.returncode != expect:
        raise AssertionError(f"exit {r.returncode} (wanted {expect}):\n{r.stdout}\n{r.stderr[-2000:]}")
    return json.loads(r.stdout.strip().splitlines()[-1]), r


def spoiler_item_locations(text: str) -> dict:
    """Parse wwrando's 'All item locations:' section into {full location name: item}."""
    section = text.split("All item locations:\n", 1)[1].split("\n\n\n", 1)[0]
    out, zone = {}, None
    for line in section.splitlines():
        if not line.startswith("    "):
            zone = line[:-1]
            continue
        m = re.match(r"^    (.+?):\s+(\S.*)$", line)
        specific, item = m.group(1), m.group(2)
        out[zone if specific == zone else f"{zone} - {specific}"] = item
    return out


@unittest.skipUnless(HAVE_RUNTIME, "rando runtime not fetched (python scripts/windows/rando_runtime.py)")
class SeedGenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        base = Path(cls.tmp.name)
        jobs = []
        for name, (seed, options) in CASES.items():
            opt_file = base / f"{name}.options.json"
            opt_file.write_text(json.dumps(options))
            for run, hashseed in (("a", "1"), ("b", "2")):
                jobs.append((name, run, ["--seed", seed, "--options", str(opt_file)], hashseed))
        cls.results = {}

        def go(job):
            name, run, args, hashseed = job
            out = base / name / run
            result, _ = run_generator(out, args, hashseed)
            return (name, run), (out, result)

        with ThreadPoolExecutor(max_workers=4) as pool:
            for key, value in pool.map(go, jobs):
                cls.results[key] = value
        cls.base = base

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def files(self, name, run):
        out, result = self.results[(name, run)]
        return (Path(result["seed_json"]), Path(result["spoiler_log"]), Path(result["non_spoiler_log"]))

    def pack(self, name):
        return json.loads(self.files(name, "a")[0].read_text(encoding="utf-8"))

    def test_outputs_exist_and_are_named_as_wwrando_names_them(self):
        for name, (seed, _) in CASES.items():
            seed_json, spoiler, nonspoiler = self.files(name, "a")
            self.assertEqual(spoiler.name, f"WW Random {seed} - Spoiler Log.txt")
            self.assertEqual(nonspoiler.name, f"WW Random {seed} - Non-Spoiler Log.txt")
            for p in (seed_json, spoiler, nonspoiler):
                self.assertGreater(p.stat().st_size, 1000, p)

    def test_deterministic_across_processes(self):
        for name in CASES:
            for a, b in zip(self.files(name, "a"), self.files(name, "b")):
                self.assertEqual(a.read_bytes(), b.read_bytes(), f"{name}: {a.name} differs between runs")

    def test_different_seeds_differ(self):
        a = self.pack("default-1")["locations"]
        b = self.pack("default-2")["locations"]
        self.assertNotEqual([x["item"] for x in a], [x["item"] for x in b])

    def test_locations_in_item_locations_file_order(self):
        text = (RUNTIME / "wwrando" / "logic" / "item_locations.txt").read_text(encoding="utf-8")
        names = [m.group(1) for m in re.finditer(r"^([^#\s][^\n]*):\s*$", text, re.M)]
        self.assertEqual(len(names), 320)
        for name in CASES:
            locs = self.pack(name)["locations"]
            self.assertEqual([l["name"] for l in locs], names)
            self.assertEqual([l["id"] for l in locs], list(range(320)))
            for l in locs:
                if l["trap_chest"]:
                    self.assertIsNone(l["item_id"])
                else:
                    self.assertIsInstance(l["item_id"], int, l)
                    self.assertTrue(0 <= l["item_id"] <= 0xFF)

    def test_spoiler_all_item_locations_match_seed_json(self):
        for name in CASES:
            seed_json, spoiler, _ = self.files(name, "a")
            pack = json.loads(seed_json.read_text(encoding="utf-8"))
            from_log = spoiler_item_locations(spoiler.read_text(encoding="utf-8"))
            from_pack = {l["name"]: l["item"] for l in pack["locations"]}
            self.assertEqual(from_log, from_pack, name)

    def test_header_matches_log(self):
        for name, (seed, _) in CASES.items():
            seed_json, spoiler, _ = self.files(name, "a")
            pack = json.loads(seed_json.read_text(encoding="utf-8"))
            head = spoiler.read_text(encoding="utf-8").split("Options selected:", 1)[0]
            self.assertIn(f"Permalink: {pack['permalink']}", head)
            self.assertIn(f"Seed Hash: {pack['seed_hash']}", head)
            self.assertEqual(pack["seed"], seed)
            self.assertEqual(pack["schema"], "bod.seedpack")

    def test_pack_sections(self):
        default = self.pack("default-1")
        self.assertEqual(len(default["charts"]["islands"]), 49)
        self.assertFalse(default["entrances"]["randomized"])
        self.assertEqual(default["starting"]["island"]["room"], 44)
        self.assertEqual(len(default["hints"]["fishmen"]), 49)
        self.assertEqual(len(default["hints"]["hoho"]), 10)
        self.assertTrue(default["playthrough"])

        kl = self.pack("keylunacy-swordless")
        self.assertEqual(kl["options"]["keylunacy"], True)
        self.assertEqual(kl["starting"]["game_variables"]["sword_mode"], 2)
        self.assertEqual(len(kl["starting"]["extra_random_items"]), 2)
        self.assertEqual(kl["starting"]["game_variables"]["num_triforce_shards_to_start_with"], 2)
        self.assertEqual(kl["starting"]["game_variables"]["damage_multiplier"], 4.0)

        ent = self.pack("entrances-all")
        self.assertTrue(ent["entrances"]["randomized"])
        self.assertEqual(len(ent["entrances"]["connections"]), 44)
        self.assertTrue(ent["required_bosses"]["enabled"])
        self.assertEqual(sum(b["required"] for b in ent["required_bosses"]["bosses"]), 3)
        self.assertEqual(sum(l["trap_chest"] for l in ent["locations"]), 5)
        self.assertTrue(ent["starting"]["island"]["randomized"])
        self.assertIsNotNone(ent["hints"]["korl"])
        self.assertEqual(sorted(c["island"] for c in ent["charts"]["islands"]), list(range(1, 50)))
        self.assertEqual(sorted(c["chart"] for c in ent["charts"]["islands"]),
                         sorted(c["chart"] for c in default["charts"]["islands"]))

    def test_permalink_reproduces_seed_pack(self):
        pack = self.pack("keylunacy-swordless")
        out = self.base / "permalink"
        run_generator(out, ["--permalink", pack["permalink"]])
        again = json.loads((out / "seed.json").read_text(encoding="utf-8"))
        # Same items everywhere; options outside the permalink (cosmetics) may differ, so compare placements only.
        self.assertEqual(again["locations"], pack["locations"])
        self.assertEqual(again["permalink"], pack["permalink"])

    def test_bad_options_exit_2(self):
        result, _ = run_generator(self.base / "bad1", ["--seed", "x", "--set", "no_such_option=1"], expect=2)
        self.assertFalse(result["ok"])
        result, _ = run_generator(self.base / "bad2", ["--seed", "x", "--set", "sword_mode=Nope"], expect=2)
        self.assertFalse(result["ok"])

    def test_generation_failure_exits_3_with_message(self):
        off = [f"--set=progression_{n}=false" for n in ("dungeons", "great_fairies", "puzzle_secret_caves", "free_gifts",
                                                        "expensive_purchases", "misc")]
        out = self.base / "fail"
        result, r = run_generator(out, ["--seed", "x", *off], expect=3)
        self.assertFalse(result["ok"])
        self.assertIn("Not enough progress locations", result["error"])
        self.assertFalse((out / "seed.json").exists())
        self.assertTrue(list(out.glob("*Error Log.txt")))


if __name__ == "__main__":
    unittest.main()
