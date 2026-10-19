#!/usr/bin/env python3
"""Builds runtime/host/src/player_gender_rules.inc: the explicit list of English message ids and phrases that
refer to the player, with their feminine wording (Player Gender = Female, runtime/host/src/text_swap.c).

The rules below are the only source: ids and phrase fragments, no game text. The script reads the disc's own
message file (the USA disc, local_paths.json BOD_DISC) to check that every phrase is found in its message, to expand
the capitalisation forms that message really uses, and to warn about lines that grow past the text box.

  python scripts/windows/gen_player_gender_rules.py [--disc ISO] [--check-arc bmgres.arc ...]

--check-arc lists the rows that no longer match in another message file (a randomizer seed's).
"""
import argparse, json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'runtime', 'host', 'src', 'player_gender_rules.inc')


def ids(s):
    return [int(x, 16) for x in s.split()]


# (message ids, find, replacement). `find` is matched whole-word and without regard to case; the replacement takes the
# case of what it replaced (Sir -> Ma'am, SIR -> MA'AM). ids is None for "every message that has it" (only for phrases
# that always address the player).
R = []


def rule(idlist, find, repl):
    R.append((idlist, find, repl))


# --- Aryll: "Big Brother" ---------------------------------------------------------------------------------------------
ARYLL = ids('0BDB 0BB9 0BBE 0BC0 0BCA 0BCE 0BCF 0BD5 0BD9 0C29 1131 1140 1145 1148')
rule(ARYLL, 'big brother', 'big sister')
rule(ids('0C2A'), 'brother', 'sister')
rule(ids('113F'), 'your brother', 'your sister')
rule(ids('113F'), "he'll be back", "she'll be back")
rule(ids('113F'), 'until he returns', 'until she returns')
rule(ids('1B12'), 'brother', 'sister')          # a pirate to the player

# --- titles: Mr. / sir / mister / young master ------------------------------------------------------------------------
for t in ('Mr. Fairy', 'Mr. Customer', 'Mr. Sailor', 'Mr. Knight', 'Mr. Smarty-Pants'):
    rule(None, t, 'Ms.' + t[3:])
rule(None, 'Mr. Swordsman', 'Ms. Swordswoman')
rule(None, 'young sir', 'young lady')
rule(None, 'good sir', 'good madam')
rule(None, 'kind sir', 'kind madam')
rule(None, 'fine sir', 'fine madam')
rule(None, 'sir', "ma'am")                       # every "sir" in the file addresses the player
rule(ids('26C3 358F'), 'mister', 'miss')
YOUNG_MASTER = ids('2845 284A 284D 2855 2859 285D 288E 288F 2A4E')   # Zunari and Lenzo (not the Rito's "young master Komali")
rule(YOUNG_MASTER, 'young master', 'young miss')
rule(ids('2713 2718 271C 2720 2C8C 2C8D 2C9B 2C9D 3397 1EF6'), 'young man', 'young lady')
rule(ids('2713'), 'young gentleman', 'young lady')
rule(ids('2FB4 2FB5 2FB8 2FBC'), 'little man', 'little lady')
rule(ids('2883'), 'my dear fellow', 'my dear lady')
rule(ids('18C5'), 'enough fellow', 'enough person')
rule(ids('09DC'), 'little fellow', 'little lass')

# --- boy / lad / laddie / guy ----------------------------------------------------------------------------------------
BOY = ids('0800 09C9 09D2 0C27 1162 1166 1B59 1B5C 1C21 1C23 1C24 1C29 1C33 1C92 1C9A 1CA5 1D42 1D45 1E25 271B '
          '2779 277C 2781 28A2 29CF 2A33 2AA5 2C25 2CEE 2DC7 2DC8 2EE1 3158 33A4 3523 352B')
rule(BOY, 'boy', 'girl')
rule(ids('2722'), 'boys', 'girls')
rule(ids('09CB 09CF 09D3 09DB 1781 1A5A'), 'lad', 'lass')
rule(None, 'laddie', 'lassie')
rule(ids('22C7 22C9 22CB 22CC 22CD 22D4 22D5 22D7'), ', guy', ', gal')
rule(ids('1996'), 'adventure guy', 'adventure gal')
rule(ids('199B'), 'adventure-guy', 'adventure-gal')
rule(ids('199D'), 'good guy', 'good gal')
SWORD = ids('098F 0991 099D 099E 099F 13EF 1406 140E 140F 1412 1416 1417 1419 1423 1424 1431 1433 1451 1454 1482 1485 '
            '148A 1492 1495 1497 149C 14C1')
rule(SWORD, 'swordsman', 'swordswoman')

