// Test control: see test_control.h.
#include "test_control.h"

#include "chart_reveal.h"
#include "game_state.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kMaxEntries = 128 };

typedef struct TestEntry {
    u64 retrace;
    u32 address;
    u32 value;
    u8 size;
    bool hold;  // every retrace from `retrace` on (pokes only)
    bool deref; // `address` holds a pointer, and the value read or written is at *address + offset
    bool or_in; // pokes: "|=" sets the value's bits and keeps the others
    u32 offset;
} TestEntry;

static TestEntry g_pokes[kMaxEntries], g_peeks[kMaxEntries];
static unsigned g_poke_count, g_peek_count;

// BLUEWAKE_TEST_TELEPORT=retrace:stage:room:x:y:z:angle[,...]: Link to a place (game_state.c's teleport).
typedef struct TestTeleport {
    u64 retrace;
    char stage[8];
    int room, angle;
    float pos[3];
} TestTeleport;
static TestTeleport g_teleports[16];
static unsigned g_teleport_count;

static void parse_teleports(void) {
    const char* list = getenv("BLUEWAKE_TEST_TELEPORT");
    for (const char* at = list; at != NULL && *at != '\0' && g_teleport_count < 16u;) {
        TestTeleport t;
        memset(&t, 0, sizeof t);
        unsigned long long retrace = 0;
        char stage[16] = {0};
        if (sscanf(at, "%llu:%15[^:]:%d:%f:%f:%f:%i", &retrace, stage, &t.room, &t.pos[0], &t.pos[1], &t.pos[2],
                   &t.angle) >= 6) {
            t.retrace = retrace;
            memcpy(t.stage, stage, strlen(stage) < 8 ? strlen(stage) : 7);
            g_teleports[g_teleport_count++] = t;
        } else {
            fprintf(stderr, "[test] BLUEWAKE_TEST_TELEPORT: ignored entry at '%s'\n", at);
        }
        at = strchr(at, ',');
        if (at != NULL)
            ++at;
    }
}

// BLUEWAKE_TEST_CHART_REVEAL=retrace:0|1[,...]: Reveal Sea Chart switched on or off at a retrace (the setting's live switch).
typedef struct TestReveal {
    u64 retrace;
    bool on;
} TestReveal;
static TestReveal g_reveals[16];
static unsigned g_reveal_count;

static void parse_reveals(void) {
    const char* list = getenv("BLUEWAKE_TEST_CHART_REVEAL");
    for (const char* at = list; at != NULL && *at != '\0' && g_reveal_count < 16u;) {
        unsigned long long retrace = 0;
        int on = 0;
        if (sscanf(at, "%llu:%d", &retrace, &on) == 2) {
            g_reveals[g_reveal_count].retrace = retrace;
            g_reveals[g_reveal_count++].on = on != 0;
        }
        at = strchr(at, ',');
        if (at != NULL)
            ++at;
    }
}

static bool guest_address(u32 address, u8 size) {
    return address >= 0x80000000u && address + size <= 0x81800000u && (address % size) == 0u;
}

// "retrace[+]:address[=value][/size]", one entry of either list. Either may be "retrace:*pointer+offset...": the
// pointer is read first (at the retrace), then the value at pointer + offset (Link's position: *0x803CA754+0x1F8/4).
// A poke's "|=value" sets those bits and keeps the rest (Link's no-draw flag: *0x803CA754+0x29C|=0x08000000/4).
static bool parse_entry(const char* text, size_t length, bool poke, TestEntry* out) {
    char item[96];
    if (length == 0u || length >= sizeof item)
        return false;
    memcpy(item, text, length);
    item[length] = '\0';
    memset(out, 0, sizeof *out);
    out->size = 1u;
    char* at = item;
    char* end = NULL;
    out->retrace = strtoull(at, &end, 0);
    if (end == at)
        return false;
    at = end;
    if (*at == '+') {
        out->hold = poke;
        ++at;
    }
    if (*at++ != ':')
        return false;
    if (*at == '*') {
        out->deref = true;
        ++at;
    }
    out->address = (u32)strtoul(at, &end, 0);
    if (end == at)
        return false;
    at = end;
    if (out->deref) {
        if (*at == '+') {
            out->offset = (u32)strtoul(at + 1, &end, 0);
            at = end;
        }
        if (!guest_address(out->address, 4u))
            return false;
    }
    if (poke) {
        if (*at == '|') {
            out->or_in = true;
            ++at;
        }
        if (*at++ != '=')
            return false;
        out->value = (u32)strtoul(at, &end, 0);
        if (end == at)
            return false;
        at = end;
    }
    if (*at == '/') {
        out->size = (u8)strtoul(at + 1, &end, 0);
        at = end;
    }
    return *at == '\0' && (out->size == 1u || out->size == 2u || out->size == 4u) &&
           (out->deref || guest_address(out->address, out->size));
}

static unsigned parse_list(const char* name, bool poke, TestEntry* entries) {
    const char* list = getenv(name);
    unsigned count = 0;
    for (const char* at = list; at != NULL && *at != '\0' && count < kMaxEntries;) {
        const char* end = strchr(at, ',');
        const size_t length = end != NULL ? (size_t)(end - at) : strlen(at);
        if (parse_entry(at, length, poke, &entries[count]))
            count++;
        else
            fprintf(stderr, "[test] %s: ignored entry '%.*s'\n", name, (int)length, at);
        at = end != NULL ? end + 1 : NULL;
    }
    if (count != 0u)
        fprintf(stderr, "[test] %s: %u entries\n", name, count);
    return count;
}

