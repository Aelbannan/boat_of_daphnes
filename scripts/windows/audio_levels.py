#!/usr/bin/env python3
"""Levels of the audio a scenario run captured, to compare runs made with other volume settings.

    python scripts/windows/audio_levels.py RUN_FOLDER [RUN_FOLDER ...] [--window START END] [--window ...]

Reads sink.s16 (s16 stereo as the game's audio gave it to the output; scenario.py's `audio` step) in each run folder and prints the
RMS level in dBFS of each window (seconds of audio from the start; default: the whole capture in ten equal parts).
"""
import argparse
import re
import sys
from pathlib import Path

import numpy as np


def load(folder: Path):
    log = (folder / "run.log").read_text(errors="replace") if (folder / "run.log").exists() else ""
    rates = [int(m) for m in re.findall(r"\[audio-sink\] offset_bytes=\d+ sample_rate=(\d+)", log)]
    rate = rates[-1] if rates else 32000
    data = np.fromfile(folder / "sink.s16", dtype="<i2").astype(np.float64) / 32768.0
    return data.reshape(-1, 2), rate


def db(x):
    rms = float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.0
    return -200.0 if rms <= 0 else 20.0 * np.log10(rms)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folders", nargs="+", type=Path)
    ap.add_argument("--window", nargs=2, type=float, action="append", metavar=("START", "END"))
    a = ap.parse_args()
    for folder in a.folders:
        x, rate = load(folder)
        seconds = len(x) / rate
        windows = a.window or [(seconds * i / 10, seconds * (i + 1) / 10) for i in range(10)]
        print(f"{folder}: {seconds:.1f} s at {rate} Hz")
        print("   whole capture: %.2f dBFS" % db(x))
        for start, end in windows:
            seg = x[int(start * rate):int(end * rate)]
            print("   %6.1f-%6.1f s: %7.2f dBFS (peak %.3f)" % (start, end, db(seg), float(np.abs(seg).max()) if len(seg) else 0.0))


if __name__ == "__main__":
    sys.exit(main())
