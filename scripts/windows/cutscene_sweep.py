#!/usr/bin/env python3
"""Cutscene audio sweep: writes the cut_*.txt scenarios, runs them and judges each cutscene's audio.

    python scripts/windows/cutscene_sweep.py gen                 # (re)write scenarios/cut_*.txt from PLAN
    python scripts/windows/cutscene_sweep.py run cut_linkrm ...  # run scenarios, then analyze
    python scripts/windows/cutscene_sweep.py analyze cut_linkrm  # analyze the last run only
    python scripts/windows/cutscene_sweep.py list

Each PLAN group is one scenario (one game boot): segments run one after another, each a `warp` (a scene
change as a door makes one) after the pokes that clear its once-only switches/event bits, then `A` pulses
to get through message boxes. The analysis reads build/windows/scenarios/<name>/<size>/run.log and audio.wav:

  - [demo] start/end lines (the host's cutscene watcher, runtime/host/src/audio_watch.c): cues, sounds,
    missing, silent seconds; [demo-sound] ... no sound; [audio-lost];
  - [music-stream] lines: which streamed track was prepared (2) / loaded (3) / playing (4);
  - the WAV in game time (retrace = 1/60 s): RMS per segment and silent stretches.
It prints a verdict hint per segment; the doc (docs/fork/cutscene-audio.md) records the final judgement.
"""
import json
import os
import re
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SCEN = os.path.join(HERE, "scenarios")
OUT = os.path.join(ROOT, "build", "windows", "scenarios")
STATES = os.path.join(ROOT, "build", "windows", "states")
MARKS = os.path.join(OUT, "_cut_marks")  # sidecars: kept out of scenarios/ (scenario.py loads every .json there)


def marks_path(name):
    os.makedirs(MARKS, exist_ok=True)
    return os.path.join(MARKS, name + ".json")

# Stage-memory table of every stage is in cutscene_list.py (spawn switch lines).
STATE_START = {"outset": 8601, "forest-card": 1916}


def seg(label, warp, frames, pre=(), a=True, expect="", settle=240, post=(), pulse=100):
    return dict(label=label, warp=warp, frames=frames, pre=list(pre), a=a, expect=expect, settle=settle,
                post=list(post), pulse=pulse)


def clear_byte(addr, live_off, tbl):
    """Poke zero into a stage-memory byte and its live copy (0x803C5380 + offset)."""
    return ["poke 0x%08X=0/1" % addr, "poke 0x%08X=0/1" % (0x803C5380 + live_off)]


PLAN = {}
SPAWN_EVENT_PICK = {  # spawn-started event cutscenes (no STB) worth hearing: the story ones
    "look_tetra", "hy_intro", "WARP_ARRIVE", "WALK_START", "MORI_INSIDE", "BOSS_WARPOUT", "TOWER_WARPOUT", "BirdStart",
    "FROM_GANON", "FROM_HYRULE_1", "FROM_HYRULE_2", "GANON_ARRIVE", "rtn_warp", "fg_warp0", "fg_warp1", "Go_00",
    "Go_01", "Go_02", "TowerLook2", "TowerLook3", "TowerLook4", "SirenR11", "moro_scam", "next_link", "kankin1",
    "kankin2", "StartCamera", "race_start_cam", "ICE_FAILED", "VOLCANO_FAILED", "R29LOOK_TAKARA", "R29LOOK_KINDAN",
    "R29LOOK_MANTEN"}


ALL_SPAWN = False


def pick_layer(layers):
    big = [l for l in layers if l >= 8]
    return (big or layers)[0]


