#!/usr/bin/env python3
"""Add mods to a generated composite as variant chunks and data writes.

A mod is a patched copy of the game: a Gecko code applied to main.dol
(gecko_apply.py) or a patcher's output (Better Wind Waker). Its code cannot be
patched at runtime in a statically recompiled game, so the patched main.dol and
RELs are translated with the same translator and settings as the base and run
through generate_composite.py into a composite source tree of their own. This
script compares that tree with the base tree and adds to the base:

  * variant chunks: every chunk whose translation differs, under a new name.
    The host calls bluewake_composite_apply_mods() at boot, which points the
    chunk table at the variants of the enabled mods. Chunks never call each
    other directly (every cross-chunk edge returns to the dispatcher), so a
    variant replaces its chunk cleanly.
  * extra chunks: code ranges only the mod has (Better Wind Waker's custom code
    section), found through bluewake_mod_extra_find() when enabled.
  * one-time writes: the mod's DOL bytes that differ from the base DOL image
    (data sections and new sections) and its relocated REL data that differs,
    written into guest RAM at boot.
  * per-frame writes: a Gecko code's data writes, re-applied at every retrace
    as Dolphin does.
  * a module layout (--layout NAME), for a mod that changes the shape of REL
    data or adds modules (the randomizer: grown RELs move their BSS, the chest
    actor's module has other sections, two modules are new): the mod's own
    tables of REL modules, REL data images and prologs, which the host selects
    before it stages anything (bluewake_composite_select_layout). The layout
    carries the data, so the mod's REL data needs no boot writes.
  * overlays: an extra chunk whose code range covers a base chunk's (the
    randomizer's chest actor is another build of the same module) replaces that
    base chunk by a trampoline that looks the pc up among the mod's extras, so
    the extras win over the base chunk with the mod on while the base tree's
    dispatcher, and with it the verified composite, is unchanged.

  * switchable mods (--switchable NAME): a mod the host turns on and off while the game runs (the randomizer, at the
    file select). The chunks it changes are not written into the chunk table by apply_mods but reached through a
    trampoline per chunk (switch_tramp_N), a function whose body calls s_mod_switch_target[N]: the chunk's own code
    with the mod off, the mod's variant with it on. The dispatcher's pc cache holds the trampoline, never a variant, so
    bluewake_composite_set_mods(mask) can change the mask at any time without anything cached going stale.

usage: build_mod_variants.py --composite-src BASE_TREE --base-dol BASE.dol
           --mod NAME:MOD_TREE:MOD.dol[:GECKO_RUNTIME_JSON] [--mod ...]
           [--combo NAME+NAME:COMBO_TREE ...] [--layout NAME ...]

Mods are numbered in the order given (bit 0, 1, ...). When two mods change the
same chunk, each keeps its own variant and a --combo tree (the game patched by
both, translated) supplies the variant used when both are enabled. A variant
applies when all the mods it requires are enabled; the most specific one wins.
"""
import argparse
import json
import re
import shutil
import sys
from pathlib import Path

FUNC_RE = re.compile(r"\bfunc_([0-9A-F]{8})\b")
CHUNK_FN_RE = re.compile(r"^void func_([0-9A-F]{8})\(CPUState\* ctx\) \{", re.M)


def dol_image(path):
    """address -> bytes of every loadable section."""
    d = Path(path).read_bytes()
    be = lambda o: int.from_bytes(d[o:o + 4], "big")
    out = {}
    for n in range(18):
        off, addr, size = be(n * 4), be(0x48 + n * 4), be(0x90 + n * 4)
        if size:
            out[addr] = d[off:off + size]
    return out


def dol_writes(base_dol, mod_dol):
    base = dol_image(base_dol)
    def base_byte(a):
        for start, data in base.items():
            if start <= a < start + len(data):
                return data[a - start]
        return None
    writes = []
    for start, data in sorted(dol_image(mod_dol).items()):
        run_start, run = None, bytearray()
        for i, b in enumerate(data):
            if base_byte(start + i) != b:
                if run_start is None:
                    run_start = start + i
                run.append(b)
            elif run_start is not None:
                writes.append((run_start, bytes(run)))
                run_start, run = None, bytearray()
        if run_start is not None:
            writes.append((run_start, bytes(run)))
    return writes


