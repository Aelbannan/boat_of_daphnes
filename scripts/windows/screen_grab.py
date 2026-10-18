#!/usr/bin/env python3
"""Pictures of the menu and the Dev Tools windows from a scenario run (scenario shots hold the game only).

    python scripts/windows/screen_grab.py <scenario run folder> [--monitor X Y W H]

Start it beside `python scripts/windows/scenario.py ...`. It waits for the run's run.log, and three seconds after each
`[test] set` line (the scenario's BLUEWAKE_TEST_SET changes, which can open the menu and its pages and windows) it
grabs the spare monitor the test window sits on into <run folder>/ui-N.png. It stops when result.json appears. It only
reads the screen; the game is started and held by scenario.py.
"""
import argparse
import time
from pathlib import Path

from PIL import ImageGrab


def main():
    p = argparse.ArgumentParser()
    p.add_argument("run", type=Path)
    p.add_argument("--monitor", nargs=4, type=int, default=[4040, 0, 1920, 1080], metavar=("X", "Y", "W", "H"))
    p.add_argument("--match", default="[test] set ", help="log text that triggers a grab")
    p.add_argument("--delay", type=float, default=3.0)
    a = p.parse_args()
    x, y, w, h = a.monitor
    log, result = a.run / "run.log", a.run / "result.json"
    seen, shots = 0, 0
    pending = []
    # An older run's files are still there until the new run clears them: wait for those to go first.
    while result.exists():
        time.sleep(1)
    while not result.exists():
        if log.exists():
            lines = log.read_text(encoding="utf-8", errors="replace").count(a.match)
            while seen < lines:
                seen += 1
                pending.append(time.time() + a.delay)
        while pending and time.time() >= pending[0]:
            pending.pop(0)
            shots += 1
            ImageGrab.grab(bbox=(x, y, x + w, y + h), all_screens=True).save(a.run / f"ui-{shots}.png")
            print(f"ui-{shots}.png")
        time.sleep(0.5)


if __name__ == "__main__":
    main()
