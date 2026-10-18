#!/usr/bin/env python3
"""Inventory of every cutscene in The Wind Waker (GZLE01) from the disc and the decomp.

Reads the disc image (never extracts it) and the tww decomp sources (read only) and prints, for each
stage: its events (event_list.dat in Stage.arc), the STB demos they play (PACKAGE staff, FileName),
the sound cues inside each STB (JSND blocks), the event-system sound/message actions, how the event
is triggered (decomp file:line where its name is used; tag actors placed in the stage that start it by
event number) and the event bits that gate it.

    python scripts/windows/cutscene_list.py                 # the full inventory (text)
    python scripts/windows/cutscene_list.py --stage linkrm  # one stage
    python scripts/windows/cutscene_list.py --event TALE_DEMO
    python scripts/windows/cutscene_list.py --stb           # STB files with their sound cues
    python scripts/windows/cutscene_list.py --markdown      # the same as a markdown table

Decomp evidence (all under tww-main): d_event_data.cpp:758-830 (PACKAGE staff starts an STB demo:
FileName from the stage's Demo<NN>.arc or Stage.arc, EventFlag is set when it starts),
d_s_room.cpp:205-216 (Demo<NN>.arc chosen from the room's Lbnk bank), JStudio_JAudio/object-sound.cpp
(TAdaptor_sound::adaptor_do_SOUND starts the cue's sound), d_a_tag_event.cpp (TagEv: event number =
param >> 24, event bit = angle z, switches = param bits 8 and 16).
"""
import argparse
import os
import re
import struct
import sys
from collections import OrderedDict, defaultdict
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swift_sail as S  # noqa: E402

ISO = local_path("BOD_DISC")
DECOMP = local_path("BOD_TWW_DECOMP")

STAFF_TYPES = {0: "NORMAL", 1: "ALL", 2: "CAMERA", 3: "EFFECT", 4: "TIMEKEEPER", 5: "FIVE", 6: "DIRECTOR",
               7: "MESSAGE", 8: "SOUND", 9: "LIGHT", 10: "TEN", 11: "PACKAGE", 12: "CREATE"}
# Events that exist in nearly every stage's list: doors, treasure chests, talking, warps, item gets.
# They are the event system's utilities, not story cutscenes.
GENERIC_STAGES = 60


def cstr(b, o, n=32):
    return b[o:o + n].split(b"\0")[0].decode("latin1")


# ---------------------------------------------------------------------------
# event_list.dat (decomp include/d/d_event_data.h)
# ---------------------------------------------------------------------------

