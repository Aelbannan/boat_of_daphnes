// Crenando: see text_swap.h.
#include "text_swap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BUF_BYTES = 0xA000,      // the English file's 58 messages that name Ganon: about 10 KB, 14 KB substituted
    TEXT_LIMIT = 4096,       // the longest message read
    MAX_RECORDS = 512,
    SCAN_EVERY = 60,         // retraces between looks for message files
    FN_ARCHIVE_HEAP = 0x80011AB4u, // mDoExt_getArchiveHeap()
    FN_HEAP_ALLOC = 0x802B0434u,   // JKRHeap::alloc(size, alignment, heap)
    FN_MESSAGE_SET = 0x8002B8A4u,  // fopMsgM_messageSet(u32 message number)
};

typedef struct Record {
    u32 entry;  // the INF1 entry (guest address)
    u32 bmg;    // its message file
    u32 data;   // DAT1 + 8
    u32 orig;   // the entry's own text offset
    u32 mine;   // the offset to the substituted text in g_buf
    u16 id;
    bool seen;
} Record;

static volatile bool g_on;
static bool g_applied;           // the entries hold the substituted offsets
static u32 g_buf;                // guest address of the buffer, 0 = none
static u32 g_buf_used;
static u8 g_shadow[BUF_BYTES];   // what was written there: the check that the heap has not given it away
static Record g_rec[MAX_RECORDS];
static u32 g_rec_count;
static u32 g_frame;
static u32 g_fail_alloc;
static bool g_log_texts;
enum { TESTS = 8 };
static struct { u32 id, at; bool done; } g_tests[TESTS];
static u32 g_test_count;

void bluewake_text_swap_enable(bool on) { g_on = on; }
bool bluewake_text_swap_enabled(void) { return g_on; }

static bool guest_ptr(u32 p) { return p >= 0x80000000u && p < 0x81800000u; }

static u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u32 be16(const u8* p) { return (u32)p[0] << 8 | p[1]; }

// The substitution of one message (control codes copied byte for byte). Returns the new length (without the NUL), or
// (u32)-1 when nothing changes or it does not fit.
static u32 substitute(const u8* in, u32 n, u8* out, u32 cap) {
    u32 o = 0;
    bool changed = false;
    for (u32 i = 0; i < n;) {
        if (in[i] == 0x1Au) { // a control code: its own length, copied whole
            const u32 len = i + 1u < n ? in[i + 1u] : 0u;
            if (len < 2u || i + len > n || o + len >= cap)
                return (u32)-1;
            memcpy(out + o, in + i, len);
            o += len;
            i += len;
            continue;
        }
        u32 skip = 0;
        const char* put = "Crenando";
        if (n - i >= 9u && memcmp(in + i, "Ganondorf", 9) == 0) {
            skip = 9;
        } else if (n - i >= 9u && memcmp(in + i, "GANONDORF", 9) == 0) {
            skip = 9;
            put = "CRENANDO";
        } else if (n - i >= 5u && memcmp(in + i, "Ganon", 5) == 0) {
            skip = 5;
        } else if (n - i >= 5u && memcmp(in + i, "GANON", 5) == 0) {
            skip = 5;
            put = "CRENANDO";
        }
        if (skip != 0u) {
            if (o + 8u >= cap)
                return (u32)-1;
            memcpy(out + o, put, 8);
            o += 8u;
            i += skip;
            changed = true;
            continue;
        }
        if (o + 1u >= cap)
            return (u32)-1;
        out[o++] = in[i++];
    }
    if (!changed)
        return (u32)-1;
    out[o] = 0u;
    return o;
}

// Text length from the raw RAM (up to the lone 0 byte, control codes whole). -1 if it runs off.
static u32 text_length(const u8* ram, u32 ram_size, u32 offset) {
    u32 n = 0;
    while (offset + n < ram_size && n < TEXT_LIMIT) {
        const u8 c = ram[offset + n];
        if (c == 0u)
            return n;
        if (c == 0x1Au) {
            if (offset + n + 1u >= ram_size)
                break;
            const u32 len = ram[offset + n + 1u];
            if (len < 2u)
                break;
            n += len;
            continue;
        }
        ++n;
    }
    return (u32)-1;
}

