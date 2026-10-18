#!/usr/bin/env python3
"""Writes scenarios/cc_<name>.txt for the cutscene crash triage (testing only).

    python scripts/windows/cutcrash_scn.py NAME STATE "STAGE:ROOM:POINT:LAYER" [--poke ADDR=VAL/SIZE ...]
        [--evbit 0x2D01 ...] [--frames N] [--shot-every N] [--give item ...]

Event bits are set with --evbit (the listed bits of a byte are written as that whole byte before the warp, so the play scene mounts the
right Link demo-animation archive and picks the right layer).
"""
import argparse
import pathlib

OUT = pathlib.Path(__file__).parent / "scenarios"
EV = 0x803C4C08 + 0x624


def main():
    p = argparse.ArgumentParser()
    p.add_argument("name")
    p.add_argument("state")
    p.add_argument("warp")
    p.add_argument("--poke", action="append", default=[])
    p.add_argument("--evbit", action="append", default=[])
    p.add_argument("--frames", type=int, default=2400)
    p.add_argument("--shot-every", type=int, default=0)
    p.add_argument("--audio", action="store_true")
    p.add_argument("--pre", action="append", default=[], help="extra lines before the warp")
    p.add_argument("--post", action="append", default=[], help="extra lines after the warp")
    a = p.parse_args()
    L = [f"description cutcrash {a.name}", f"start state {a.state}"]
    if a.state == "forest-card":
        L.append("card user/GZLE01.card")
    if a.audio:
        L.append("audio")
    L += ["size 640x480", "wait 10"]
    bytes_ = {}
    for e in a.evbit:
        v = int(e, 0)
        bytes_[v >> 8] = bytes_.get(v >> 8, 0) | (v & 0xFF)
    for b, m in sorted(bytes_.items()):
        L.append(f"poke {EV + b:#x}={m:#x}/1")
    L += [f"poke {x}" for x in a.poke]
    L += a.pre
    L.append(f"warp {a.warp}")
    L += a.post
    done = 0
    while done < a.frames:
        L.append("wait 98")
        L.append("press A 2")
        done += 100
        if a.shot_every and done % a.shot_every == 0:
            L.append("shot")
    L.append("end")
    (OUT / f"cc_{a.name}.txt").write_text("\n".join(L) + "\n")


main()