class EventList:
    def __init__(self, blob):
        self.b = blob
        h = struct.unpack_from(">16I", blob, 0)
        self.h = h
        self.events, self.staffs, self.cuts, self.data = [], [], [], []
        for i in range(h[1]):
            o = h[0] + i * 0xB0
            ns = struct.unpack_from(">i", blob, o + 0x7C)[0]
            self.events.append(dict(name=cstr(blob, o), pri=struct.unpack_from(">I", blob, o + 0x28)[0],
                                    staff=list(struct.unpack_from(">20I", blob, o + 0x2C))[:max(ns, 0)],
                                    endsnd=blob[o + 0x94]))
        for i in range(h[3]):
            o = h[2] + i * 0x50
            self.staffs.append(dict(name=cstr(blob, o), tag=struct.unpack_from(">i", blob, o + 0x20)[0],
                                    type=struct.unpack_from(">i", blob, o + 0x2C)[0],
                                    first=struct.unpack_from(">i", blob, o + 0x30)[0]))
        for i in range(h[5]):
            o = h[4] + i * 0x50
            t = struct.unpack_from(">IIIIIIIII", blob, o + 0x20)
            self.cuts.append(dict(name=cstr(blob, o), first=struct.unpack_from(">i", blob, o + 0x38)[0],
                                  next=struct.unpack_from(">i", blob, o + 0x3C)[0]))
        for i in range(h[7]):
            o = h[6] + i * 0x40
            t = struct.unpack_from(">iiiii", blob, o + 0x20)
            self.data.append(dict(name=cstr(blob, o), type=t[1], idx=t[2], size=t[3], next=t[4]))

    def value(self, x):
        b, h = self.b, self.h
        if x["type"] == 4:
            return cstr(b, h[12] + x["idx"], 64)
        if x["type"] == 3:
            n = min(max(x["size"], 1), 8, (len(b) - h[10] - x["idx"] * 4) // 4)
            v = struct.unpack_from(">%di" % max(n, 1), b, h[10] + x["idx"] * 4)
            return v[0] if n == 1 else list(v)
        if x["type"] == 0:
            return round(struct.unpack_from(">f", b, h[8] + x["idx"] * 4)[0], 3)
        if x["type"] == 1:
            return tuple(round(v, 1) for v in struct.unpack_from(">3f", b, h[8] + x["idx"] * 4))
        return None

    def cut_data(self, cut):
        out, i, n = OrderedDict(), cut["first"], 0
        while i >= 0 and n < 64:
            x = self.data[i]
            out[x["name"]] = self.value(x)
            i, n = x["next"], n + 1
        return out

    def event_script(self, ev):
        """[(staff name, type name, [(cut name, {data})...])]"""
        res = []
        for si in ev["staff"]:
            s = self.staffs[si]
            cuts, c, n = [], s["first"], 0
            while c >= 0 and n < 200:
                cu = self.cuts[c]
                cuts.append((cu["name"], self.cut_data(cu)))
                c, n = cu["next"], n + 1
            res.append((s["name"], STAFF_TYPES.get(s["type"], str(s["type"])), cuts))
        return res


# ---------------------------------------------------------------------------
# STB (JStudio binary): sound cues
# ---------------------------------------------------------------------------

def stb_summary(blob):
    """Block counts, length in frames (longest object) and the JSND sound cues [(frame, id, object)]."""
    if blob[:4] != b"STB\0":
        return None
    nblocks = struct.unpack_from(">I", blob, 0x0C)[0]
    off = 0x20
    kinds = defaultdict(int)
    cues, length = [], 0
    for _ in range(nblocks):
        size, typ = struct.unpack_from(">I4s", blob, off)
        typ = typ.decode("latin1")
        kinds[typ] += 1
        if typ == "JSND" or typ in ("JACT", "JCMR", "JMSG", "JPTC", "JLIT", "JFOG", "JABL"):
            flag, idsz = struct.unpack_from(">HH", blob, off + 8)
            ident = blob[off + 12:off + 12 + idsz].split(b"\0")[0].decode("latin1")
            p = off + 12 + ((idsz + 3) & ~3)
            end = off + size
            t = 0
            last = None
            while p < end:
                head = struct.unpack_from(">I", blob, p)[0]
                ty, param = head >> 24, head & 0xFFFFFF
                if ty == 0:
                    break
                if ty <= 0x7F:
                    if ty == 2:
                        t += param
                    elif ty == 3:  # jump: not followed (loops end a sequence)
                        break
                    p += 4
                    continue
                q, stop = p + 4, p + 4 + param
                while q < stop:
                    w = struct.unpack_from(">H", blob, q)[0]
                    if w & 0x8000:
                        psz = ((w << 16) & 0x7FFF0000) | struct.unpack_from(">H", blob, q + 2)[0]
                        ptype = struct.unpack_from(">I", blob, q + 4)[0]
                        q += 8
                    else:
                        psz = w
                        ptype = struct.unpack_from(">H", blob, q + 2)[0]
                        q += 4
                    if typ == "JSND" and ptype == ((60 << 5) | 0x19) and psz >= 4:
                        last = [t, struct.unpack_from(">I", blob, q)[0], ident, False]
                        cues.append(last)
                    elif typ == "JSND" and (ptype >> 5) == 46 and last is not None:
                        # kind 46 = TAdaptor_sound's BEGIN_FADE_IN value (jstudio-object.cpp, TObject_sound::
                        # do_paragraph case 46 -> variable 3): it starts the sound; the host's [demo] cue count
                        # counts these (audio_watch.h: TAdaptor_sound's fade-in output).
                        last[0], last[3] = t, True
                    if ptype == 0x02 and psz >= 4:  # reserved: wait
                        t += struct.unpack_from(">I", blob, q)[0]
                    q += (psz + 3) & ~3
                p = stop
            length = max(length, t)
        off += size
    return dict(blocks=dict(kinds), frames=length, cues=[tuple(c) for c in cues])


# ---------------------------------------------------------------------------
# Sound ids named from the decomp enums
# ---------------------------------------------------------------------------

_sound_names = None


def sound_names():
    global _sound_names
    if _sound_names is None:
        _sound_names = {}
        for fn in ("JAZelAudio_BGM.h", "JAZelAudio_SE.h"):
            path = os.path.join(DECOMP, "include", "JAZelAudio", fn)
            if not os.path.exists(path):
                continue
            for line in open(path, encoding="utf8", errors="replace"):
                m = re.match(r"\s*(JA_\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", line)
                if m:
                    _sound_names.setdefault(int(m.group(2), 0), m.group(1))
    return _sound_names


def sound_name(i):
    return sound_names().get(i, "0x%08X" % i)


# ---------------------------------------------------------------------------
# Decomp trigger search
# ---------------------------------------------------------------------------

_src_index = None
_flag_values = None


def flag_values():
    global _flag_values
    if _flag_values is None:
        _flag_values = {}
        path = os.path.join(DECOMP, "include", "d", "d_save_event_flag.inc")
        for line in open(path, encoding="utf8", errors="replace"):
            m = re.match(r"\s*/\*.*\*/\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", line)
            if m:
                _flag_values[m.group(1)] = int(m.group(2), 16)
    return _flag_values


def src_index():
    """{string literal: [(file, line)]} for src/d and src/f_*: quoted names that look like event names."""
    global _src_index
    if _src_index is None:
        _src_index = defaultdict(list)
        root = os.path.join(DECOMP, "src")
        pat = re.compile(r'"([A-Za-z0-9_]{3,31})"')
        for dp, _dn, fns in os.walk(root):
            for fn in fns:
                if not fn.endswith((".cpp", ".inc", ".h")):
                    continue
                path = os.path.join(dp, fn)
                try:
                    lines = open(path, encoding="utf8", errors="replace").read().split("\n")
                except OSError:
                    continue
                rel = os.path.relpath(path, DECOMP).replace("\\", "/")
                for n, line in enumerate(lines, 1):
                    for m in pat.finditer(line):
                        _src_index[m.group(1)].append((rel, n))
    return _src_index


_file_lines = {}


def file_lines(rel):
    if rel not in _file_lines:
        _file_lines[rel] = open(os.path.join(DECOMP, rel), encoding="utf8", errors="replace").read().split("\n")
    return _file_lines[rel]


def event_bits_near(rel, line, span=30):
    """Event bits named within `span` lines of a reference: [(call, id)] with the decomp line."""
    ls = file_lines(rel)
    out = []
    for n in range(max(0, line - span - 1), min(len(ls), line + span)):
        for m in re.finditer(r"(is|on|off)EventBit\(\s*(?:dSv_event_flag_c::)?(\w+)", ls[n]):
            name = m.group(2)
            val = flag_values().get(name)
            if val is None and name.startswith("0x"):
                val = int(name, 16)
            if val is not None:
                out.append((m.group(1), val, n + 1))
    return out


def trigger_refs(name, limit=4):
    refs = [r for r in src_index().get(name, []) if r[0].startswith(("src/d/", "src/f_", "src/m_"))]
    return refs[:limit], len(refs)


# ---------------------------------------------------------------------------
# Stage data: EVNT table, TagEv actors, Lbnk (Demo<NN> archive)
# ---------------------------------------------------------------------------

def chunks(blob):
    n = struct.unpack_from(">I", blob, 0)[0]
    out = {}
    for i in range(n):
        tag, count, off = struct.unpack_from(">4sII", blob, 4 + i * 12)
        out.setdefault(tag.decode("latin1"), []).append((count, off))
    return out


def evnt_table(blob):
    """The stage's EVNT table (decomp dStage_Event_dt_c, whose name sits at +1: [number] -> (name, spawn switch))."""
    tab = []
    for count, off in chunks(blob).get("EVNT", []):
        for k in range(count):
            e = off + k * 0x18
            tab.append((cstr(blob, e + 1, 15), blob[e + 0x13]))
    return tab


def stag_table(blob):
    """The stage's save table number: dStage_stagInfo_GetSaveTbl = (mProp >> 1) & 0x7F (STAG +9)."""
    for count, off in chunks(blob).get("STAG", []):
        return (blob[off + 9] >> 1) & 0x7F
    return None


ACTOR_TAGS = ["ACTR", "SCOB"] + ["ACT%X" % i for i in range(12)] + ["SCO%X" % i for i in range(12)]


def tag_actors(blob):
    """TagEv / TagIsl / TagMd / TagHt / AND_SW2 actors: (name, param, pos, angz)."""
    ch = chunks(blob)
    out = []
    for tag, entries in ch.items():
        if tag[:3] not in ("ACT", "SCO") and tag not in ("ACTR", "SCOB"):
            continue
        size = 0x20 if tag.startswith("ACT") else 0x24
        for count, off in entries:
            for k in range(count):
                e = off + k * size
                name = cstr(blob, e, 8)
                if name in ("TagEv", "TagIsl", "TagMd", "TagHt", "TagHt2", "AND_SW2"):
                    param, x, y, z = struct.unpack_from(">Ifff", blob, e + 8)
                    ax, ay, az = struct.unpack_from(">hhh", blob, e + 0x18)
                    out.append((name, param, (x, y, z), az & 0xFFFF, tag))
    return out


def spawns(blob):
    """PLYR entries: (point = angle z low byte, start event number = param >> 24, mode = param >> 12 & 15, pos)."""
    out = []
    for count, off in chunks(blob).get("PLYR", []):
        for k in range(count):
            e = off + k * 0x20
            param, x, y, z = struct.unpack_from(">Ifff", blob, e + 8)
            az = struct.unpack_from(">h", blob, e + 0x1C)[0]
            out.append(dict(point=az & 0xFF, event=param >> 24, mode=(param >> 12) & 0xF, pos=(x, y, z),
                            room_param=param & 0xFF))
    return out


def lbnk(blob):
    ch = chunks(blob)
    for count, off in ch.get("LBNK", []):
        return list(blob[off:off + count])
    return []


class Inventory:
    def __init__(self, iso=ISO):
        self.disc = S.Disc(iso)
        self.stages = sorted({k.split("/")[2] for k in self.disc.entries if k.startswith("res/stage/")})
        self.cased = self._cased_names()
        self.demo_arcs = {}
        for k in sorted(self.disc.entries):
            if k.startswith("res/object/demo"):
                files = S.open_archive(self.disc, k)
                self.demo_arcs[os.path.basename(k)] = {n: b for n, b in files.items() if n.endswith(".stb")}
        self.stb_info = {}
        for arc, files in self.demo_arcs.items():
            for n, b in files.items():
                self.stb_info[(arc, os.path.basename(n))] = stb_summary(b)
        self.stage_data = {}
        self.event_stage_count = defaultdict(int)
        for st in self.stages:
            d = self.load_stage(st)
            self.stage_data[st] = d
            if d["events"]:
                for name in {e["name"] for e in d["events"].events}:
                    self.event_stage_count[name] += 1

    def _cased_names(self):
        """{lower-case stage directory: exact-case name}: the game compares stage names with strcmp."""
        disc = self.disc
        head = disc.f.seek(0) or disc.f.read(0x440)
        fst_off, fst_size = struct.unpack_from(">II", head, 0x424)
        disc.f.seek(fst_off)
        fst = disc.f.read(fst_size)
        count = struct.unpack_from(">I", fst, 8)[0]
        names = count * 12
        out = {}
        for i in range(1, count):
            flags_name, off, length = struct.unpack_from(">III", fst, i * 12)
            if flags_name >> 24:
                name_off = flags_name & 0xFFFFFF
                nm = fst[names + name_off:fst.index(b"\0", names + name_off)].decode("shift_jis")
                out.setdefault(nm.lower(), nm)
        return out

    def load_stage(self, st):
        arcs = {}
        for k in self.disc.entries:
            if k.startswith("res/stage/%s/" % st):
                arcs[os.path.basename(k)] = k
        d = dict(events=None, stage_stbs={}, evnt={}, tags=[], rooms=[], banks={}, spawns={})
        try:
            stage = S.open_archive(self.disc, arcs["stage.arc"])
        except Exception:
            return d
        if "dat/event_list.dat" in stage:
            d["events"] = EventList(stage["dat/event_list.dat"])
        for n, b in stage.items():
            if n.endswith(".stb"):
                d["stage_stbs"][os.path.basename(n)] = stb_summary(b)
        if "dzs/stage.dzs" in stage:
            d["evnt"]["stage"] = evnt_table(stage["dzs/stage.dzs"])
            d["save_tbl"] = stag_table(stage["dzs/stage.dzs"])
            d["tags"] += [("stage",) + t for t in tag_actors(stage["dzs/stage.dzs"])]
        for an in sorted(arcs):
            if not an.startswith("room"):
                continue
            try:
                room = S.open_archive(self.disc, arcs[an])
            except Exception:
                continue
            rn = int(re.sub(r"\D", "", an) or 0)
            d["rooms"].append(rn)
            if "dzr/room.dzr" in room:
                blob = room["dzr/room.dzr"]
                d["tags"] += [(rn,) + t for t in tag_actors(blob)]
                d["banks"][rn] = lbnk(blob)
                d["spawns"][rn] = spawns(blob)
        return d

    # -- helpers -----------------------------------------------------------
    def stb_for(self, st, filename):
        """(archive label, summary) for an STB file name used by a stage."""
        d = self.stage_data[st]
        if filename in d["stage_stbs"]:
            return "%s/Stage.arc" % st, d["stage_stbs"][filename]
        bank_arcs = []
        for rn, banks in d["banks"].items():
            for b in banks:
                if b != 0xFF:
                    bank_arcs.append("demo%02d.arc" % b)
        for arc in dict.fromkeys(bank_arcs):
            if (arc, filename) in self.stb_info:
                return "Demo%s" % arc[4:-4], self.stb_info[(arc, filename)]
        for (arc, fn), info in self.stb_info.items():
            if fn == filename:
                return "Demo%s (not in this stage's Lbnk)" % arc[4:-4], info
        return None, None

    def classify(self, st, ev, script):
        kinds = set(t for _n, t, _c in script)
        stb = None
        for sname, t, cuts in script:
            if t == "PACKAGE":
                for cname, data in cuts:
                    if isinstance(data.get("FileName"), str):
                        stb = (data["FileName"], data.get("EventFlag"), cname)
        if stb:
            return "stb", stb
        n = self.event_stage_count[ev["name"]]
        if n >= GENERIC_STAGES:
            return "generic", None
        if "CAMERA" in kinds or "NORMAL" in kinds or "MESSAGE" in kinds or len(script) > 2:
            return "event", None
        return "util", None

    def tag_triggers(self, st, ev):
        """What starts this event in the stage: spawn points whose start event is it (warp stage:room:point),
        and TagEv-style actors whose event number selects it (the stage's EVNT table maps number to name)."""
        d = self.stage_data[st]
        tab = d["evnt"].get("stage", [])
        out = []
        for rn, sps in sorted(d["spawns"].items()):
            for sp in sps:
                if sp["event"] < len(tab) and tab[sp["event"]][0] == ev["name"]:
                    out.append(("spawn", rn, sp["point"], sp["pos"], None, (sp["event"], tab[sp["event"]][1],
                                                                          d.get("save_tbl"), sp["mode"])))
        for where, name, param, pos, angz, ctag in d["tags"]:
            if name == "TagEv":
                no, flag = param >> 24, angz
            elif name in ("TagIsl", "TagMd", "TagHt", "TagHt2", "AND_SW2"):
                no, flag = param >> 24, None
            else:
                continue
            if no < len(tab) and tab[no][0] == ev["name"]:
                out.append((name, where, ctag, pos, flag if flag not in (0, 0xFFFF) else None, param))
        return out


def spawn_switch_text(sw, tbl):
    """Where the once-only spawn switch lives (dEvent_exception_c::setStartDemo, d_event_manager.cpp:23-50)."""
    if sw == 0xFF:
        return "no spawn switch (plays at every arrival)"
    if sw < 0x80 and tbl is not None:
        word = 0x803C4F88 + tbl * 0x24 + 4 + (sw >> 5) * 4
        byte = word + 3 - ((sw & 31) >> 3)
        return "spawn switch %d = stage-memory table %d byte 0x%08X mask 0x%02X (live copy 0x803C5380 + 0x%X)" % (
            sw, tbl, byte, 1 << (sw & 7), byte - (0x803C4F88 + tbl * 0x24))
    if sw < 0xC0:
        return "spawn switch %d = dungeon switch (dSv_danBit_c, cleared when the dungeon changes)" % sw
    return "spawn switch %d = zone switch (cleared when the room is created)" % sw


def fmt_bit(v):
    return "0x%04X (byte %d mask 0x%02X)" % (v, v >> 8, v & 0xFF)


def event_row(inv, st, ev, script, kind, stb):
    row = OrderedDict(stage=st, name=ev["name"], kind=kind)
    row["staffs"] = ["%s:%s(%d)" % (n, t, len(c)) for n, t, c in script]
    sounds = []
    for sname, t, cuts in script:
        if t == "SOUND":
            for cname, data in cuts:
                sounds.append("%s %s" % (cname, dict(data)))
    row["event_sounds"] = sounds
    msgs = []
    for sname, t, cuts in script:
        if t == "MESSAGE":
            for cname, data in cuts:
                if "MsgNo" in data or "msg" in str(data).lower():
                    msgs.append(str(dict(data)))
    row["messages"] = msgs[:6]
    row["stb"] = None
    if stb:
        fn, flag, cname = stb
        arc, info = inv.stb_for(st, fn)
        row["stb"] = dict(file=fn, arc=arc, event_flag=flag, info=info)
    refs, nrefs = trigger_refs(ev["name"])
    row["refs"] = refs
    row["nrefs"] = nrefs
    row["next"] = None
    for sname, t, cuts in script:
        if t == "PACKAGE":
            for cname, data in cuts:
                if isinstance(data.get("Stage"), str):
                    row["next"] = "%s start=%s room=%s layer=%s" % (data.get("Stage"), data.get("StartCode"),
                                                                   data.get("RoomNo"), data.get("Layer"))
    row["file_sounds"] = []
    for rel in dict.fromkeys(r[0] for r in refs):
        txt = " ".join(file_lines(rel))
        names = list(dict.fromkeys(re.findall(r"\b(JA_(?:SE|BGM|STRM)_\w+)", txt)))
        row["file_sounds"] += [(rel, n) for n in names[:10]]
    row["dev"] = bool(re.match(r"(k_test|i_test|itest|h_test|vrtest|test|e3roop|sirenmb$)", st))
    row["bits"] = []
    for rel, line in refs[:2]:
        for call, val, ln in event_bits_near(rel, line):
            row["bits"].append((rel, ln, call, val))
    row["tags"] = inv.tag_triggers(st, ev)
    row["layers"] = {}
    if row["stb"] and row["stb"]["arc"] and row["stb"]["arc"].startswith("Demo"):
        num = int(row["stb"]["arc"][4:6])
        for rn, banks in inv.stage_data[st]["banks"].items():
            ls = [i for i, b in enumerate(banks) if b == num]
            if ls:
                row["layers"][rn] = ls
    return row


def all_rows(inv, stages=None, only_event=None):
    rows, merged = [], {}
    seen_generic = set()
    for st in stages or inv.stages:
        d = inv.stage_data[st]
        el = d["events"]
        if el is None:
            continue
        for ev in el.events:
            if only_event and ev["name"] != only_event:
                continue
            script = el.event_script(ev)
            kind, stb = inv.classify(st, ev, script)
            if kind == "generic":
                if ev["name"] in seen_generic and not only_event:
                    continue
                seen_generic.add(ev["name"])
            row = event_row(inv, st, ev, script, kind, stb)
            row["stages"] = [st]
            key = (ev["name"], repr(script), repr(stb))
            if key in merged and not only_event:
                merged[key]["stages"].append(st)
                merged[key]["tags"] += [t for t in row["tags"] if t not in merged[key]["tags"]]
                merged[key]["bits"] += [t for t in row["bits"] if t not in merged[key]["bits"]]
                continue
            merged[key] = row
            rows.append(row)
    return rows


def spawn_plan(inv, kinds=("stb",)):
    """Every event that a spawn point starts: [{stage, room, point, layers, event, index, stb, frames, ...}].
    `layers` are the layers whose Lbnk holds the demo archive (use one for the warp), [-1] when none is needed."""
    out = []
    for st in inv.stages:
        d = inv.stage_data[st]
        el = d["events"]
        if el is None:
            continue
        for idx, ev in enumerate(el.events):
            script = el.event_script(ev)
            kind, stb = inv.classify(st, ev, script)
            if kind not in kinds:
                continue
            for t in inv.tag_triggers(st, ev):
                if t[0] != "spawn":
                    continue
                no, sw, tbl, mode = t[5]
                info = arc = None
                layers = [-1]
                if stb:
                    arc, info = inv.stb_for(st, stb[0])
                    if arc and arc.startswith("Demo"):
                        num = int(arc[4:6])
                        banks = d["banks"].get(t[1], [])
                        layers = [i for i, b in enumerate(banks) if b == num] or [-1]
                entry = dict(stage=inv.cased.get(st, st), room=t[1], point=t[2], layers=layers, event=ev["name"],
                             index=idx, stb=stb[0] if stb else None, arc=arc, mode=mode,
                             frames=info["frames"] if info else None,
                             cues=sum(1 for c in info["cues"] if c[3]) if info else None,
                             streams=[sound_name(c[1]) for c in info["cues"] if (c[1] >> 28) == 0xC] if info else [],
                             ends_into=None, switch=None, flag=stb[1] if stb else None)
                for sname, tt, cuts in script:
                    if tt == "PACKAGE":
                        for cname, data in cuts:
                            if isinstance(data.get("Stage"), str):
                                entry["ends_into"] = "%s:%s:%s:%s" % (data.get("Stage"), data.get("RoomNo"),
                                                                    data.get("StartCode"), data.get("Layer"))
                if sw != 0xFF and sw < 0x80 and tbl is not None:
                    base = 0x803C4F88 + tbl * 0x24
                    byte = base + 4 + (sw >> 5) * 4 + 3 - ((sw & 31) >> 3)
                    entry["switch"] = dict(no=sw, table=tbl, byte=byte, mask=1 << (sw & 7), live=0x803C5380 + byte - base)
                elif sw != 0xFF:
                    entry["switch"] = dict(no=sw, table=tbl, byte=None, mask=None, live=None)
                out.append(entry)
    return out


def print_text(rows, kinds=None):
    for r in rows:
        if kinds and r["kind"] not in kinds:
            continue
        print("%s / %s  [%s%s]" % (",".join(r["stages"][:6]) + ("+%d" % (len(r["stages"]) - 6) if len(r["stages"]) > 6 else ""),
                                  r["name"], r["kind"], ", dev stage" if r["dev"] else ""))
        print("   staffs: %s" % ", ".join(r["staffs"]))
        if r["stb"]:
            s = r["stb"]
            print("   STB: %s in %s, EventFlag=%s" % (s["file"], s["arc"], fmt_bit(s["event_flag"]) if isinstance(s["event_flag"], int) else None))
            if s["info"]:
                print("        %d frames (%.1f s at 30 fps), blocks %s" % (s["info"]["frames"], s["info"]["frames"] / 30.0,
                                                                         s["info"]["blocks"]))
                for t, sid, ident, started in s["info"]["cues"]:
                    print("        cue frame %5d  %s  (%s)%s" % (t, sound_name(sid), ident, "" if started else "  [id set, no fade-in start]"))
        if r["next"]:
            print("   ends into: %s" % r["next"])
        for e in r["event_sounds"]:
            print("   event sound action: %s" % e)
        for rel, ln in r["refs"]:
            print("   decomp: %s:%d" % (rel, ln))
        if r["file_sounds"]:
            print("   sounds named in the trigger file: %s" % ", ".join(n for _r, n in r["file_sounds"]))
        if r["nrefs"] > len(r["refs"]):
            print("   decomp: (+%d more references)" % (r["nrefs"] - len(r["refs"])))
        shown = set()
        for rel, ln, call, val in r["bits"]:
            if (call, val) in shown:
                continue
            shown.add((call, val))
            print("   bit: %sEventBit %s at %s:%d" % (call, fmt_bit(val), rel, ln))
        if r["layers"]:
            print("   layers (room Lbnk holds this demo's archive): %s" % ", ".join(
                "room %s layer %s" % (rn, "/".join(map(str, ls))) for rn, ls in sorted(r["layers"].items())))
        for t in r["tags"][:6]:
            if t[0] == "spawn":
                no, sw, tbl, mode = t[5]
                print("   spawn: warp <stage>:%s:%d (start event #%d, start mode %d, PLYR at (%.0f, %.0f, %.0f)); %s" % (
                    t[1], t[2], no, mode, t[3][0], t[3][1], t[3][2], spawn_switch_text(sw, tbl)))
                continue
            name, where, ctag, pos, flag, param = t
            g = ", needs event bit 0x%04X" % flag if flag else ""
            print("   placed: %s (%s) in %s at (%.0f, %.0f, %.0f), param 0x%08X%s" % (
                name, ctag, "stage" if where == "stage" else "room %s" % where, pos[0], pos[1], pos[2], param, g))


def print_markdown(rows, kinds=None):
    print("| Stage | Event | Kind | STB (frames, cues) | Trigger (decomp) | Gate (event bits) |")
    print("| --- | --- | --- | --- | --- | --- |")
    for r in rows:
        if kinds and r["kind"] not in kinds:
            continue
        stb = ""
        if r["stb"]:
            i = r["stb"]["info"]
            stb = "%s (%s)" % (r["stb"]["file"], "%d f, %d cues" % (i["frames"], sum(1 for c in i["cues"] if c[3])) if i else "?")
        trig = "; ".join("%s:%d" % x for x in r["refs"][:2])
        for t in r["tags"][:1]:
            trig += (" ; " if trig else "") + ("spawn %s:%d" % (t[1], t[2]) if t[0] == "spawn" else t[0])
        bits = ", ".join(sorted({"0x%04X" % v for _r, _l, _c, v in r["bits"]})[:4])
        tagflags = ", ".join("0x%04X" % t[4] for t in r["tags"] if t[4] and t[0] != "spawn")
        print("| %s | %s | %s | %s | %s | %s |" % (",".join(r["stages"][:3]), r["name"], r["kind"], stb, trig, ", ".join(x for x in (bits, tagflags) if x)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage", action="append")
    ap.add_argument("--event")
    ap.add_argument("--kind", action="append", help="stb, event, util, generic (default: stb event)")
    ap.add_argument("--plan", action="store_true", help="JSON of every spawn-started STB event (for cutscene_sweep.py)")
    ap.add_argument("--stb", action="store_true", help="list the STB files and their cues only")
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--iso", default=ISO)
    a = ap.parse_args()
    inv = Inventory(a.iso)
    if a.plan:
        import json
        print(json.dumps(spawn_plan(inv), indent=1))
        return 0
    if a.stb:
        for (arc, fn), info in sorted(inv.stb_info.items()):
            print("%s %s: %d frames, %s" % (arc, fn, info["frames"], info["blocks"]))
            print("    %d cues start a sound" % sum(1 for c in info["cues"] if c[3]))
            for t, sid, ident, started in sorted(info["cues"]):
                print("    frame %5d  %s (%s)%s" % (t, sound_name(sid), ident, "" if started else "  [id set only]"))
        return 0
    rows = all_rows(inv, a.stage, a.event)
    kinds = a.kind or (None if a.event else ["stb", "event"])
    (print_markdown if a.markdown else print_text)(rows, kinds)
    from collections import Counter
    c = Counter(r["kind"] for r in rows)
    print("\ntotals: %s" % dict(c), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
