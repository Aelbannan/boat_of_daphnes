#!/usr/bin/env python3
"""Measure what the audio sink and the device were given in a scenario run.

    python scripts/windows/audio_device_check.py <run folder> [--skip SECONDS]

Reads sink.s16 (s16 stereo as pushed to SDL, after the stretcher; written by
DOL_AUDIO_SINK_CAPTURE) and device.f32 (float32 stereo as the device got it,
after SDL's resampler; DOL_AUDIO_DEVICE_CAPTURE), both made by scenario.py's
`audio` command. The rates come from run.log. Reports, per stream:
  - real length against the nominal;
  - zero runs (the device ran dry: silence between pushes) and their total;
  - the largest sample-to-sample jumps relative to the signal level;
  - band levels (dBFS) up to Nyquist, and the energy above 16 kHz against the
    8-12 kHz band (an image or aliasing floor shows there);
  - clipped samples.
Needs numpy.
"""
import argparse
import re
import sys
from pathlib import Path

import numpy as np


def rates(log: str):
    sink = [int(m) for m in re.findall(r"\[audio-sink\] offset_bytes=\d+ sample_rate=(\d+)", log)]
    m = re.search(r"\[audio-device\] capture=.*?device_rate=(\d+)", log)
    return sink, int(m.group(1)) if m else None


def zero_runs(x: np.ndarray, min_frames: int):
    """Runs of exact digital silence on both channels, in frames, after the first sound."""
    nz = np.flatnonzero(np.abs(x).max(axis=1) > 0)
    if nz.size == 0:
        return []
    z = np.abs(x[nz[0]:]).max(axis=1) == 0
    edge = np.diff(np.concatenate(([0], z.astype(np.int8), [0])))
    starts, ends = np.flatnonzero(edge == 1), np.flatnonzero(edge == -1)
    return [(int(s) + int(nz[0]), int(e - s)) for s, e in zip(starts, ends) if e - s >= min_frames]


def bands(m: np.ndarray, rate: int, full_scale: float):
    n = 8192
    blocks = len(m) // n
    if blocks < 2:
        return {}
    w = np.hanning(n)
    spec = np.zeros(n // 2 + 1)
    for b in range(blocks):
        spec += np.abs(np.fft.rfft(m[b * n:(b + 1) * n] * w)) ** 2
    spec /= blocks
    f = np.fft.rfftfreq(n, 1.0 / rate)
    norm = (w ** 2).sum() * full_scale ** 2 / 2  # power of a full-scale sine -> 0 dB
    out = {}
    edges = [0, 500, 2000, 4000, 8000, 12000, 16000, 20000, rate / 2]
    for lo, hi in zip(edges, edges[1:]):
        if lo >= rate / 2 or hi <= lo:
            continue
        sel = (f >= lo) & (f < hi)
        out[f"{int(lo)}-{int(hi)}"] = 10 * np.log10(spec[sel].sum() / norm + 1e-20)
    return out


def report(name: str, x: np.ndarray, rate: int, full_scale: float, skip: float):
    print(f"== {name}: {len(x)} frames at {rate} Hz = {len(x) / rate:.2f} s")
    x = x[int(skip * rate):]
    runs = zero_runs(x, max(8, rate // 2000))
    total = sum(n for _, n in runs)
    print(f"  silence runs >= 0.5 ms: {len(runs)}, total {total / rate * 1000:.1f} ms"
          f" ({100.0 * total / max(1, len(x)):.2f}% of the stream)")
    if runs:
        longest = sorted(runs, key=lambda r: -r[1])[:3]
        print("  longest: " + ", ".join(f"{n / rate * 1000:.1f} ms at {s / rate:.2f} s" for s, n in longest))
    m = x.mean(axis=1)
    d = np.abs(np.diff(m))
    rms = float(np.sqrt((m ** 2).mean()) + 1e-9)
    print(f"  rms {20 * np.log10(rms / full_scale):.1f} dBFS, peak {np.abs(x).max() / full_scale:.3f},"
          f" clipped {int((np.abs(x) >= full_scale * 0.9999).sum())},"
          f" max jump {d.max() / full_scale:.3f} FS ({d.max() / rms:.1f} x rms),"
          f" jumps > 0.25 FS: {int((d > 0.25 * full_scale).sum())}")
    b = bands(m, rate, full_scale)
    print("  bands dBFS: " + "  ".join(f"{k}: {v:.1f}" for k, v in b.items()))
    if "8000-12000" in b and "16000-20000" in b:
        print(f"  16-20 kHz relative to 8-12 kHz: {b['16000-20000'] - b['8000-12000']:.1f} dB")
    return b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--skip", type=float, default=2.0, help="seconds to ignore at the start")
    a = ap.parse_args()
    run = Path(a.run)
    log = (run / "run.log").read_text(errors="replace") if (run / "run.log").exists() else ""
    sink_rates, dev_rate = rates(log)
    sink = run / "sink.s16"
    if sink.exists() and sink_rates:
        x = np.fromfile(sink, "<i2")
        x = x[: len(x) // 2 * 2].reshape(-1, 2).astype(np.float64)
        report("sink (s16 given to SDL)", x, sink_rates[-1], 32768.0, a.skip)
    dev = run / "device.f32"
    if dev.exists() and dev_rate:
        x = np.fromfile(dev, "<f4")
        x = x[: len(x) // 2 * 2].reshape(-1, 2).astype(np.float64)
        report("device (float32 after SDL)", x, dev_rate, 1.0, a.skip)
    summ = re.findall(r"\[audio\] summary .*", log)
    if summ:
        print(summ[-1])


if __name__ == "__main__":
    sys.exit(main())