static Record* find_record(u32 entry) {
    for (u32 i = 0; i < g_rec_count; ++i)
        if (g_rec[i].entry == entry)
            return &g_rec[i];
    return NULL;
}

static void log_text(const char* tag, u16 id, const u8* t, u32 n) {
    char line[1600];
    u32 o = 0;
    for (u32 i = 0; i < n && o + 12u < sizeof line; ++i) {
        if (t[i] == 0x1Au) {
            const u32 len = i + 1u < n ? t[i + 1u] : 2u;
            o += (u32)snprintf(line + o, sizeof line - o, "<%u>", len);
            i += (len < 2u ? 2u : len) - 1u;
        } else if (t[i] == '\n') {
            o += (u32)snprintf(line + o, sizeof line - o, "\\n");
        } else if (t[i] >= 0x20u && t[i] < 0x7Fu) {
            line[o++] = (char)t[i];
        } else {
            o += (u32)snprintf(line + o, sizeof line - o, "?");
        }
    }
    line[o] = 0;
    fprintf(stderr, "[text_swap] %s message 0x%04X: %s\n", tag, id, line);
}

// One message file: every entry whose text names the Ganons gets a substituted copy (or its record's again).
static void scan_file(CPUState* cpu, u32 bmg) {
    const u8* ram = cpu->ram;
    const u32 size = cpu->ram_size, base = bmg - 0x80000000u;
    if (base + 32u > size)
        return;
    const u32 blocks = be32(ram + base + 8u), sections = be32(ram + base + 12u);
    if (blocks < 2u || blocks > 0x00400000u / 32u || sections == 0u || sections > 16u)
        return;
    u32 inf = 0u, dat = 0u, at = bmg + 32u;
    for (u32 i = 0; i < sections; ++i) {
        const u32 a = at - 0x80000000u;
        if (a + 8u > size)
            return;
        const u32 magic = be32(ram + a), sz = be32(ram + a + 4u);
        if (sz < 8u || at + sz > bmg + blocks * 32u)
            return;
        if (magic == 0x494E4631u)
            inf = at;
        else if (magic == 0x44415431u)
            dat = at;
        at += sz;
    }
    if (inf == 0u || dat == 0u)
        return;
    const u32 entries = be16(ram + inf - 0x80000000u + 8u), esize = be16(ram + inf - 0x80000000u + 10u);
    if (esize < 6u || esize > 64u || entries == 0u || inf + 16u + entries * esize > bmg + blocks * 32u)
        return;
    const u32 data = dat + 8u;
    u32 patched = 0;
    static u8 out[TEXT_LIMIT + 64u];
    for (u32 i = 0; i < entries; ++i) {
        const u32 entry = inf + 16u + i * esize;
        const u32 off = be32(ram + entry - 0x80000000u);
        const u16 id = (u16)be16(ram + entry - 0x80000000u + 4u);
        Record* r = find_record(entry);
        if (r != NULL && r->bmg == bmg) {
            if (off == r->mine) { // still ours
                r->seen = true;
                continue;
            }
            if (off == r->orig && g_buf != 0u) { // put back (while it was off, or by a reload): ours again
                mem_write32(cpu, entry, r->mine);
                r->seen = true;
                ++patched;
                continue;
            }
        }
        if (r != NULL) { // another file's, or another offset now: a new look
            *r = g_rec[--g_rec_count];
            r = NULL;
        }
        const u32 text = data + off; // wraps for texts the Bunny Hood keeps in its own buffer
        if (!guest_ptr(text))
            continue;
        const u32 toff = text - 0x80000000u;
        const u32 len = text_length(ram, size, toff);
        if (len == (u32)-1 || len < 5u)
            continue;
        const u32 nlen = substitute(ram + toff, len, out, sizeof out);
        if (nlen == (u32)-1)
            continue;
        if (g_buf == 0u || g_rec_count >= MAX_RECORDS || g_buf_used + nlen + 1u > BUF_BYTES) {
            static int reported;
            if (reported++ < 3)
                fprintf(stderr, "[text_swap] no room for message 0x%04X (%u records, %u bytes used)\n", id, g_rec_count,
                        g_buf_used);
            continue;
        }
        const u32 at_buf = g_buf + g_buf_used;
        memcpy(g_shadow + g_buf_used, out, nlen + 1u);
        for (u32 k = 0; k <= nlen; ++k)
            mem_write8(cpu, at_buf + k, out[k]);
        g_buf_used += (nlen + 4u) & ~3u;
        Record rec = {.entry = entry, .bmg = bmg, .data = data, .orig = off, .mine = at_buf - data, .id = id, .seen = true};
        g_rec[g_rec_count++] = rec;
        mem_write32(cpu, entry, rec.mine);
        ++patched;
        if (g_log_texts)
            log_text("patched", id, out, nlen);
    }
    if (patched != 0u)
        fprintf(stderr, "[text_swap] message file at 0x%08X: %u messages say Crenando (%u records, %u bytes)\n", bmg,
                patched, g_rec_count, g_buf_used);
}

