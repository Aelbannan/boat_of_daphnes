#!/usr/bin/env python3
"""Inspect the generated watch set's collision costs; not a gameplay trace."""
import argparse
import json
import re
from pathlib import Path


def analyze(text):
    addresses = [int(x, 16) & ~0x40000000
                 for x in re.findall(r"0x([0-9a-fA-F]+)[uU]?", text)]
    table = [0] * 4096
    for address in addresses:
        if address == 0:
            raise ValueError("zero is reserved as the empty-slot sentinel")
        slot = ((address * 0x9E3779B1) & 0xFFFFFFFF) >> 20
        for _ in range(len(table)):
            if table[slot] in (0, address):
                table[slot] = address
                break
            slot = (slot + 1) & 4095
        else:
            raise ValueError("watch table has no free slot")
    # Uniform starting buckets expose clustering only. Actual guest addresses
    # and repeated-address rates require a separate instrumented game run.
    miss_probes = []
    for start in range(len(table)):
        probes = 1
        while table[(start + probes - 1) & 4095] and probes < 4096:
            probes += 1
        miss_probes.append(probes)
    occupied = sum(bool(x) for x in table)
    return {"unique_addresses": occupied, "slots": len(table),
            "occupancy": occupied / len(table),
            "uniform_bucket_miss_mean_probes": sum(miss_probes) / len(table),
            "uniform_bucket_miss_max_probes": max(miss_probes),
            "scope": "static table geometry; no runtime frequency or cache hit-rate claim"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("watch_include", type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.watch_include.read_text()), indent=2))