def build_plan():
    """One group (scenario) per stage: every spawn-started STB demo, plus the picked spawn-started events."""
    import cutscene_list as C
    inv = C.Inventory()
    entries = C.spawn_plan(inv) + [e for e in C.spawn_plan(inv, kinds=("event", "util")) if e["event"] in SPAWN_EVENT_PICK or ALL_SPAWN]
    by_stage = {}
    for e in entries:
        by_stage.setdefault(e["stage"], []).append(e)
    plan = {}
    for st, es in sorted(by_stage.items()):
        name = "cut_" + re.sub(r"\W", "", st).lower()
        segs = []
        for e in es:
            pre = []
            sw = e["switch"]
            if sw and sw["byte"]:
                pre += ["poke 0x%08X=0/1" % sw["byte"], "poke 0x%08X=0/1" % sw["live"]]
            layer = pick_layer(e["layers"])
            warp = "%s:%d:%d" % (e["stage"], e["room"], e["point"]) + (":%d" % layer if layer >= 0 else "")
            if e["frames"]:
                frames = int(e["frames"] * 2 * 2.0) + 900
                exp = "%s %d frames (%.0f s): %d cue(s) start sounds%s%s" % (
                    e["stb"], e["frames"], e["frames"] / 30.0, e["cues"], (", streams " + "/".join(e["streams"])) if e["streams"] else "",
                    (", then loads " + e["ends_into"]) if e["ends_into"] else "")
            else:
                frames, exp = 1500, "event-system cutscene (no STB); sounds from the event's actors"
            segs.append(seg("%s (event #%d)" % (e["event"], e["index"]), warp, frames, pre=pre, expect=exp))
        plan[name] = dict(description="Cutscene audio, stage %s: %s" % (st, ", ".join(e["event"] for e in es)),
                          state="outset" if st in ("LinkRM", "Ojhous", "Ojhous2") else "forest-card",
                          card=st not in ("LinkRM", "Ojhous", "Ojhous2"), segments=segs)
    return plan


def event_bit_poke(bit, on=True):
    """Poke for an event bit: byte 0x803C4C08 + 0x624 + (id >> 8), mask id & 0xFF. The byte is written whole (a test
    only): the other seven bits of that byte are cleared."""
    return "poke 0x%08X=0x%02X/1" % (0x803C4C08 + 0x624 + (bit >> 8), (bit & 0xFF) if on else 0)


def build_tag_plan(camera_only=False):
    """Event cutscenes started by a TagEv volume (daTag_Event_c, d_a_tag_event.cpp:250-470): teleport into the
    volume. Only events with sound-bearing staffs (STB package, SOUND, MESSAGE) that no spawn point starts."""
    import cutscene_list as C
    inv = C.Inventory()
    spawn_started = {(e["stage"].lower(), e["event"]) for e in C.spawn_plan(inv, kinds=("stb", "event", "util"))}
    plan = {}
    for st in inv.stages:
        if re.match(r"(k_test|i_test|itest|h_test|vrtest|test)", st):
            continue
        d = inv.stage_data[st]
        el = d["events"]
        if el is None:
            continue
        tab = d["evnt"].get("stage", [])
        segs = []
        for idx, ev in enumerate(el.events):
            script = el.event_script(ev)
            kind, stb = inv.classify(st, ev, script)
            types = {t for _n, t, _c in script}
            if kind in ("generic", "util") or (st, ev["name"]) in spawn_started:
                continue
            audio = bool(stb or "MESSAGE" in types or "SOUND" in types)
            if audio == camera_only:
                continue
            tags = [t for t in inv.tag_triggers(st, ev) if t[0] == "TagEv" and t[1] != "stage" or t[0] == "TagEv"]
            if not tags:
                continue
            t = tags[0]
            where, ctag, pos, flag, param = t[1], t[2], t[3], t[4], t[5]
            room = where if where != "stage" else 0
            swbit = (param >> 8) & 0xFF
            pre = []
            tbl = d.get("save_tbl")
            if swbit < 0x80 and tbl is not None:
                base = 0x803C4F88 + tbl * 0x24
                byte = base + 4 + (swbit >> 5) * 4 + 3 - ((swbit & 31) >> 3)
                pre += ["poke 0x%08X=0/1" % byte, "poke 0x%08X=0/1" % (0x803C5380 + byte - base)]
            if flag:
                pre.append(event_bit_poke(flag))
            warp_to = "%s %d %d %d %d" % (inv.cased.get(st, st), room, pos[0], pos[1] + 20, pos[2])
            exp = "event #%d %s (staffs %s)%s" % (idx, ev["name"], ",".join(sorted(types)),
                                                 (", STB " + stb[0]) if stb else "")
            dur = 1800
            if stb:
                _arc, info = inv.stb_for(st, stb[0])
                if info:
                    dur = int(info["frames"] * 8) + 900
            segs.append(dict(label="%s (event #%d)" % (ev["name"], idx), tele=warp_to, frames=dur, pre=pre,
                             expect=exp, a=True, settle=300, post=[], pulse=100, warp=None))
        if segs:
            plan[("cut_tagc_" if camera_only else "cut_tag_") + re.sub(r"\W", "", st).lower()] = dict(
                description="Cutscene audio via TagEv teleports, stage %s: %s" % (st, ", ".join(x["label"] for x in segs)),
                state="forest-card", card=True, segments=segs)
    return plan