# --- pronouns for the player, one message at a time -------------------------------------------------------------------
P = [
    ('0627', 'behind him', 'behind her'),
    ('0628', 'him to lift you', 'her to lift you'),
    ('0628', 'carry him', 'carry her'),
    ('0C28', 'about him', 'about her'),
    ('1781', 'to him and', 'to her and'),
    ('1AFF', 'knew he was', 'knew she was'),
    ('1B2A', 'could he do', 'could she do'),
    ('1B2B', 'to him,', 'to her,'),
    ('1D23', "he's frozen", "she's frozen"),
    ('1D29', 'could he be', 'could she be'),
    ('1D2F', 'he handles', 'she handles'),
    ('1D2F', 'his money', 'her money'),
    ('1D3F', 'does he get', 'does she get'),
    ('1D43', 'did he say', 'did she say'),
    ('1D44', 'get him for', 'get her for'),
    ('1D45', "he's just", "she's just"),
    ('1D48', 'is he serious', 'is she serious'),
    ('339E', 'he thinks', 'she thinks'),
    ('339F', "he's got", "she's got"),
    ('3145', "thinks he's", "thinks she's"),
    ('3784', 'dedicated his life', 'dedicated her life'),
    ('3819', 'increasing his powers', 'increasing her powers'),
    ('381A', 'ice to his arrows', 'ice to her arrows'),
    ('381D', 'on his mysterious boat', 'on her mysterious boat'),
    ('381D', 'he seems to', 'she seems to'),
    ('381D', 'completed his collection', 'completed her collection'),
    ('381D', 'he is now', 'she is now'),
    ('3862', 'he must look', 'she must look'),
    ('3862', 'aid him in', 'aid her in'),
]
for i, f, r in P:
    rule(ids(i), f, r)

# --------------------------------------------------------------------------------------------------------------------