static bool file_ok(CPUState* cpu, u32 bmg) {
    return guest_ptr(bmg) && mem_read32(cpu, bmg) == 0x4D455347u && mem_read32(cpu, bmg + 4u) == 0x626D6731u;
}

// Every entry we changed gets its own offset back.
static void restore_all(CPUState* cpu) {
    u32 restored = 0;
    for (u32 i = 0; i < g_rec_count; ++i) {
        const Record* r = &g_rec[i];
        if (file_ok(cpu, r->bmg) && mem_read32(cpu, r->entry) == r->mine) {
            mem_write32(cpu, r->entry, r->orig);
            ++restored;
        }
    }
    g_applied = false;
    if (restored != 0u)
        fprintf(stderr, "[text_swap] off: %u messages have their own text again\n", restored);
}

// The buffer still holds what we put there (the heap has not given it away).
static bool buffer_intact(CPUState* cpu) {
    if (g_buf == 0u || g_buf_used == 0u)
        return true;
    const u32 off = g_buf - 0x80000000u;
    return off + g_buf_used <= cpu->ram_size && memcmp(cpu->ram + off, g_shadow, g_buf_used) == 0;
}

// BLUEWAKE_TEXT_SWAP_LOG: what message 0x1165 reads through its entry right now (the proof that on and off apply at once).
static void dump_live(CPUState* cpu, const char* when) {
    if (!g_log_texts)
        return;
    for (u32 i = 0; i < g_rec_count; ++i) {
        const Record* r = &g_rec[i];
        if (r->id != 0x1165u || !file_ok(cpu, r->bmg))
            continue;
        u8 t[256];
        const u32 at = r->data + mem_read32(cpu, r->entry);
        u32 n = guest_ptr(at) ? text_length(cpu->ram, cpu->ram_size, at - 0x80000000u) : 0u;
        if (n == (u32)-1 || n >= sizeof t)
            n = 0;
        memcpy(t, cpu->ram + (at - 0x80000000u), n);
        char tag[64];
        snprintf(tag, sizeof tag, "at retrace %u, %s: reads", g_frame, when);
        log_text(tag, r->id, t, n);
    }
}

static void scan_all(CPUState* cpu) {
    for (u32 i = 0; i < g_rec_count; ++i)
        g_rec[i].seen = false;
    const u8* ram = cpu->ram;
    for (u32 offset = 0; offset + 8u <= cpu->ram_size;) {
        const u8* hit = memmem(ram + offset, cpu->ram_size - offset, "MESGbmg1", 8);
        if (hit == NULL)
            break;
        scan_file(cpu, 0x80000000u + (u32)(hit - ram));
        offset = (u32)(hit - ram) + 8u;
    }
    for (u32 i = g_rec_count; i-- > 0;) // files that went away
        if (!g_rec[i].seen)
            g_rec[i] = g_rec[--g_rec_count];
    if (g_rec_count == 0u)
        g_buf_used = 0u;
    g_applied = true;
}

