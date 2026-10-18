#!/usr/bin/env python3
"""Measure the moon's jitter in frames dumped with DOL_AURORA_FRAME_INTERP_DUMP (see moon_sail.txt).

    python scripts/windows/moon_jitter.py <dump folder> <first game frame> <last game frame>

Takes the centroid of the moon's yellow pixels in each real and in-between frame (in the order
they are shown: the in-between frames, then the real frame), prints it, and reports how far each
frame lies from the midpoint of its neighbours. The moon moves smoothly across the sky, so the
mean is a few tenths of a pixel when the in-between frames follow it, and pixels when they lag.
The search box is for a 1280x720 window with the moon at the upper left, as moon_sail.txt shows it.
Needs numpy and Pillow.
"""
import sys

import numpy as np
from PIL import Image

ORDER = ["between", "between2", "between3", "real"]
BOX = (60, 150, 420, 300)  # x0, y0, x1, y1


def centroid(path):
    a = np.asarray(Image.open(path)).astype(int)
    x0, y0, x1, y1 = BOX
    reg = a[y0:y1, x0:x1]
    mask = (reg[:, :, 0] > 150) & (reg[:, :, 1] > 140) & (reg[:, :, 2] < 170) & (reg[:, :, 0] - reg[:, :, 2] > 25)
    ys, xs = np.nonzero(mask)
    return (xs.mean() + x0, ys.mean() + y0, len(xs)) if len(xs) else (float("nan"), float("nan"), 0)


def main():
    folder, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    points = []
    for frame in range(lo, hi + 1):
        for kind in ORDER:
            x, y, n = centroid(f"{folder}/frame{frame:06d}-{kind}.ppm")
            print(f"{frame} {kind:9} x={x:.1f} y={y:.1f} n={n}")
            if x == x:
                points.append((x, y))
    dev = [max(abs(points[i][k] - (points[i - 1][k] + points[i + 1][k]) / 2) for k in (0, 1))
           for i in range(1, len(points) - 1)]
    print(f"JITTER mean {np.mean(dev):.3f} max {np.max(dev):.3f} px (n={len(dev)})")


if __name__ == "__main__":
    main()