REL_DATA_RE = re.compile(r"static const u8 (s_rel_data_\d+_\d+)\[\] = \{(.*?)\};", re.S)
REL_ENTRY_RE = re.compile(r"\{(\d+)u, (\d+)u, 0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, (\w+)\},")


def rel_data(tree):
    text = (Path(tree) / "rel_data.inc").read_text()
    arrays = {name: bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})u", body))
              for name, body in REL_DATA_RE.findall(text)}
    out = {}
    for mid, si, linked, size, name in REL_ENTRY_RE.findall(text):
        out[(int(mid), int(si))] = (int(linked, 16), arrays.get(name))
    return out


def rel_writes(base_tree, mod_tree):
    base, mod = rel_data(base_tree), rel_data(mod_tree)
    writes = []
    for key, (linked, data) in sorted(mod.items()):
        if data is None:
            continue
        b = base.get(key)
        if b is None or b[0] != linked or b[1] is None or len(b[1]) != len(data):
            sys.exit("REL data section %s changed shape; not supported" % (key,))
        old = b[1]
        i = 0
        while i < len(data):
            if data[i] == old[i]:
                i += 1
                continue
            j = i
            while j < len(data) and data[j] != old[j]:
                j += 1
            writes.append((linked + i, data[i:j]))
            i = j
    return writes


SEC_ARRAY_RE = re.compile(r"static const StaticRecompRelSection s_rel_sec_(\d+)\[\] = \{(.*?)\};", re.S)
SEC_RE = re.compile(r"\{(\d+)u, (\d+)u, 0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u\},")
MODULE_RE = re.compile(r"\{(\d+)u, (\d+)u, (\d+)u, 0x([0-9A-F]{4})u, (\d+)u, s_rel_sec_(\d+), (\d+)u\},")
LIFECYCLE_RE = re.compile(r"\{(\d+)u, 0x([0-9A-F]{8})u\},")


def rel_layout(tree):
    """A tree's module tables as data: `modules` (id, version, section_count, section_info_offset, file_size) in
    table order, `sections` (module -> [(index, linked_start, size)]), `data` ((module, section) -> (linked_start,
    size, bytes or None for BSS)) with its table `order`, and the `lifecycle` prolog addresses."""
    tree = Path(tree)
    text = (tree / "rel_modules.inc").read_text()
    sections = {int(mid): [(int(i), int(a, 16), int(n, 16)) for _, i, a, n in SEC_RE.findall(body)]
                for mid, body in SEC_ARRAY_RE.findall(text)}
    modules = [(int(m[0]), int(m[1]), int(m[2]), int(m[3], 16), int(m[4]))
               for m in MODULE_RE.findall(text.split("s_rel_modules[] = {")[1])]
    dtext = (tree / "rel_data.inc").read_text()
    arrays = {name: bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})u", body))
              for name, body in REL_DATA_RE.findall(dtext)}
    data, order = {}, []
    for mid, si, linked, size, name in REL_ENTRY_RE.findall(dtext):
        key = (int(mid), int(si))
        data[key] = (int(linked, 16), int(size, 16), arrays.get(name))
        order.append(key)
    lifecycle = [(int(m), int(a, 16)) for m, a in LIFECYCLE_RE.findall(dtext.split("s_rel_lifecycle[] = {")[1])]
    return {"modules": modules, "sections": sections, "data": data, "order": order, "lifecycle": lifecycle}