void bluewake_text_swap_retrace(CPUState* cpu) {
    if (cpu == NULL || cpu->ram == NULL)
        return;
    ++g_frame;
    static bool env_read;
    if (!env_read) {
        env_read = true;
        g_log_texts = getenv("BLUEWAKE_TEXT_SWAP_LOG") != NULL;
        // BLUEWAKE_TEST_MESSAGE=id@retrace: the game shows message `id` (a box, as an NPC's) at that retrace.
        // (a comma separated list shows several)
        for (const char* v = getenv("BLUEWAKE_TEST_MESSAGE"); v != NULL && *v != 0 && g_test_count < TESTS;) {
            char* end = NULL;
            g_tests[g_test_count].id = (u32)strtoul(v, &end, 0);
            g_tests[g_test_count].at = end != NULL && *end == '@' ? (u32)strtoul(end + 1, &end, 0) : 0u;
            ++g_test_count;
            v = end != NULL && *end == ',' ? end + 1 : "";
        }
    }
    if (!g_on) {
        if (g_applied) {
            restore_all(cpu);
            dump_live(cpu, "off");
        }
        return;
    }
    if (g_buf != 0u && !buffer_intact(cpu)) {
        fprintf(stderr, "[text_swap] the buffer at 0x%08X was taken back by the heap: starting over\n", g_buf);
        restore_all(cpu);
        g_buf = 0u;
        g_buf_used = 0u;
        g_rec_count = 0u;
    }
    if (g_buf == 0u)
        return; // the fiber gets it
    // A look at once when switched on, or when one of our entries is not ours any more (a file loaded over), else now and then.
    bool look = !g_applied || g_frame % SCAN_EVERY == 0u;
    if (!look && g_rec_count != 0u) {
        const Record* r = &g_rec[g_frame % g_rec_count];
        look = !file_ok(cpu, r->bmg) || mem_read32(cpu, r->entry) != r->mine;
    }
    if (look) {
        const bool first = !g_applied;
        scan_all(cpu);
        if (first)
            dump_live(cpu, "on");
    }
}

bool bluewake_text_swap_wants_fiber(void) {
    if (g_on && g_buf == 0u && g_fail_alloc < 4u)
        return true;
    for (u32 i = 0; i < g_test_count; ++i)
        if (!g_tests[i].done && g_frame >= g_tests[i].at)
            return true;
    return false;
}

void bluewake_text_swap_fiber(GuestCoro* co, CPUState* cpu) {
    if (g_on && g_buf == 0u && g_fail_alloc < 4u) {
        const u32 heap = bluewake_guest_call(co, FN_ARCHIVE_HEAP, 0, 0, 0, 0, 0, 0);
        const u32 buf = bluewake_guest_call(co, FN_HEAP_ALLOC, BUF_BYTES, 32u, heap, 0, 0, 0);
        fprintf(stderr, "[text_swap] buffer: archive heap %08X, %u bytes at %08X\n", heap, BUF_BYTES, buf);
        if (guest_ptr(buf) && guest_ptr(buf + BUF_BYTES - 1u)) {
            g_buf = buf;
            g_buf_used = 0u;
            for (u32 i = 0; i < BUF_BYTES; i += 4u)
                mem_write32(cpu, buf + i, 0u);
            memset(g_shadow, 0, sizeof g_shadow);
        } else {
            ++g_fail_alloc;
        }
    }
    for (u32 i = 0; i < g_test_count; ++i) {
        if (g_tests[i].done || g_frame < g_tests[i].at || mem_read32(cpu, 0x803CA754u) == 0u) // wants a scene with Link
            continue;
        g_tests[i].done = true;
        const u32 pid = bluewake_guest_call(co, FN_MESSAGE_SET, g_tests[i].id, 0, 0, 0, 0, 0);
        fprintf(stderr, "[test] message 0x%X shown at retrace %u: process id %08X (Crenando %s)\n", g_tests[i].id, g_frame,
                pid, g_on ? "on" : "off");
        break;
    }
}
