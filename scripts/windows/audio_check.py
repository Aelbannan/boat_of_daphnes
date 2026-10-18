#!/usr/bin/env python3
"""Check a WAV captured with BLUEWAKE_CAPTURE_AUDIO_WAV (scenario.py's `audio` command).

    python scripts/windows/audio_check.py <wav> [--from-retrace A --to-retrace B] [--png out.png]

Reports duration, rate, channels, per-second RMS (dBFS) left/right, silent stretches,
clipping and left/right imbalance. Retraces are converted at 60 per second of audio
(the capture is written from the game's audio DMA, so it follows game time, not wall time).
Needs numpy; Pillow only for --png.
"""
import argparse
import sys
import wave

import numpy as np

RETRACE_HZ = 60.0
FLOOR_DB = -120.0


def db(x: float) -> float:
    return FLOOR_DB if x <= 1e-9 else max(FLOOR_DB, 20.0 * np.log10(x))


def load(path: str):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width != 2:
        sys.exit(f"audio_check: expected 16-bit, got {width * 8}-bit")
    data = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    return data.reshape(-1, ch) if ch > 1 else data.reshape(-1, 1), rate


def silent_spans(per: np.ndarray, win: float, thresh_db: float, min_len: float):
    """per: dBFS of the loudest channel per window of `win` seconds."""
    spans, start = [], None
    for i, v in enumerate(per):
        if v < thresh_db:
            start = i if start is None else start
        elif start is not None:
            if (i - start) * win > min_len:
                spans.append((start * win, i * win))
            start = None
    if start is not None and (len(per) - start) * win > min_len:
        spans.append((start * win, len(per) * win))
    return spans


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--from-retrace", type=float)
    ap.add_argument("--to-retrace", type=float)
    ap.add_argument("--start-retrace", type=float, default=0.0,
                    help="retrace the capture's first sample belongs to (default 0: a capture starts with the run)")
    ap.add_argument("--silence-db", type=float, default=-60.0)
    ap.add_argument("--silence-min", type=float, default=0.5)
    ap.add_argument("--png")
    ap.add_argument("--quiet", action="store_true", help="no per-second table")
    a = ap.parse_args()

    x, rate = load(a.wav)
    total = len(x) / rate
    print(f"file: {a.wav}\nrate {rate} Hz, channels {x.shape[1]}, frames {len(x)}, duration {total:.2f} s "
          f"= {total * RETRACE_HZ:.0f} retraces")
    t0 = 0.0
    if a.from_retrace is not None or a.to_retrace is not None:
        lo = ((a.from_retrace or a.start_retrace) - a.start_retrace) / RETRACE_HZ
        hi = ((a.to_retrace if a.to_retrace is not None else total * RETRACE_HZ + a.start_retrace) - a.start_retrace) / RETRACE_HZ
        x = x[int(max(lo, 0) * rate):int(hi * rate)]
        t0 = max(lo, 0)
        print(f"window: {t0:.2f}..{t0 + len(x) / rate:.2f} s of the capture")
    if len(x) == 0:
        sys.exit("audio_check: empty window")
    left = x[:, 0]
    right = x[:, 1] if x.shape[1] > 1 else x[:, 0]

    sec = rate
    n = int(np.ceil(len(x) / sec))
    ldb, rdb = [], []
    for i in range(n):
        seg = slice(i * sec, (i + 1) * sec)
        ldb.append(db(float(np.sqrt(np.mean(left[seg] ** 2)))))
        rdb.append(db(float(np.sqrt(np.mean(right[seg] ** 2)))))
    if not a.quiet:
        print("\n  sec   L dBFS   R dBFS   (retrace)")
        for i in range(n):
            print(f"{t0 + i:5.0f}  {ldb[i]:7.1f}  {rdb[i]:7.1f}   ({(t0 + i) * RETRACE_HZ + a.start_retrace:.0f})")
    print(f"\noverall RMS: L {db(float(np.sqrt(np.mean(left ** 2)))):.1f}  R {db(float(np.sqrt(np.mean(right ** 2)))):.1f} dBFS; "
          f"peak: L {db(float(np.max(np.abs(left)))):.1f}  R {db(float(np.max(np.abs(right)))):.1f} dBFS")

    # silence at 100 ms resolution
    win = 0.1
    k = int(rate * win)
    m = len(x) // k
    if m:
        blk = np.maximum(
            np.sqrt(np.mean(left[:m * k].reshape(m, k) ** 2, axis=1)),
            np.sqrt(np.mean(right[:m * k].reshape(m, k) ** 2, axis=1)))
        per = np.array([db(float(v)) for v in blk])
        spans = silent_spans(per, win, a.silence_db, a.silence_min)
        silent_total = float(np.sum(per < a.silence_db)) * win
        print(f"\nsilent (< {a.silence_db:.0f} dBFS, > {a.silence_min} s) stretches: {len(spans)}; "
              f"total below threshold {silent_total:.1f} s of {m * win:.1f} s")
        for s, e in spans:
            print(f"  {t0 + s:7.2f} - {t0 + e:7.2f} s  ({e - s:.1f} s)  retraces {(t0 + s) * RETRACE_HZ + a.start_retrace:.0f}.."
                  f"{(t0 + e) * RETRACE_HZ + a.start_retrace:.0f}")

    clipped = int(np.sum(np.abs(x) >= 32767.0 / 32768.0))
    print(f"\nclipping: {clipped} samples at full scale ({100.0 * clipped / x.size:.4f}%)")

    ol, orr = float(np.sqrt(np.mean(left ** 2))), float(np.sqrt(np.mean(right ** 2)))
    diff = db(ol) - db(orr)
    flag = "IMBALANCED" if abs(diff) > 3.0 and max(ol, orr) > 1e-4 else "ok"
    corr = float(np.corrcoef(left, right)[0, 1]) if np.std(left) > 0 and np.std(right) > 0 else float("nan")
    print(f"L/R: level difference {diff:+.1f} dB ({flag}); correlation {corr:.3f}"
          + ("  (one channel silent)" if (ol < 1e-5) != (orr < 1e-5) else ""))

    if a.png:
        from PIL import Image, ImageDraw
        W, H = max(600, n * 12 + 80), 300
        im = Image.new("RGB", (W, H), "white")
        d = ImageDraw.Draw(im)
        for dbv in (0, -20, -40, -60, -80):
            y = int(10 + (-dbv) / 100.0 * (H - 30))
            d.line([(40, y), (W - 5, y)], fill=(220, 220, 220))
            d.text((2, y - 5), str(dbv), fill="gray")
        for series, col in ((ldb, (30, 90, 200)), (rdb, (200, 60, 30))):
            pts = [(40 + (i + 0.5) * (W - 50) / n, 10 + (-max(v, -100.0)) / 100.0 * (H - 30)) for i, v in enumerate(series)]
            if len(pts) > 1:
                d.line(pts, fill=col, width=2)
        d.text((45, H - 15), f"RMS dBFS per second, L blue / R red, from {t0:.0f} s", fill="black")
        im.save(a.png)
        print(f"png: {a.png}")


if __name__ == "__main__":
    main()
