#include "hd_look.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool bluewake_hd_distant_islands;
static bool g_islands_were_on;
static bool g_trace;

static int setting(const char* key) {  // 1, 0, or -1 when unset
    const char* value = getenv(key);
    if (value == NULL || value[0] == '\0')
        return -1;
    return value[0] == '1';
}

void bluewake_hd_look_reload(void) {
    const int all = setting("BLUEWAKE_HD_LOOK");
    const int islands = setting("BLUEWAKE_HD_DISTANT_ISLANDS");
    bluewake_hd_distant_islands = islands >= 0 ? islands == 1 : all == 1;
    g_trace = setting("BLUEWAKE_HD_TRACE") == 1;
    fprintf(stderr, "[hd-look] distant islands %s\n", bluewake_hd_distant_islands ? "on" : "off");
}

// GZLE01 revision 0. Every actor's create tag is on g_fopAcTg_Queue
// (node_list_class: head first); a tag is node_class (next at +0x08) with its
// actor at +0x0C. An actor's process name is at +0x08 and its scale at +0x214.
enum {
    kActorQueue = 0x80372028u,
    kNodeNext = 0x08u,
    kTagActor = 0x0Cu,
    kProcName = 0x08u,
    kScaleX = 0x214u,
    kScaleZ = 0x21Cu,
    kActorHeap = 0xF0u,
    kLodBg = 0x01C3u,  // fpcNm_LODBG_e, d_a_lod_bg
    kMaxActors = 4096u,
};

// d_a_lod_bg's constructor makes scale.x the distance within which the island's
// far model is shown (its stage scale times 20,000) and scale.z, 20,000 more,
// the distance past which it is unloaded; it reads both every frame. HD shows
// the models 100,000 units further out. A changed actor keeps 20,001 between
// the two, so it is changed once and can be changed back, also in a save state.
static const float kExtra = 100000.0f;
static const float kGap = 20000.0f;
static const float kChangedGap = 20001.0f;

static bool guest_pointer(u32 address) { return address >= 0x80000000u && address < 0x81800000u; }

static float read_float(CPUState* cpu, u32 address) {
    const u32 bits = mem_read32(cpu, address);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static void write_float(CPUState* cpu, u32 address, float value) {
    u32 bits;
    memcpy(&bits, &value, sizeof bits);
    mem_write32(cpu, address, bits);
}

static void islands(CPUState* cpu, bool on, unsigned long long retrace) {
    unsigned count = 0, changed = 0, loaded = 0;
    u32 node = mem_read32(cpu, kActorQueue);
    for (u32 n = 0; n < kMaxActors && guest_pointer(node); ++n, node = mem_read32(cpu, node + kNodeNext)) {
        const u32 actor = mem_read32(cpu, node + kTagActor);
        if (!guest_pointer(actor) || mem_read16(cpu, actor + kProcName) != kLodBg)
            continue;
        count++;
        loaded += mem_read32(cpu, actor + kActorHeap) != 0u;
        const float x = read_float(cpu, actor + kScaleX);
        const float gap = read_float(cpu, actor + kScaleZ) - x;
        if (on && gap == kGap) {
            write_float(cpu, actor + kScaleX, x + kExtra);
            write_float(cpu, actor + kScaleZ, x + kExtra + kChangedGap);
            changed++;
            if (g_trace)
                fprintf(stderr, "[hd-look] island model 0x%08X (room %u): shown within %.0f, now %.0f\n", actor,
                        mem_read32(cpu, actor + 0xB0u), x, x + kExtra);
        } else if (!on && gap == kChangedGap) {
            write_float(cpu, actor + kScaleX, x - kExtra);
            write_float(cpu, actor + kScaleZ, x - kExtra + kGap);
            changed++;
        }
    }
    static unsigned last_count = ~0u, last_loaded = ~0u;
    if (g_trace && (count != last_count || loaded != last_loaded || changed)) {
        fprintf(stderr, "[hd-look] retrace=%llu island models: %u, loaded %u, changed now %u\n", retrace, count,
                loaded, changed);
        last_count = count;
        last_loaded = loaded;
    }
}

void bluewake_hd_look_retrace(CPUState* cpu) {
    static unsigned long long retrace;
    retrace++;
    if (cpu == NULL || cpu->ram == NULL)
        return;
    if (bluewake_hd_distant_islands || g_islands_were_on)
        islands(cpu, bluewake_hd_distant_islands, retrace);
    g_islands_were_on = bluewake_hd_distant_islands;
}