def appended_code_images(base, mod, rels_dir):
    """Data images for the executable sections a mod appends to a module (the randomizer's section 7: free-space
    code that also holds data tables, which the game reads through the section's linked address and which a seed
    writes into). The base has no such section, so the layout adds the file's bytes as the section's image, as a
    data section's. Returns {(module, section): (linked_start, size, bytes)}."""
    files = {}
    for path in Path(rels_dir).glob("*.rel"):
        data = path.read_bytes()
        files[int.from_bytes(data[:4], "big")] = data
    out = {}
    for mid, sections in mod["sections"].items():
        old = {i: (start, size) for i, start, size in base["sections"].get(mid, [])}
        data = files.get(mid)
        if data is None:
            continue
        count, table = int.from_bytes(data[12:16], "big"), int.from_bytes(data[16:20], "big")
        for index, start, size in sections:
            if not size or not start or old.get(index, (0, 0))[1] or index >= count:
                continue
            raw = int.from_bytes(data[table + 8 * index:table + 8 * index + 4], "big")
            if not raw & 1 or mid not in base["sections"]:
                continue  # data sections have images already; a module the base lacks keeps code out of memory
            offset = raw & ~1
            out[(mid, index)] = (start, size, data[offset:offset + size])
    return out


def layout_lines(name, base, mod):
    """The C tables of mod `name`'s module layout. Whatever equals the base's (a section table, a data image) is
    referenced by the base's own name, so only what the mod changes costs bytes."""
    out = []
    changed = {mid for mid, secs in mod["sections"].items() if base["sections"].get(mid) != secs}
    for mid in sorted(changed):
        out.append("static const StaticRecompRelSection s_mod_rel_sec_%s_%d[] = {" % (name, mid))
        out += ["    {%du, %du, 0x%08Xu, 0x%08Xu}," % (mid, i, a, n) for i, a, n in mod["sections"][mid]]
        out.append("};")
    out.append("static const StaticRecompRelModule s_mod_rel_modules_%s[] = {" % name)
    for mid, version, count, sio, fsize in mod["modules"]:
        secs = ("s_mod_rel_sec_%s_%d" % (name, mid)) if mid in changed else ("s_rel_sec_%d" % mid)
        out.append("    {%du, %du, %du, 0x%04Xu, %du, %s, %du}," % (
            mid, version, count, sio, fsize, secs, len(mod["sections"][mid])))
    out.append("};")
    refs = {}
    for key in mod["order"]:
        data = mod["data"][key][2]
        old = base["data"].get(key)
        if data is None:
            refs[key] = "NULL"
        elif old is not None and old[2] == data:
            refs[key] = "s_rel_data_%d_%d" % key
        else:
            refs[key] = "s_mod_rel_data_%s_%d_%d" % (name, key[0], key[1])
            out.append("static const u8 %s[] = {%s};" % (refs[key], ", ".join("0x%02Xu" % b for b in data)))
    out.append("static const BlueWakeRelData s_mod_rel_data_%s[] = {" % name)
    out += ["    {%du, %du, 0x%08Xu, 0x%08Xu, %s}," % (k[0], k[1], mod["data"][k][0], mod["data"][k][1], refs[k])
            for k in mod["order"]]
    out.append("};")
    out.append("static const BlueWakeRelLifecycle s_mod_rel_lifecycle_%s[] = {" % name)
    out += ["    {%du, 0x%08Xu}," % entry for entry in mod["lifecycle"]]
    out.append("};")
    return out


def overlaid_chunks(base_ranges, extras):
    """The base chunks (start -> end) that these extra chunks overlap. The extras must cover each one completely:
    the overlay trampoline has no base code to fall back on."""
    spans = sorted((start, end) for _, start, end, _ in extras)
    hit = {}
    for start, end in sorted(base_ranges.items()):
        pieces = [(a, b) for a, b in spans if a < end and start < b]
        if not pieces:
            continue
        at = start
        for a, b in pieces:
            if a > at:
                break
            at = max(at, b)
        if at < end:
            sys.exit("an extra chunk overlaps base chunk %08X-%08X without covering it" % (start, end))
        hit[start] = end
    return hit


def chunk_ranges(tree):
    text = (Path(tree) / "generated_composite.h").read_text()
    starts = [int(v, 16) for v in re.findall(
        r"0x([0-9A-F]{8})u,", text.split("static const u32 s_dolrecomp_chunk_starts[] = {")[1].split("};")[0])]
    ends = [int(v, 16) for v in re.findall(
        r"0x([0-9A-F]{8})u,", text.split("static const u32 s_ends[] = {")[1].split("};")[0])]
    return dict(zip(starts, ends))