def yaz0(d):
    size = struct.unpack('>I', d[4:8])[0]
    out = bytearray()
    i = 16
    while len(out) < size:
        code = d[i]
        i += 1
        for b in range(8):
            if len(out) >= size:
                break
            if code & (0x80 >> b):
                out.append(d[i])
                i += 1
            else:
                b1, b2 = d[i], d[i + 1]
                i += 2
                dist = ((b1 & 0xF) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0:
                    n = d[i] + 0x12
                    i += 1
                else:
                    n += 2
                for _ in range(n):
                    out.append(out[-dist])
    return bytes(out)


def rarc_file(d, want):
    if d[:4] == b'Yaz0':
        d = yaz0(d)
    assert d[:4] == b'RARC'
    dataoff = struct.unpack('>I', d[12:16])[0] + 0x20
    dirn, diro, filn, filo, strsz, stro = struct.unpack('>IIIIII', d[0x20:0x38])

    def s(o):
        e = d.index(b'\0', 0x20 + stro + o)
        return d[0x20 + stro + o:e].decode('latin1')
    for i in range(filn):
        o = 0x20 + filo + i * 20
        idx, h, flags, no, doff, dsz = struct.unpack('>HHHHII', d[o:o + 16])
        if s(no) == want and not flags & 0x200:
            return d[doff + dataoff:doff + dataoff + dsz]
    raise SystemExit('no ' + want)


def iso_file(iso, path):
    f = open(iso, 'rb')
    f.seek(0x424)
    fo, fs = struct.unpack('>II', f.read(8))
    f.seek(fo)
    fst = f.read(fs)
    n = struct.unpack('>I', fst[8:12])[0]
    strs = fst[n * 12:]

    def name(i):
        o = struct.unpack('>I', fst[i * 12:i * 12 + 4])[0] & 0xFFFFFF
        return strs[o:strs.index(b'\0', o)].decode()
    stack = []
    ends = []
    for i in range(1, n):
        while ends and i >= ends[-1]:
            ends.pop()
            stack.pop()
        flags = fst[i * 12]
        _, a, b = struct.unpack('>III', fst[i * 12:i * 12 + 12])
        if flags:
            stack.append(name(i))
            ends.append(b)
        elif '/'.join(stack + [name(i)]).lower() == path.lower():
            f.seek(a)
            return f.read(b)
    raise SystemExit('not on the disc: ' + path)


def messages(bmg):
    ns = struct.unpack('>I', bmg[12:16])[0]
    at = 32
    inf = dat = None
    for _ in range(ns):
        m = bmg[at:at + 4]
        sz = struct.unpack('>I', bmg[at + 4:at + 8])[0]
        if m == b'INF1':
            inf = at
        if m == b'DAT1':
            dat = at
        at += sz
    n, es = struct.unpack('>HH', bmg[inf + 8:inf + 12])
    out = {}
    for i in range(n):
        e = inf + 16 + i * es
        off = struct.unpack('>I', bmg[e:e + 4])[0]
        mid = struct.unpack('>H', bmg[e + 4:e + 6])[0]
        p = dat + 8 + off
        q = p
        while bmg[q] != 0:
            q += bmg[q + 1] if bmg[q] == 0x1a else 1
        out[mid] = bmg[p:q]
    return out


def visible(t):
    """The text with every control code turned into a NUL, so a phrase can never span one."""
    o = bytearray()
    i = 0
    while i < len(t):
        if t[i] == 0x1a:
            o.append(0)
            i += t[i + 1]
        else:
            o.append(t[i])
            i += 1
    return bytes(o).decode('latin1')


def adapt(src, repl):
    if len(src) > 1 and src.upper() == src and any(c.isalpha() for c in src):
        return repl.upper()
    words = src.split(' ')
    if len(words) > 1 and all(w[:1].isupper() for w in words):
        return ' '.join(w[:1].upper() + w[1:] for w in repl.split(' '))
    if src[0].isupper():
        return repl[0].upper() + repl[1:]
    return repl


def pattern(find):
    head = r"(?<![A-Za-z0-9'])" if find[0].isalnum() else ''
    tail = r"(?![A-Za-z0-9])" if find[-1].isalnum() else ''
    return re.compile(head + re.escape(find) + tail, re.I)


def expand(msgs, strict=True):
    """-> {id: [(find, repl)]}, longest find first."""
    rows = {}
    for idl, find, repl in R:
        targets = idl if idl is not None else sorted(msgs)
        pat = pattern(find)
        for mid in targets:
            if mid not in msgs:
                continue
            text = visible(msgs[mid])
            seen = set()
            for m in pat.finditer(text):
                if m.group(0) in seen:
                    continue
                seen.add(m.group(0))
                rows.setdefault(mid, []).append((m.group(0), adapt(m.group(0), repl)))
            if not seen and idl is not None and strict:
                print('NO MATCH: %04X %r' % (mid, find), file=sys.stderr)
    for mid in rows:
        d = {}
        for f, r in rows[mid]:
            d.setdefault(f, r)
        ordered = sorted(d.items(), key=lambda fr: (-len(fr[0]), fr[0]))
        # keep the rows that really fire, applied the way the game's code does (left to right, first row that fits)
        text = visible(msgs[mid])
        used = set()
        i = 0
        while i < len(text):
            for f, r in ordered:
                if text.startswith(f, i) and pattern(f).match(text, i):
                    used.add(f)
                    i += len(f) - 1
                    break
            i += 1
        rows[mid] = [fr for fr in ordered if fr[0] in used]
    return rows


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--disc')
    ap.add_argument('--check-arc', nargs='*', default=[])
    a = ap.parse_args()
    disc = a.disc
    if not disc:
        lp = os.path.join(HERE, '..', '..', 'local_paths.json')
        disc = json.load(open(lp))['BOD_DISC']
    van = messages(rarc_file(iso_file(disc, 'res/Msg/bmgres.arc'), 'zel_00.bmg'))
    rows = expand(van)
    lines = []
    grew = 0
    for mid in sorted(rows):
        for f, r in rows[mid]:
            lines.append('    {0x%04X, %s, %s},' % (mid, cstr(f), cstr(r)))
    longest = max(len(l) for t in van.values() for l in visible(t).split('\n'))
    for mid in sorted(rows):
        t = visible(van[mid])
        for f, r in rows[mid]:
            t = pattern(f).sub(lambda m, r=r: r, t)
        for l in t.split('\n'):
            if len(l) > longest:
                print('LONG LINE %04X (%d): %s' % (mid, len(l), l), file=sys.stderr)
                grew += 1
    with open(OUT, 'w', newline='\n') as f:
        f.write('// Generated by scripts/windows/gen_player_gender_rules.py; do not edit by hand.\n')
        f.write('// {message id, phrase, feminine phrase}: the phrases of English messages that refer to the player\n')
        f.write('// (Player Gender = Female). Whole-word, any capitalisation; ids ascending, longest phrase first per id.\n')
        f.write('\n'.join(lines) + '\n')
    print('%d rows over %d messages -> %s (longest vanilla line %d chars, %d lines longer)' %
          (len(lines), len(rows), os.path.normpath(OUT), longest, grew))
    for arc in a.check_arc:
        other = messages(rarc_file(open(arc, 'rb').read(), 'zel_00.bmg'))
        miss = same = 0
        for mid in rows:
            if mid not in other:
                miss += 1
                continue
            if visible(other[mid]) == visible(van[mid]):
                same += 1
                continue
            re_rows = expand({mid: other[mid]}, strict=False).get(mid, [])
            if sorted(re_rows) != sorted(rows[mid]):
                print('%04X differs from the disc and its rows change: %s' % (mid, visible(other[mid])[:200].replace('\n', '|')))
        print('%s: %d messages unchanged, %d absent' % (arc, same, miss))


if __name__ == '__main__':
    main()
