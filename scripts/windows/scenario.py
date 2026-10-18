#!/usr/bin/env python3
"""Run test scenarios: start the game from a debug save state (or boot), set
up a situation, press buttons, take frames, and check the log.

    python scripts/windows/scenario.py telescope pause --size 2560x1080 --size 1440x1080
    python scripts/windows/scenario.py --list

A scenario is a JSON file in scripts/windows/scenarios/ (see README.md there).
It starts either from "boot" (the opening played by script to player control
on Outset) or from a named state. States are made by the scenario that saves
them (a "save_state" step) and kept in OUT/states/ (git-ignored: a state holds
game memory). A state remembers the game module it was made with; when the
module changes, the state is made again before it is used.

Step times ("at") count retraces from the start: the retrace a state was saved
at, or for "boot" retrace 8600, just after player control (the legend skipped).

Output: OUT/scenarios/<name>/<WxH>/ with run.log, a PNG per frame, sheet.png
and result.json. The exit code is non-zero when an expectation fails.

Testing only. Nothing here goes into a player's build.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
import sys
from pathlib import Path
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

ROOT = Path(__file__).resolve().parent.parent.parent
SCENARIOS = Path(__file__).resolve().parent / "scenarios"
DEFAULT_ISO = local_path("BOD_DISC")
# The scripted opening skips the new-game legend (Better Wind Waker's Skip Opening Movie, on in boot runs
# unless the scenario sets it): Outset's wake-up talk comes by retrace 2000; A is pressed through it.
BOOT_PULSES = range(2500, 7601, 150)
BOOT_START = 8600  # after player control on Outset in the scripted opening (control by about 8000)

# --- the game's memory (GZLE01; tww decomp names) ---------------------------------
GAME_INFO = 0x803C4C08              # g_dComIfG_gameInfo
SAVE_STATUS_A = GAME_INFO + 0x0     # dSv_player_status_a_c
SAVE_ITEMS = GAME_INFO + 0x3C       # dSv_player_item_c::mItems[21], by dInvSlot
PLAY = GAME_INFO + 0x12A0           # dComIfG_play_c
FIELDS = {
    # name: (address, size)
    "max_life": (SAVE_STATUS_A + 0x0, 2),   # quarter hearts
    "life": (SAVE_STATUS_A + 0x2, 2),
    "rupees": (SAVE_STATUS_A + 0x4, 2),
    "magic": (SAVE_STATUS_A + 0x14, 1),
    "play_life": (PLAY + 0x4924, 2),        # mItemNowLife
}
# dInvSlot: inventory slot of each item, and its item number (dItemNo).
ITEMS = {
    "telescope": (0, 0x20), "sail": (1, 0x78), "wind_waker": (2, 0x22), "grappling_hook": (3, 0x25),
    "spoils_bag": (4, 0x24), "boomerang": (5, 0x2D), "deku_leaf": (6, 0x34), "tingle_tuner": (7, 0x21),
    "picto_box": (8, 0x23), "deluxe_picto_box": (8, 0x26), "iron_boots": (9, 0x29), "magic_armor": (10, 0x2A),
    "bait_bag": (11, 0x2C), "bow": (12, 0x27), "bombs": (13, 0x31), "bottle": (14, 0x50),
    "delivery_bag": (18, 0x30), "hookshot": (19, 0x2F), "skull_hammer": (20, 0x33),
}
BUTTON_SLOTS = {"X": 0, "Y": 1, "Z": 2}
SAVE_SELECT = SAVE_STATUS_A + 0x09      # slot numbers, X Y Z
PLAY_SELECT = PLAY + 0x4933             # item numbers, X Y Z
PAD = {"left": 0x0001, "right": 0x0002, "down": 0x0004, "up": 0x0008, "Z": 0x0010, "R": 0x0020,
       "L": 0x0040, "A": 0x0100, "B": 0x0200, "X": 0x0400, "Y": 0x0800, "start": 0x1000}


def die(message: str):
    print(f"scenario: {message}", file=sys.stderr)
    sys.exit(2)


def parse_script(text: str, where: str) -> dict:
    """The line format (MergeShip2's test scripts, for this game): one command
    a line, '#' comments. A cursor counts retraces from the start; 'wait'
    moves it, and so do press/hold/stick by their length. See README.md."""
    s: dict = {"steps": [], "settings": {}, "env": {}, "expect": {"log": [], "not_log": []}}
    t = 0
    explicit_end = None
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        word, *rest = line.split()
        arg = line[len(word):].strip()

        def need(n):
            if len(rest) < n:
                die(f"{where}:{number}: '{word}' needs {n} argument(s)")

        if word == "description":
            s["description"] = arg
        elif word == "start":
            need(1)
            if rest[0] == "boot":
                s["start"] = {"raw": "boot_raw", "timeline": "boot_timeline"}.get(rest[-1], "boot")
            else:
                s["start"] = {"state": rest[-1]}
        elif word == "size":
            need(1)
            s["size"] = rest[0]
        elif word == "card":
            need(1)
            s["card"] = rest[0]
        elif word == "cvar":
            need(2)
            value = rest[1]
            if value.startswith("rgb:") and len(value) == 10:
                rgb = int(value[4:], 16)
                s["settings"][rest[0]] = {"R": rgb >> 16, "G": (rgb >> 8) & 255, "B": rgb & 255, "A": 255}
            else:
                try:
                    s["settings"][rest[0]] = float(value) if "." in value else int(value, 0)
                except ValueError:
                    s["settings"][rest[0]] = " ".join(rest[1:])  # a string
        elif word == "env":
            need(2)
            s["env"][rest[0]] = " ".join(rest[1:])
        elif word == "audio":
            s["audio"] = True  # the game's audio output to <run folder>/audio.wav
        elif word == "wait":
            need(1)
            t += int(rest[0])
        elif word in ("press", "hold"):
            need(1)
            frames = int(rest[1]) if len(rest) > 1 else 2
            s["steps"].append({"at": t, "press": rest[0], "frames": frames})
            t += frames
        elif word == "stick":
            need(3)
            step = {"at": t, "stick": [int(rest[0]), int(rest[1])], "frames": int(rest[2])}
            if len(rest) > 3:
                step["hold"] = rest[3]
            s["steps"].append(step)
            t += int(rest[2])
        elif word == "cstick":
            need(3)
            s["steps"].append({"at": t, "cstick": [int(rest[0]), int(rest[1])], "frames": int(rest[2])})
            t += int(rest[2])
        elif word == "give":
            need(1)
            s["steps"].append({"at": t, "give": rest})
        elif word == "equip":
            need(2)
            s["steps"].append({"at": t, "equip": {rest[0]: rest[1]}})
        elif word == "health":
            need(1)
            s["steps"].append({"at": t, "set": {"life": int(rest[0]), "play_life": int(rest[0])}})
        elif word == "set":
            need(2)
            s["steps"].append({"at": t, "set": {rest[0]: int(rest[1], 0)}})
        elif word == "warp":
            need(1)
            s["steps"].append({"at": t, "warp": rest[0]})
        elif word == "resize":
            need(1)
            s["steps"].append({"at": t, "resize": rest[0]})
        elif word == "poke":
            need(1)
            s["steps"].append({"at": t, "poke": rest})
        elif word == "teleport":
            # teleport <stage> <room> <x> <y> <z> [angle]: Link to that place (the area reloads), facing angle (s16)
            need(5)
            s["steps"].append({"at": t, "teleport": ":".join(rest[:6] if len(rest) >= 6 else rest[:5] + ["0"])})
        elif word == "peek":
            need(1)
            s["steps"].append({"at": t, "peek": rest})
        elif word == "shot":
            s["steps"].append({"at": t, "capture": True})
        elif word == "save_state":
            need(1)
            s["steps"].append({"at": t, "save_state": rest[0]})
        elif word == "expect":
            s["expect"]["log"].append(arg)
        elif word == "expect_not":
            s["expect"]["not_log"].append(arg)
        elif word in ("end", "quit"):
            explicit_end = int(rest[0]) if rest else t + 10
        else:
            die(f"{where}:{number}: unknown command '{word}'")
    s["end"] = explicit_end if explicit_end is not None else t + 10
    return s


def load_scenario(name: str) -> dict:
    path = Path(name)
    if not path.suffix:
        for suffix in (".txt", ".json"):
            if (SCENARIOS / f"{name}{suffix}").is_file():
                path = SCENARIOS / f"{name}{suffix}"
                break
    if not path.is_file():
        die(f"no scenario {name} ({path})")
    text = path.read_text(encoding="utf-8")
    data = json.loads(text) if path.suffix == ".json" else parse_script(text, str(path))
    data.setdefault("name", path.stem)
    return data


def all_scenarios() -> list[dict]:
    return [load_scenario(str(p)) for p in sorted(list(SCENARIOS.glob("*.txt")) + list(SCENARIOS.glob("*.json")))]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def buttons(spec) -> int:
    if isinstance(spec, int):
        return spec
    mask = 0
    for b in str(spec).replace("+", " ").split():
        if b not in PAD:
            die(f"unknown button {b} (known: {', '.join(PAD)})")
        mask |= PAD[b]
    return mask


class Run:
    """One scenario at one window size."""

    def __init__(self, args, scenario: dict, size: str):
        self.args, self.s, self.size = args, scenario, size
        self.out = args.out / "scenarios" / scenario["name"] / (f"{size}-{args.tag}" if args.tag else size)
        self.pads: list[str] = []
        self.pokes: list[str] = []
        self.peeks: list[str] = []
        self.warps: list[str] = []
        self.teleports: list[str] = []
        self.captures: list[int] = []
        self.resizes: list[str] = []
        self.saves: list[tuple[str, int]] = []

    # Steps -> the host's test variables (absolute retraces).
    def plan(self, start: int):
        for step in self.s.get("steps", []):
            at = start + int(step.get("at", 0))
            frames = int(step.get("frames", 2))
            if "press" in step:
                self.pads.append(f"{at}:{buttons(step['press'])}:{frames}")
            if "stick" in step:
                x, y = step["stick"]
                c = step.get("cstick")
                entry = f"{at}:{buttons(step.get('hold', 0))}:{frames}:{int(x)}:{int(y)}"
                if c is not None:
                    entry += f":{int(c[0])}:{int(c[1])}"
                self.pads.append(entry)
            elif "cstick" in step:
                c = step["cstick"]
                self.pads.append(f"{at}:{buttons(step.get('hold', 0))}:{frames}:0:0:{int(c[0])}:{int(c[1])}")
            if "warp" in step:
                self.warps.append(f"{at}:{step['warp']}")
            if "resize" in step:
                self.resizes.append(f"{at}:{step['resize']}")
            if "teleport" in step:
                self.teleports.append(f"{at}:{step['teleport']}")
            if "give" in step:
                for item in ([step["give"]] if isinstance(step["give"], str) else step["give"]):
                    slot, number = ITEMS[item]
                    self.pokes.append(f"{at}:{SAVE_ITEMS + slot:#x}={number:#x}")
            if "equip" in step:
                for button, item in step["equip"].items():
                    slot, number = ITEMS[item]
                    i = BUTTON_SLOTS[button]
                    self.pokes.append(f"{at}:{SAVE_SELECT + i:#x}={slot:#x}")
                    self.pokes.append(f"{at}:{PLAY_SELECT + i:#x}={number:#x}")
            if "set" in step:
                hold = "+" if step.get("hold_value") else ""
                for field, value in step["set"].items():
                    address, size = FIELDS[field]
                    self.pokes.append(f"{at}{hold}:{address:#x}={int(value):#x}/{size}")
            if "poke" in step:
                for p in step["poke"]:
                    self.pokes.append(f"{at}:{p}")
            if "peek" in step:
                for p in ([step["peek"]] if isinstance(step["peek"], str) else step["peek"]):
                    if p in FIELDS:
                        address, size = FIELDS[p]
                        p = f"{address:#x}/{size}"
                    self.peeks.append(f"{at}:{p}")
            if step.get("capture"):
                self.captures.append(at)
            if "save_state" in step:
                self.saves.append((step["save_state"], at))
        self.end = start + int(self.s.get("end", 300))

    def env(self, start_state: Path | None) -> dict:
        a, b = self.args, self.args.out
        data = self.out / "data"
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BLUEWAKE_", "DOL_", "LLVM_PROFILE_"))}
        env.update({
            "BLUEWAKE_DATA_DIR": str(data), "BLUEWAKE_NO_DIALOG": "1", "BLUEWAKE_SESSION_LOG": "0",
            "BLUEWAKE_DOL": str((a.game or b / "game") / "main.dol"), "BLUEWAKE_RELS_DIR": str((a.game or b / "game") / "rels"),
            "BLUEWAKE_DISC": str(a.iso),
            "BLUEWAKE_DSP_IROM": str(ROOT / "runtime/recompcore/Data/Sys/GC/dsp_rom.bin"),
            "BLUEWAKE_DSP_COEF": str(ROOT / "runtime/recompcore/Data/Sys/GC/dsp_coef.bin"),
            "BLUEWAKE_MAX_RETRACES": str(self.end), "BLUEWAKE_WALL_PACE": "0",
            "BLUEWAKE_LIVE_PAD": "0", "BLUEWAKE_RENDERER": "aurora",
            # Never disturb the desktop (someone may be working while runs go): the
            # window on the spare monitor, never focused, and no sound to the
            # speakers (SDL's dummy driver keeps real-time audio pacing; the
            # WAV capture still records the game's output). BLUEWAKE_TEST_WINDOW_POS
            # and BLUEWAKE_TEST_AUDIO_DRIVER (e.g. "disk", with
            # SDL_AUDIO_DISK_OUTPUT_FILE) override them.
            "BLUEWAKE_WINDOW_POS": os.environ.get("BLUEWAKE_TEST_WINDOW_POS", "4060,40"),
            "BLUEWAKE_WINDOW_NOACTIVATE": "1",
            "SDL_AUDIO_DRIVER": os.environ.get("BLUEWAKE_TEST_AUDIO_DRIVER", "dummy"),
            "BLUEWAKE_STATE_DIR": str(self.out),
            "DOL_AURORA_WINDOW": self.size, "DOL_AURORA_RENDER_SCALE": "0",
        })
        pads = list(self.pads)
        if start_state is None and self.s.get("start") == "boot_raw":
            pass  # no scripted opening: the scenario's own presses from retrace 0
        elif start_state is None:
            # The opening by script, as scripts/windows/build.py's training
            # route: confirm the title and file screens, A through the opening.
            # (The randomizer's file select would stop at its quest select: not here.)
            env["BLUEWAKE_NO_FILE_SELECT_UI"] = "1"
            env.update({
                "BLUEWAKE_PLAYER_PROBE": "1", "BLUEWAKE_PAD_BUTTONS": "0x0100",
                "BLUEWAKE_OVERLAP_OBSERVATION": "1",
                "BLUEWAKE_PAD_PULSE_ON_TITLE_READY": "1", "BLUEWAKE_PAD_PULSE_LENGTH": "2",
                "BLUEWAKE_PAD_CONFIRM_EVENT": "any",
            })
            pads = [f"{n}:0x0100:2" for n in BOOT_PULSES] + pads
        else:
            env["BLUEWAKE_LOAD_STATE"] = str(start_state)
            env["BLUEWAKE_LOAD_STATE_FORCE"] = "1"
        if pads:
            env["BLUEWAKE_PAD_SCRIPT"] = ",".join(sorted(pads, key=lambda e: int(e.split(":")[0])))
        if self.pokes:
            env["BLUEWAKE_TEST_POKE"] = ",".join(self.pokes)
        if self.peeks:
            env["BLUEWAKE_TEST_PEEK"] = ",".join(self.peeks)
        if self.warps:
            env["BLUEWAKE_TEST_WARP"] = ",".join(self.warps)
        if self.resizes:
            env["BLUEWAKE_TEST_RESIZE"] = ",".join(self.resizes)
        if self.teleports:
            env["BLUEWAKE_TEST_TELEPORT"] = ",".join(self.teleports)
        if self.captures:
            env["BLUEWAKE_CAPTURE_OPENING_FRAME"] = str(self.out / "frame.ppm")
            env["BLUEWAKE_CAPTURE_RETRACES"] = ",".join(str(r) for r in sorted(set(self.captures)))
        if self.saves:
            env["BLUEWAKE_SAVE_STATE"] = ",".join(f"{self.out / (n + '.bwstate')}@{r}" for n, r in self.saves)
        if self.s.get("audio"):
            env["BLUEWAKE_CAPTURE_AUDIO_WAV"] = str(self.out / "audio.wav")
            # What the sink is given (s16 after the stretcher), what the device
            # gets (float32 at the device rate, after SDL's conversion), and the
            # producer's push timing (see docs/fork/audio-static.md).
            env["DOL_AUDIO_SINK_CAPTURE"] = str(self.out / "sink.s16")
            env["DOL_AUDIO_DEVICE_CAPTURE"] = str(self.out / "device.f32")
            env["DOL_AUDIO_PUSH_TRACE"] = str(self.out / "push.trace")
        env.update({k: str(v).replace("{root}", str(ROOT)) for k, v in self.s.get("env", {}).items()})
        # A scenario's own poke list (env BLUEWAKE_TEST_POKE, e.g. noon held) joins the steps' pokes (give, set,
        # poke) instead of replacing them: replaced, a held noon silently dropped a give's inventory pokes.
        if self.pokes and "BLUEWAKE_TEST_POKE" in self.s.get("env", {}):
            env["BLUEWAKE_TEST_POKE"] = ",".join([self.s["env"]["BLUEWAKE_TEST_POKE"]] + self.pokes)
        for item in getattr(self.args, "env", []) or []:
            k, v = item.split("=", 1)
            env[k] = v
        return env

    def write_settings(self):
        data = self.out / "data"
        data.mkdir(parents=True, exist_ok=True)
        root: dict = {"CVars": {}}
        settings = dict(self.s.get("settings", {}))
        if self.s.get("start", "boot") in ("boot", "boot_timeline"):
            # The scripted opening skips the new-game legend (BOOT_PULSES/BOOT_START assume it).
            settings.setdefault("gEnhancements.BetterWW.skip_intro_movie", 1)
        for name, value in settings.items():
            node = root["CVars"]
            parts = name.split(".")
            for part in parts[:-1]:
                node = node.setdefault(part, {})
            node[parts[-1]] = value
        (data / "boat-of-daphnes.json").write_text(json.dumps(root, indent=4) + "\n")
        # A memory card to play with (OUT/states/<card>): copied, so the game's
        # own saves never touch the original.
        if "card" in self.s:
            (data / "Save").mkdir(exist_ok=True)
            shutil.copy2(self.args.states / self.s["card"], data / "Save" / "GZLE01.card")

    def run(self, start_state: Path | None, start: int) -> dict:
        shutil.rmtree(self.out, ignore_errors=True)
        self.out.mkdir(parents=True)
        self.plan(start)
        self.write_settings()
        env = self.env(start_state)
        module = self.args.module
        log = self.out / "run.log"
        print(f"{self.s['name']} {self.size}: retraces {start}..{self.end}"
              + (f", from state {start_state.name}" if start_state else ", from boot"))
        with open(log, "wb") as stream:
            try:
                code = subprocess.call([str(self.args.exe), "--module", str(module)], env=env, stdout=stream,
                                       stderr=subprocess.STDOUT, timeout=self.args.timeout)
            except subprocess.TimeoutExpired:
                code = "timeout"
        frames = self.convert_frames()
        text = log.read_text(encoding="utf-8", errors="replace")
        result = {"scenario": self.s["name"], "size": self.size, "exit": code, "frames": frames,
                  "peeks": re.findall(r"\[test\] peek .*", text), "expect": [], "passed": True}
        for pattern in self.s.get("expect", {}).get("log", []):
            found = re.search(pattern, text) is not None
            result["expect"].append({"log": pattern, "found": found})
            result["passed"] &= found
        for pattern in self.s.get("expect", {}).get("not_log", []):
            found = re.search(pattern, text) is not None
            result["expect"].append({"not_log": pattern, "found": found})
            result["passed"] &= not found
        if code not in (0, "0"):
            result["passed"] = False
        for name, _ in self.saves:
            made = self.out / f"{name}.bwstate"
            result.setdefault("states", []).append({"name": name, "made": made.is_file()})
            if made.is_file():
                self.args.states.mkdir(parents=True, exist_ok=True)
                # The state is taken at the first clean point at or after the
                # retrace asked for: its own retrace is in the log.
                m = re.search(r"\[state\] saved .*" + re.escape(f"{name}.bwstate") + r" retrace=(\d+)", text)
                retrace = int(m.group(1)) if m else int(dict(self.saves)[name])
                shutil.copy2(made, self.args.states / f"{name}.bwstate")
                (self.args.states / f"{name}.json").write_text(json.dumps(
                    {"retrace": retrace, "module_sha256": self.args.module_sha, "scenario": self.s["name"],
                     "size": self.size}, indent=2) + "\n")
            else:
                result["passed"] = False
        (self.out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        status = "ok" if result["passed"] else "FAILED"
        print(f"  {status}: exit {code}, {len(frames)} frames, {self.out}")
        for p in result["peeks"]:
            print("  " + p)
        for e in result["expect"]:
            if ("log" in e) != e["found"]:
                print(f"  expectation not met: {e}")
        return result

    def convert_frames(self) -> list[str]:
        ppms = sorted(self.out.glob("frame*.ppm"), key=lambda q: int(re.sub(r"\D", "", q.stem) or 0))
        if not ppms:
            return []
        try:
            from PIL import Image
        except ImportError:
            return [p.name for p in ppms]
        names, thumbs = [], []
        for p in ppms:
            image = Image.open(p)
            png = p.with_suffix(".png")
            image.save(png)
            p.unlink()
            names.append(png.name)
            w, h = image.size
            thumbs.append(image.resize((640, max(1, round(640 * h / w)))))
        cols = min(4, len(thumbs))
        rows = (len(thumbs) + cols - 1) // cols
        th = max(t.size[1] for t in thumbs)
        sheet = Image.new("RGB", (cols * 640, rows * th))
        for i, t in enumerate(thumbs):
            sheet.paste(t, ((i % cols) * 640, (i // cols) * th))
        sheet.save(self.out / "sheet.png")
        return names


def state_ready(args, name: str) -> tuple[Path, int] | None:
    state, meta = args.states / f"{name}.bwstate", args.states / f"{name}.json"
    if not (state.is_file() and meta.is_file()):
        return None
    info = json.loads(meta.read_text())
    # An external state (someone's own, from another build) is never made
    # here: it loads with BLUEWAKE_LOAD_STATE_FORCE, or the run says it failed.
    if not info.get("external") and info.get("module_sha256") != args.module_sha:
        print(f"state {name} was made with another game module; making it again")
        return None
    return state, int(info["retrace"])


def producer(name: str) -> dict:
    for s in all_scenarios():
        if any(step.get("save_state") == name for step in s.get("steps", [])):
            return s
    die(f"no scenario saves the state {name}")


def resolve_start(args, scenario: dict, depth=0) -> tuple[Path | None, int]:
    start = scenario.get("start", "boot")
    if start == "boot":
        return None, BOOT_START
    if start in ("boot_raw", "boot_timeline"):
        return None, 0
    name = start["state"] if isinstance(start, dict) else start
    ready = state_ready(args, name)
    if ready is None:
        if depth > 4:
            die(f"state chain too deep at {name}")
        maker = producer(name)
        path, base = resolve_start(args, maker, depth + 1)
        Run(args, maker, maker.get("size", "1280x960")).run(path, base)
        ready = state_ready(args, name)
        if ready is None:
            die(f"the scenario {maker['name']} did not make the state {name}")
    return ready


class Lock:
    """One runner at a time: two games at once share the GPU and the frame
    timing, and their results cannot be compared (MergeShip2 and Lego Battles
    both lost measurements to this)."""

    # One lock for the whole machine, not one per out folder: runners of other
    # checkouts and worktrees (each with its own --out) ran their games side by
    # side and every frame-rate number of those runs was worthless.
    # BLUEWAKE_SCENARIO_LOCK names another file (a test of the lock itself).
    GLOBAL = Path(os.environ.get("BLUEWAKE_SCENARIO_LOCK")
                  or Path(os.environ.get("LOCALAPPDATA") or Path.home()) / "bluewake-scenario.lock")

    def __init__(self, folder: Path | None = None):
        self.path = self.GLOBAL

    @staticmethod
    def _alive(pid: int) -> bool:
        return subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True,
                              text=True).stdout.find(str(pid)) >= 0

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        # Another runner's turn: wait for it (one game at a time; see above).
        # The file is made with O_EXCL, so two runners cannot both take it.
        waited = False
        while True:
            try:
                fd = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                os.write(fd, str(os.getpid()).encode())
                os.close(fd)
                break
            except FileExistsError:
                pass
            try:
                pid = int(self.path.read_text().strip())
                alive = self._alive(pid)
            except (ValueError, OSError):
                pid, alive = 0, False
            if not alive:
                self.path.unlink(missing_ok=True) # a runner that died holding it
                continue
            if not waited:
                print(f"waiting for another scenario run (pid {pid}, lock {self.path})", flush=True)
                waited = True
            time.sleep(2)
        # Keep the display awake: a sleeping display throttles presentation.
        if os.name == "nt":
            import ctypes
            ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | 0x00000002 | 0x00000001)
        return self

    def __exit__(self, *exc):
        self.path.unlink(missing_ok=True)
        if os.name == "nt":
            import ctypes
            ctypes.windll.kernel32.SetThreadExecutionState(0x80000000)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("scenarios", nargs="*", help="scenario names (files in scripts/windows/scenarios)")
    p.add_argument("--list", action="store_true", help="list the scenarios")
    p.add_argument("--size", action="append", help="window size WxH (repeatable; default: the scenario's)")
    p.add_argument("--out", type=Path, default=ROOT / "build/windows", help="the build's out folder")
    p.add_argument("--exe", type=Path, help="default: OUT/boat-of-daphnes/boat-of-daphnes.exe")
    p.add_argument("--module", type=Path, help="default: OUT/boat-of-daphnes/gGZLE01_recomp.dll")
    p.add_argument("--iso", type=Path, default=Path(DEFAULT_ISO))
    p.add_argument("--game", type=Path, help="the prepared game folder (main.dol, rels/): default OUT/game")
    p.add_argument("--remake", action="store_true", help="make the starting states again")
    p.add_argument("--timeout", type=int, default=1800)
    p.add_argument("--cvar", action="append", default=[], metavar="NAME=VALUE",
                   help="a setting on top of the scenario's (repeatable; a number, or text)")
    p.add_argument("--env", action="append", default=[], metavar="NAME=VALUE", help="an environment variable for every run (after the scenario's own)")
    p.add_argument("--tag", default="", help="added to the output folder's name (size-TAG), for runs with other settings")
    args = p.parse_args()
    if args.list:
        for s in all_scenarios():
            print(f"{s['name']:24} {s.get('description', '')}")
        return 0
    if not args.scenarios:
        p.error("name a scenario (or --list)")
    args.out = args.out.resolve()
    args.exe = (args.exe or args.out / "boat-of-daphnes/boat-of-daphnes.exe").resolve()
    args.module = (args.module or args.out / "boat-of-daphnes/gGZLE01_recomp.dll").resolve()
    for f in (args.exe, args.module, args.iso):
        if not f.is_file():
            die(f"missing {f}")
    args.states = args.out / "states"
    args.module_sha = sha256(args.module)
    with Lock(args.out / "scenarios"):
        return run_all(args)


def run_all(args) -> int:
    failed = 0
    for name in args.scenarios:
        scenario = load_scenario(name)
        for item in args.cvar:
            key, _, value = item.partition("=")
            try:
                scenario.setdefault("settings", {})[key] = float(value) if "." in value else int(value, 0)
            except ValueError:
                scenario.setdefault("settings", {})[key] = value
        if args.remake and isinstance(scenario.get("start"), (str, dict)) and scenario.get("start") != "boot":
            state = scenario["start"]["state"] if isinstance(scenario["start"], dict) else scenario["start"]
            (args.states / f"{state}.json").unlink(missing_ok=True)
        path, start = resolve_start(args, scenario)
        for size in args.size or [scenario.get("size", "1280x960")]:
            if not Run(args, scenario, size).run(path, start)["passed"]:
                failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