# Game options (game_options.py): the switches the option sites read, the host's
# native code for the hook sites, and the option table. module_export.c
# includes this file after defining RECOMP_MODULE_EXPORT and BlueWakeModWriteFn.
OPTION_GLUE = r"""
/* The switches the option sites read, set by the host before the first
 * dispatch; and the host's native code for the hook sites, which returns 0 to
 * go on or the guest address to continue at. */
volatile unsigned char dolrecomp_option_flags[256];
typedef u32 (*BlueWakeNativeHookFn)(CPUState* ctx, u32 id);
static BlueWakeNativeHookFn s_native_hook;
u32 dolrecomp_native_hook(CPUState* ctx, u32 id);
u32 dolrecomp_native_hook(CPUState* ctx, u32 id)
{
    return s_native_hook != NULL ? s_native_hook(ctx, id) : 0u;
}
typedef struct BlueWakeOption { u32 index; u32 default_on; const char* name; const char* title; } BlueWakeOption;
typedef struct BlueWakeOptionWrite { u32 option; u32 address; u32 size; const u8* bytes; } BlueWakeOptionWrite;"""

OPTION_EXPORTS = r"""
RECOMP_MODULE_EXPORT volatile unsigned char* bluewake_composite_option_flags(void);
RECOMP_MODULE_EXPORT volatile unsigned char* bluewake_composite_option_flags(void)
{
    return dolrecomp_option_flags;
}
RECOMP_MODULE_EXPORT void bluewake_composite_set_native_hook(BlueWakeNativeHookFn fn);
RECOMP_MODULE_EXPORT void bluewake_composite_set_native_hook(BlueWakeNativeHookFn fn)
{
    s_native_hook = fn;
}
RECOMP_MODULE_EXPORT u32 bluewake_composite_option_count(void);
RECOMP_MODULE_EXPORT u32 bluewake_composite_option_count(void)
{
    return MODULE_OPTION_COUNT;
}
/* The option at a position in the table: its switch, whether it is on by
 * default, its name (the setting's key) and its title (the menu's). */
RECOMP_MODULE_EXPORT const char* bluewake_composite_option(u32 position, u32* index, u32* default_on,
                                                           const char** title);
RECOMP_MODULE_EXPORT const char* bluewake_composite_option(u32 position, u32* index, u32* default_on,
                                                           const char** title)
{
    if (position >= MODULE_OPTION_COUNT)
        return NULL;
    if (index) *index = s_options[position].index;
    if (default_on) *default_on = s_options[position].default_on;
    if (title) *title = s_options[position].title;
    return s_options[position].name;
}
/* The values the switched-on options write, at boot. */
RECOMP_MODULE_EXPORT u32 bluewake_composite_option_writes(BlueWakeModWriteFn fn, void* user);
RECOMP_MODULE_EXPORT u32 bluewake_composite_option_writes(BlueWakeModWriteFn fn, void* user)
{
    u32 count = 0;
    for (u32 i = 0; i < MODULE_OPTION_WRITE_COUNT; ++i) {
        const BlueWakeOptionWrite* w = &s_option_writes[i];
        if (!dolrecomp_option_flags[w->option])
            continue;
        if (fn)
            fn(user, w->address, w->bytes, w->size);
        count++;
    }
    return count;
}"""


# Module layouts: the tables the host stages RELs from, one set per layout mod. The base's stay in force unless a layout
# mod is on; module_export.c's bluewake_composite_select_layout points the module at the chosen mod's tables, and the
# host calls it before it reads the first REL table.
LAYOUT_TYPES = """typedef struct BlueWakeModLayout {
    u32 mod;
    const StaticRecompRelModule* modules;
    u32 num_modules;
    const BlueWakeRelData* data;
    u32 num_data;
    const BlueWakeRelLifecycle* lifecycle;
    u32 num_lifecycle;
} BlueWakeModLayout;"""

# An extra chunk's code range covers a base chunk's: with the mod on, the base chunk's slot runs this, which finds
# the pc among the mod's extras (a chunk returns on a pc it does not hold, as this does).
OVERLAY_FN = """static void bluewake_mod_overlay(CPUState* ctx)
{
    DolRecompFunction fn = bluewake_mod_extra_find(ctx->pc);
    if (fn)
        fn(ctx);
}"""