def load_plan():
    if not PLAN:
        PLAN.update(build_plan())
        PLAN.update(build_tag_plan())
        PLAN.update(build_tag_plan(camera_only=True))
    return PLAN


def gen_lines(name, g):
    lines = ["description %s" % g["description"], "start state %s" % g["state"]]
    if g.get("card"):
        lines.append("card user/GZLE01.card")
    lines += ["audio", "size 1280x960"]  # no WATCH_TRACE: the watcher has a 600-line budget per process
    for k, v in g.get("env", {}).items():
        lines.append("env %s %s" % (k, v))
    lines += list(g.get("setup", []))
    lines.append("wait 10")
    t = 10
    marks = []
    for s in g["segments"]:
        for p in s["pre"]:
            lines.append(p)
        if s.get("tele"):
            lines.append("teleport %s" % s["tele"])
        else:
            lines.append("warp %s" % s["warp"])
        start = t
        remaining = s["frames"]
        settle = min(s["settle"], remaining)
        lines.append("wait %d" % settle)
        remaining -= settle
        t += settle
        while remaining > 0:
            step = min(s["pulse"], remaining)
            if s["a"]:
                lines.append("press A 2")
                lines.append("wait %d" % max(step - 2, 1))
            else:
                lines.append("wait %d" % step)
            t += step
            remaining -= step
        for p in s["post"]:
            lines.append(p)
        marks.append(dict(label=s["label"], at=start, end=t, warp=s["warp"] or ("teleport " + s["tele"]), expect=s["expect"]))
    lines.append("end")
    return lines, marks


def write_scenario(name, g, lines, marks):
    nl = chr(10)
    with open(os.path.join(SCEN, name + ".txt"), "w", encoding="utf8", newline=nl) as f:
        f.write(nl.join(lines) + nl)
    with open(marks_path(name), "w", encoding="utf8") as f:
        json.dump(dict(state=g["state"], marks=marks), f, indent=1)