// BLUEWAKE_TEST_RESIZE=retrace:WxH[,...] (testing only): the window is resized at that retrace.
extern void VISetWindowSize(unsigned width, unsigned height);
static struct { u64 retrace; unsigned w, h; } g_resizes[8];
static unsigned g_resize_count;

static void parse_resizes(void) {
    for (const char* at = getenv("BLUEWAKE_TEST_RESIZE"); at != NULL && *at != '\0' && g_resize_count < 8u;) {
        unsigned long long r = 0;
        unsigned w = 0, h = 0, used = 0;
        if (sscanf(at, "%llu:%ux%u%n", &r, &w, &h, &used) == 3) {
            g_resizes[g_resize_count].retrace = r;
            g_resizes[g_resize_count].w = w;
            g_resizes[g_resize_count].h = h;
            ++g_resize_count;
            at += used;
        }
        const char* comma = strchr(at, ',');
        at = comma != NULL ? comma + 1 : NULL;
    }
}

void bluewake_test_control_attach(void) {
    parse_resizes();
    g_poke_count = parse_list("BLUEWAKE_TEST_POKE", true, g_pokes);
    g_peek_count = parse_list("BLUEWAKE_TEST_PEEK", false, g_peeks);
    parse_teleports();
    parse_reveals();
}

static u32 read_sized(CPUState* cpu, u32 address, u8 size) {
    return size == 4u ? mem_read32(cpu, address) : size == 2u ? mem_read16(cpu, address) : mem_read8(cpu, address);
}

void bluewake_test_control_retrace(CPUState* cpu, u64 retrace) {
    if (cpu == NULL || cpu->ram == NULL)
        return;
    for (unsigned i = 0; i < g_poke_count; ++i) {
        const TestEntry* e = &g_pokes[i];
        if (retrace == e->retrace || (e->hold && retrace > e->retrace)) {
            u32 address = e->address;
            if (e->deref) {
                address = mem_read32(cpu, e->address) + e->offset;
                if (!guest_address(address, e->size))
                    continue;
            }
            const u32 value = e->or_in ? read_sized(cpu, address, e->size) | e->value : e->value;
            if (e->size == 4u)
                mem_write32(cpu, address, value);
            else if (e->size == 2u)
                mem_write16(cpu, address, (u16)value);
            else
                mem_write8(cpu, address, (u8)value);
            if (retrace == e->retrace)
                fprintf(stderr, "[test] poke retrace=%llu address=0x%08X size=%u value=0x%X%s\n",
                        (unsigned long long)retrace, address, e->size, value, e->hold ? " (held)" : "");
        }
    }
    for (unsigned i = 0; i < g_resize_count; ++i)
        if (retrace == g_resizes[i].retrace) {
            fprintf(stderr, "[test] resize retrace=%llu %ux%u\n", (unsigned long long)retrace, g_resizes[i].w, g_resizes[i].h);
            VISetWindowSize(g_resizes[i].w, g_resizes[i].h);
        }
    for (unsigned i = 0; i < g_teleport_count; ++i)
        if (retrace == g_teleports[i].retrace) {
            fprintf(stderr, "[test] teleport retrace=%llu %s room %d\n", (unsigned long long)retrace, g_teleports[i].stage,
                    g_teleports[i].room);
            bluewake_request_teleport(g_teleports[i].stage, g_teleports[i].room, g_teleports[i].pos, g_teleports[i].angle);
        }
    for (unsigned i = 0; i < g_reveal_count; ++i)
        if (retrace == g_reveals[i].retrace) {
            fprintf(stderr, "[test] chart reveal retrace=%llu %s\n", (unsigned long long)retrace, g_reveals[i].on ? "on" : "off");
            bluewake_chart_reveal_enable(g_reveals[i].on);
        }
    for (unsigned i = 0; i < g_peek_count; ++i) {
        const TestEntry* e = &g_peeks[i];
        if (retrace != e->retrace)
            continue;
        u32 address = e->address;
        if (e->deref) {
            const u32 pointer = mem_read32(cpu, e->address);
            address = pointer + e->offset;
            if (!guest_address(address, e->size)) {
                fprintf(stderr, "[test] peek retrace=%llu address=0x%08X size=%u value=unreadable (pointer 0x%08X)\n",
                        (unsigned long long)retrace, address, e->size, pointer);
                continue;
            }
        }
        const u32 value = read_sized(cpu, address, e->size);
        float as_float = 0.0f;
        memcpy(&as_float, &value, sizeof as_float);
        if (e->size == 4u && e->deref)
            fprintf(stderr, "[test] peek retrace=%llu address=0x%08X size=%u value=0x%X float=%g\n",
                    (unsigned long long)retrace, address, e->size, value, (double)as_float);
        else
            fprintf(stderr, "[test] peek retrace=%llu address=0x%08X size=%u value=0x%X\n",
                    (unsigned long long)retrace, address, e->size, value);
    }
}