def switch_lines(names, switchable, chunks):
    """The trampolines of the switchable mods' chunks: one per chunk start any of their variants (alone or in a
    combination) replaces. Without a switchable mod the module has none (MODULE_MOD_SWITCH_COUNT 0)."""
    bits = 0
    for name in switchable:
        if name not in names:
            sys.exit("--switchable %s is not one of the mods" % name)
        bits |= 1 << names.index(name)
    starts = sorted({c[1] for c in chunks if c[0] & bits})
    lines = ["#define MODULE_MOD_SWITCH_MASK 0x%Xu" % bits, "#define MODULE_MOD_SWITCH_COUNT %du" % len(starts)]
    if not starts:
        return lines
    lines.append("static DolRecompFunction s_mod_switch_target[MODULE_MOD_SWITCH_COUNT];")
    for i in range(len(starts)):
        lines.append("static void bluewake_mod_switch_tramp_%d(CPUState* ctx) { s_mod_switch_target[%d](ctx); }" % (i, i))
    lines.append("static const u32 s_mod_switch_starts[MODULE_MOD_SWITCH_COUNT] = {%s};" % ", ".join("0x%08Xu" % a for a in starts))
    lines.append("static const DolRecompFunction s_mod_switch_tramps[MODULE_MOD_SWITCH_COUNT] = {%s};" % ", ".join(
        "bluewake_mod_switch_tramp_%d" % i for i in range(len(starts))))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--composite-src", required=True)
    ap.add_argument("--base-dol", required=True)
    ap.add_argument("--mod", action="append", default=[])
    ap.add_argument("--combo", action="append", default=[])
    # Mods that are never enabled together (two widescreen aspects): a chunk
    # only they share needs no combined variant. The host keeps one of them.
    ap.add_argument("--exclusive", action="append", default=[], metavar="NAME,NAME")
    # Game options (game_options.py): the option table and the values the
    # options write, resolved against the base tree's REL data.
    ap.add_argument("--switchable", action="append", default=[], metavar="NAME",
                    help="the named mod can be turned on and off while the game runs: its chunks are reached through "
                         "trampolines (bluewake_composite_set_mods)")
    ap.add_argument("--layout", action="append", default=[], metavar="NAME",
                    help="the named mod's REL module layout replaces the base's while it is on")
    ap.add_argument("--layout-rels", action="append", default=[], metavar="NAME=DIR",
                    help="the REL files the layout mod's tree was translated from: the executable sections it "
                         "appends to a module get a data image (their tables are read and written by the game)")
    ap.add_argument("--options", metavar="SPEC")
    ap.add_argument("--rels-bin-dir", metavar="DIR")
    args = ap.parse_args()
    exclusive = [set(spec.split(",")) for spec in args.exclusive]
    out = Path(args.composite_src)
    base_ranges = chunk_ranges(out)
    owner = {}
    names, chunks, extras, writes, decls = [], [], [], [], []
    layouts, overlays, base_layout = [], 0, None
    for bit, spec in enumerate(args.mod):
        parts = spec.split(":")
        name, tree, mod_dol = parts[0], Path(parts[1]), parts[2]
        gecko = parts[3] if len(parts) > 3 else None
        names.append(name)
        dest = out / ("chunks_mod_" + name)
        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir()
        mod_ranges = chunk_ranges(tree)
        n_var = n_extra = 0
        for chunk_dir in sorted(tree.glob("chunks_*")):
            if chunk_dir.name.startswith("chunks_mod_"):
                continue
            for variant in sorted(chunk_dir.glob("*.c")):
                original = out / chunk_dir.name / variant.name
                text = variant.read_text()
                if original.exists() and original.read_text() == text:
                    continue
                m = CHUNK_FN_RE.search(text)
                if not m:
                    sys.exit("no chunk function in " + str(variant))
                start = int(m.group(1), 16)
                owner.setdefault(start, []).append(name)
                suffix = "__mod_" + name
                target = dest / (chunk_dir.name + "__" + variant.name)
                target.write_text(FUNC_RE.sub(lambda g: g.group(0) + suffix, text), newline=chr(10))
                fn = "func_%08X%s" % (start, suffix)
                decls.append("void %s(CPUState* ctx);" % fn)
                if start in base_ranges:
                    if base_ranges[start] != mod_ranges.get(start):
                        sys.exit("chunk %08X changed its range" % start)
                    chunks.append((1 << bit, start, fn))
                    n_var += 1
                else:
                    extras.append((bit, start, mod_ranges[start], fn))
                    n_extra += 1
        for start in overlaid_chunks(base_ranges, [e for e in extras if e[0] == bit]):
            if set(owner.get(start, [])) - {name}:
                sys.exit("chunk %08X is overlaid by %s and also changed by %s" % (start, name, owner[start]))
            owner.setdefault(start, []).append(name)
            chunks.append((1 << bit, start, "bluewake_mod_overlay"))
            overlays += 1
        if name in args.layout:
            # The layout carries the mod's REL data, whatever its shape: no boot writes for it.
            if base_layout is None:
                base_layout = rel_layout(out)
            mod_layout = rel_layout(tree)
            for spec_ in args.layout_rels:
                if spec_.split("=", 1)[0] == name:
                    extra = appended_code_images(base_layout, mod_layout, spec_.split("=", 1)[1])
                    for key, image in sorted(extra.items()):
                        mod_layout["data"][key] = image
                        mod_layout["order"].append(key)
                    print("%s: %d appended executable sections carry a data image" % (name, len(extra)))
            layouts.append((bit, name, layout_lines(name, base_layout, mod_layout)))
            once = dol_writes(args.base_dol, mod_dol)
        else:
            once = dol_writes(args.base_dol, mod_dol) + rel_writes(out, tree)
        for address, data in once:
            writes.append((bit, 0, address, data))
        frame = []
        if gecko:
            frame = [(a, bytes.fromhex(h)) for a, h in json.load(open(gecko))["data"]]
            for address, data in frame:
                writes.append((bit, 1, address, data))
        print("%s: %d variant chunks, %d extra chunks, %d boot writes (%d bytes), %d per-frame writes%s" % (
            name, n_var, n_extra, len(once), sum(len(d) for _, d in once), len(frame),
            ", module layout" if name in args.layout else ""))
    for spec in args.combo:
        combo, tree = spec.split(":")
        members = combo.split("+")
        mask = 0
        for member in members:
            mask |= 1 << names.index(member)
        tree = Path(tree)
        dest = out / ("chunks_mod_" + "_".join(members))
        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir()
        shared = {start for start, who in owner.items() if len(set(who) & set(members)) > 1}
        found = set()
        for chunk_dir in sorted(tree.glob("chunks_*")):
            for variant in sorted(chunk_dir.glob("*.c")):
                text = variant.read_text()
                m = CHUNK_FN_RE.search(text)
                if not m or int(m.group(1), 16) not in shared:
                    continue
                start = int(m.group(1), 16)
                suffix = "__mod_" + "_".join(members)
                (dest / (chunk_dir.name + "__" + variant.name)).write_text(
                    FUNC_RE.sub(lambda g: g.group(0) + suffix, text), newline=chr(10))
                fn = "func_%08X%s" % (start, suffix)
                decls.append("void %s(CPUState* ctx);" % fn)
                chunks.append((mask, start, fn))
                found.add(start)
        if found != shared:
            sys.exit("combo %s is missing chunks %s" % (combo, sorted(shared - found)))
        print("%s: %d combined chunks" % (combo, len(found)))
    def pair_resolved(start, a, b):
        if any({a, b} <= group for group in exclusive):
            return True
        pair = (1 << names.index(a)) | (1 << names.index(b))
        return any(c[1] == start and c[0] & pair == pair for c in chunks)
    unresolved = [s_ for s_, who in owner.items() if not all(
        pair_resolved(s_, a, b) for i, a in enumerate(who) for b in who[i + 1:])]
    if unresolved:
        sys.exit("chunks changed by several mods need a --combo: %s" % ["%08X" % u for u in unresolved])
    # Most specific last: apply_mods lets a later matching variant win.
    chunks.sort(key=lambda c: bin(c[0]).count("1"))
    lines = ["// Generated by scripts/mods/build_mod_variants.py -- do not edit.",
             "#define MODULE_MOD_COUNT %du" % len(names),
             "static const char* const s_mod_names[] = {%s};" % ", ".join('"%s"' % n for n in names)]
    lines += decls
    if overlays:
        lines.append(OVERLAY_FN)
    lines.append("static const BlueWakeModChunk s_mod_chunks[] = {")
    lines += ["    {0x%Xu, 0x%08Xu, %s}," % c for c in chunks]
    lines.append("};")
    lines.append("#define MODULE_MOD_CHUNK_COUNT %du" % len(chunks))
    lines += switch_lines(names, args.switchable, chunks)
    lines.append("static const BlueWakeModExtraChunk s_mod_extra_chunks[] = {")
    lines += ["    {%du, 0x%08Xu, 0x%08Xu, %s}," % e for e in extras]
    lines.append("};")
    lines.append("#define MODULE_MOD_EXTRA_CHUNK_COUNT %du" % len(extras))
    for i, (_, _, _, data) in enumerate(writes):
        lines.append("static const u8 s_mod_write_%d[] = {%s};" % (i, ", ".join("0x%02Xu" % b for b in data)))
    lines.append("static const BlueWakeModWrite s_mod_writes[] = {")
    lines += ["    {%du, %du, 0x%08Xu, %du, s_mod_write_%d}," % (bit, frame, a, len(d), i)
              for i, (bit, frame, a, d) in enumerate(writes)]
    lines.append("};")
    lines.append("#define MODULE_MOD_WRITE_COUNT %du" % len(writes))
    if layouts:
        lines.append(LAYOUT_TYPES)
        for _, _, layout in layouts:
            lines += layout
        lines.append("static const BlueWakeModLayout s_mod_layouts[] = {")
        for bit, name, _ in layouts:
            lines.append("    {%du, s_mod_rel_modules_%s, (u32)(sizeof s_mod_rel_modules_%s / sizeof s_mod_rel_modules_%s[0]),"
                         " s_mod_rel_data_%s, (u32)(sizeof s_mod_rel_data_%s / sizeof s_mod_rel_data_%s[0]),"
                         " s_mod_rel_lifecycle_%s, (u32)(sizeof s_mod_rel_lifecycle_%s / sizeof s_mod_rel_lifecycle_%s[0])}," % (
                             (bit,) + (name,) * 9))
        lines.append("};")
        lines.append("#define MODULE_MOD_LAYOUT_COUNT %du" % len(layouts))
    if args.options:
        import game_options
        spec = game_options.Spec(args.options)
        linked = {key: value[0] for key, value in rel_data(out).items()}
        option_writes = game_options.resolve_writes(spec, args.rels_bin_dir, linked)
        lines.append(OPTION_GLUE)
        lines.append("#define MODULE_OPTION_COUNT %du" % len(spec.options))
        lines.append("static const BlueWakeOption s_options[] = {")
        lines += ['    {%du, %du, "%s", "%s"},' % (i, on, n, t.replace('"', '\\"'))
                  for i, n, on, t in spec.options]
        lines.append("};")
        for i, (_, _, data) in enumerate(option_writes):
            lines.append("static const u8 s_option_write_%d[] = {%s};" % (
                i, ", ".join("0x%02Xu" % b for b in data)))
        lines.append("static const BlueWakeOptionWrite s_option_writes[] = {")
        lines += ["    {%du, 0x%08Xu, %du, s_option_write_%d}," % (o, a, len(d), i)
                  for i, (o, a, d) in enumerate(option_writes)]
        lines.append("};")
        lines.append("#define MODULE_OPTION_WRITE_COUNT %du" % len(option_writes))
        lines.append(OPTION_EXPORTS)
        print("options: %d, %d writes" % (len(spec.options), len(option_writes)))
    (out / "mod_variants.inc").write_text("\n".join(lines) + "\n", newline=chr(10))


if __name__ == "__main__":
    main()