def gen_all(only=None):
    os.makedirs(SCEN, exist_ok=True)
    for name, g in load_plan().items():
        if only and not any(name.startswith(o) for o in only):
            continue
        lines, marks = gen_lines(name, g)
        with open(os.path.join(SCEN, name + ".txt"), "w", encoding="utf8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        with open(marks_path(name), "w", encoding="utf8") as f:
            json.dump(dict(state=g["state"], marks=marks), f, indent=1)
        print("wrote", name, "(%d segments, %d retraces)" % (len(marks), marks[-1]["end"]))


# --------------------------------------------------------------------------- analysis

def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        data = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    return data.reshape(-1, ch), rate


def dbfs(x):
    return -120.0 if x <= 1e-9 else max(-120.0, 20 * np.log10(x))


def wav_stats(data, rate, a, b, thresh=-60.0):
    """RMS and silent stretches (> 1 s) for retraces a..b (relative to the capture start)."""
    s0, s1 = int(a / 60.0 * rate), min(int(b / 60.0 * rate), len(data))
    if s1 <= s0:
        return None
    x = data[s0:s1]
    rms = [dbfs(float(np.sqrt(np.mean(x[:, c] ** 2)))) for c in range(x.shape[1])]
    win = rate // 10
    n = len(x) // win
    per = np.array([dbfs(float(np.sqrt(np.mean(x[i * win:(i + 1) * win] ** 2)))) for i in range(n)])
    spans, start = [], None
    for i, v in enumerate(per):
        if v < thresh:
            start = i if start is None else start
        elif start is not None:
            if (i - start) / 10.0 > 1.0:
                spans.append((round(start / 10.0, 1), round(i / 10.0, 1)))
            start = None
    if start is not None and (n - start) / 10.0 > 1.0:
        spans.append((round(start / 10.0, 1), round(n / 10.0, 1)))
    quiet = float((per < thresh).sum()) / 10.0
    return dict(rms_l=round(rms[0], 1), rms_r=round(rms[-1], 1), peak=round(dbfs(float(np.abs(x).max())), 1),
                silent_spans=spans, silent_total=round(quiet, 1), seconds=round(n / 10.0, 1))


def analyze(name, size="1280x960"):
    d = os.path.join(OUT, name, size)
    log_path, wav_path = os.path.join(d, "run.log"), os.path.join(d, "audio.wav")
    side = json.load(open(marks_path(name), encoding="utf8"))
    marks = side["marks"]
    base = STATE_START.get(side["state"], 0)
    events = []  # (retrace, kind, text)
    cur = base
    for line in open(log_path, encoding="utf8", errors="replace"):
        m = re.search(r"retrace=(\d+)", line)
        if m and (line.startswith("[load]") or line.startswith("[demo] start") or line.startswith("[music-stream]")
                  or line.startswith("[test]")):
            cur = int(m.group(1))
        if line.startswith(("[demo]", "[demo-sound]", "[audio-lost]", "[scene] stage", "[music-stream]", "[event]")):
            events.append((cur, line.split(" ")[0] if not line.startswith("[scene]") else "[scene]", line.strip()))
    wav = read_wav(wav_path) if os.path.exists(wav_path) else None
    result = []
    # Attribute log events to segments: by retrace window, except a [demo] end / [demo-sound] / [audio-lost]
    # that follows a start belongs to the segment of that start.
    seg_of = []
    cur_seg = 0
    demo_seg = None
    for r_, k_, t_ in events:
        for i, mk in enumerate(marks):
            if base + mk["at"] <= r_ < base + (marks[i + 1]["at"] if i + 1 < len(marks) else mk["end"] + 100000):
                cur_seg = i
        if k_ == "[demo]" and t_.startswith("[demo] start"):
            demo_seg = cur_seg
        elif k_ in ("[demo]", "[demo-sound]", "[audio-lost]") and demo_seg is not None:
            cur_seg = demo_seg  # the end, the cues and the loss lines of a demo belong to its start's segment
        seg_of.append(cur_seg)
    for i, mk in enumerate(marks):
        a, b = base + mk["at"], base + mk["end"]
        ev = [e for e, sidx in zip(events, seg_of) if sidx == i]
        demos = [e[2] for e in ev if e[1] == "[demo]"]
        lost = [e[2] for e in ev if e[1] == "[audio-lost]"]
        nosound = [e[2] for e in ev if e[1] == "[demo-sound]" and "no sound" in e[2]]
        scenes = [e[2] for e in ev if e[1] == "[scene]"]
        cues = [e[2] for e in ev if e[1] == "[demo-sound]"]
        evl = [e[2] for e in ev if e[1] == "[event]"]
        streams = []
        for e in ev:
            if e[1] == "[music-stream]":
                m = re.search(r'path="([^"]*)" id=(\S+) state=(\d+)', e[2])
                if m and m.group(1):
                    streams.append((e[0], m.group(1).split("/")[-1], int(m.group(3))))
        st = wav_stats(*(wav[0], wav[1], mk["at"], mk["end"])) if wav else None
        al = None
        rr = dict(label=mk["label"], warp=mk["warp"], expect=mk["expect"], retraces=(a, b), demos=demos)
        try:
            al = align(rr, wav, base)
        except Exception as e:  # analysis only
            al = None
        result.append(dict(align=al, label=mk["label"], warp=mk["warp"], expect=mk["expect"], retraces=(a, b), demos=demos,
                           lost=lost, nosound=nosound, cues=cues, evl=evl, scenes=scenes, streams=streams, wav=st))
    return result


_INV = []


def stb_cues(stb):
    if not _INV:
        import cutscene_list as C
        _INV.append(C.Inventory())
    inv = _INV[0]
    for (arc, fn), info in sorted(inv.stb_info.items()):
        if fn == stb:
            return info
    for st, d in inv.stage_data.items():
        if stb in d["stage_stbs"]:
            return d["stage_stbs"][stb]
    return None


def window_db(data, rate, t0, t1):
    a, b = max(int(t0 * rate), 0), min(int(t1 * rate), len(data))
    if b <= a:
        return None
    x = data[a:b]
    return dbfs(float(np.sqrt(np.mean(x ** 2))))


def align(r, wav, base, step=2.0):
    """Per STB cue: is there sound where the cue starts? Only when the demo ran its STB length (no message wait
    shifts its clock): demo frame f is retrace start + 2f (30 fps)."""
    m = re.search(r"([\w]+\.stb)", r["expect"])
    if not m or wav is None:
        return None
    info = stb_cues(m.group(1))
    starts = [d for d in r["demos"] if d.startswith("[demo] start")]
    ends = [d for d in r["demos"] if d.startswith("[demo] end")]
    if not info or not starts or not ends:
        return None
    r0 = int(re.search(r"retrace=(\d+)", starts[0]).group(1))
    fr = int(re.search(r"frames=(\d+)", ends[0]).group(1))
    drift = abs(fr - info["frames"]) / max(info["frames"], 1)
    out = dict(frames=fr, stb_frames=info["frames"], drift=round(drift, 3), cues=[])
    data, rate = wav
    mono = np.max(np.abs(data), axis=1) if data.shape[1] > 1 else data[:, 0]
    for t, sid, ident, started in sorted(info["cues"]):
        if not started:
            continue
        wt = (r0 - base) / 60.0 + t * step / 60.0 * 30.0 / 30.0 / 1.0
        wt = (r0 - base) / 60.0 + t / 30.0
        before = window_db(mono, rate, wt - 0.6, wt - 0.05)
        at = window_db(mono, rate, wt, wt + 0.6)
        out["cues"].append((t, sid, round(wt, 1), None if before is None else round(before, 1), None if at is None else round(at, 1)))
    return out


def hint(r):
    ended = [d for d in r["demos"] if d.startswith("[demo] end")]
    started = [d for d in r["demos"] if d.startswith("[demo] start")]
    notes = []
    if started and not ended:
        notes.append("demo started but did not end in the window")
    if r["lost"]:
        notes.append("audio-lost reported")
    if r["nosound"]:
        notes.append("%d cue(s) with no sound" % len(r["nosound"]))
    m = re.search(r"event #(\d+)", r["label"])
    triggered = m and any(re.search(r"\[event\] start id=%s " % m.group(1), l) for l in r["evl"])
    if not started:
        notes.append("event started (event-system cutscene, no STB demo)" if triggered else "event NOT started")
    for d in ended:
        m = re.search(r"cues=(\d+) sounds=(\d+) missing=(\d+) silent=([\d.]+)s of ([\d.]+)s", d)
        if m and int(m.group(3)) > 0:
            notes.append("missing=%s" % m.group(3))
    return "; ".join(notes) or "ok"


def sound_name_(sid):
    import cutscene_list as C
    return C.sound_name(sid)


def report(name):
    res = analyze(name)
    for r in res:
        print("== %s  (%s)  retraces %d..%d" % (r["label"], r["warp"], *r["retraces"]))
        if r["expect"]:
            print("   expect: %s" % r["expect"])
        for s in r["scenes"]:
            print("   " + s)
        for d in r["demos"]:
            print("   " + d)
        for d in r["evl"][:6]:
            print("   " + d)
        for d in r["lost"]:
            print("   " + d)
        for d in r["cues"]:
            print("   " + d)
        for s in r["streams"]:
            print("   stream retrace=%d %s state=%d" % s)
        w = r["wav"]
        al = r.get("align")
        if al:
            print("   align: demo ran %d of STB %d frames (drift %.0f%%)%s" % (al["frames"], al["stb_frames"], al["drift"] * 100,
                  "" if al["drift"] < 0.05 else ", message waits shift the cue times"))
            for t, sid, wt, b, a in al["cues"][:40]:
                print("      cue f%-5d %-34s at wav %.1f s: before %s, at %s dBFS" % (t, sound_name_(sid), wt, b, a))
        if w:
            print("   wav: %.1f s, RMS L %.1f R %.1f dBFS, peak %.1f, silent total %.1f s, spans %s" % (
                w["seconds"], w["rms_l"], w["rms_r"], w["peak"], w["silent_total"], w["silent_spans"][:6]))
        print("   hint: %s" % hint(r))
    return res


def bad_segment(r, window):
    """A crash or a stall: the segment's WAV is missing or short, or the whole window is silent (-55 dBFS)."""
    w = r["wav"]
    return (w is None or w["seconds"] < 0.5 * window / 60.0 or max(w["rms_l"], w["rms_r"]) < -44.0
            or w["silent_total"] >= 0.8 * w["seconds"])


def nl_join(lines):
    return chr(10).join(lines) + chr(10)


def maybe_continue(n, res, depth):
    side = json.load(open(marks_path(n), encoding="utf8"))
    for i, r in enumerate(res):
        mk = side["marks"][i]
        if bad_segment(r, mk["end"] - mk["at"]) and i + 1 < len(res) and depth < 40:
            k = int(n.split("__")[1]) + i + 1 if "__" in n else i + 1
            base = n.split("__")[0]
            g = load_plan().get(base)
            if g is None:
                return
            cont = dict(g)
            cont["segments"] = g["segments"][k:]
            cont["description"] = g["description"] + " (continued from segment %d)" % k
            cname = "%s__%d" % (base, k)
            PLAN[cname] = cont
            lines, marks = gen_lines(cname, cont)
            write_scenario(cname, cont, lines, marks)
            print("segment %d of %s (%s) crashed or stalled; continuing as %s" % (i, n, r["label"], cname))
            if os.path.exists(os.path.join(OUT, cname, "1280x960", "audio.wav")) and os.path.exists(os.path.join(SCEN, cname + ".txt"))                     and open(os.path.join(SCEN, cname + ".txt"), encoding="utf8").read() == nl_join(lines):
                maybe_continue(cname, analyze(cname), depth + 1)  # already run: follow its chain only
            else:
                run([cname], depth + 1)
            return


def run(names, depth=0):
    """Run scenarios; when a segment crashes or stalls the game, the segments after it run as a continuation
    scenario <name>__<k> (a crash or a stalled stage load ends the useful part of a boot)."""
    for n in names:
        subprocess.call([sys.executable, os.path.join(HERE, "scenario.py"), n])
        maybe_continue(n, report(n), depth)


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    cmd = argv[1]
    if cmd == "gen":
        gen_all(argv[2:])
    elif cmd == "list":
        for n, g in load_plan().items():
            print(n, "-", g["description"])
    elif cmd == "run":
        run(argv[2:])
    elif cmd == "cont":
        for n in argv[2:]:
            maybe_continue(n, report(n), 0)
    elif cmd == "analyze":
        for n in argv[2:]:
            report(n)
    else:
        print("unknown command", cmd)
        return 2
    return 0


if __name__ == "__main__":
    sys.path.insert(0, HERE)
    sys.exit(main(sys.argv))
