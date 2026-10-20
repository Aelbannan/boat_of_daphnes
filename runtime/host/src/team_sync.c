// Team sync: see team_sync.h.
#include "team_sync.h"
#include "bunny_hood.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum {
    SAVE = 0x803C4C08u,       // g_dComIfG_gameInfo.save (dSv_info_c; mSavedata first, mPlayer first)
    EVENT_MODE = 0x803C9EA2u, // play.mEvtCtrl.mMode: 0 when no event runs
    STAGE_NAME = 0x803C9D3Cu, // play.mCurStage.mName
    STAG_INFO = 0x803C9DA0u,  // play.mStageData.mpStagInfo (play +0x3EB0, +0x48)
    PLAYER = 0x803CA754u,     // play.mpPlayerPtr[0]
    MEMORY = 0x380u,          // mSavedata.mMemory[16]: one dSv_memory_c (0x24) per save table
    MEMORY_SIZE = 0x24u,
    STAGES = 16u,
    KEY_NUM = 0x20u,          // dSv_memBit_c: mTbox, mSwitch[4], mItem, mVisitedRoom[2], mKeyNum, mDungeonItem
    LIVE = 0x778u,            // dSv_info_c::mMemory: the current stage's copy, put back when it is left
    COLLECT4 = 0x0B4u + 4u,   // mCollect[4]: Hero's Charm / Bunny Hood owned (0x01, 0x04), worn (0x02, 0x08), shown (0x10)
    COLLECT4_SYNC = 0x05u,    // only ownership travels: wearing is personal
    QUIET_RETRACES = 60,      // no event for this long before a teammate's state is merged
    STABLE_RETRACES = 30,     // the stage must hold this long before its live copy counts
};

typedef enum { MERGE_OR, MERGE_MAX, MERGE_SLOT } Merge;

typedef struct Region {
    uint16_t offset; // from SAVE
    uint16_t size;
    Merge merge;
} Region;

// dSv_player_c (tww include/d/d_save.h:604-620), then the stage memories,
// the ocean and the event flags (d_save.h:906-909).
static const Region kRegions[] = {
    {0x000, 1, MERGE_MAX},  // max life, high byte (u16, big-endian: both bytes max as a pair below)
    {0x001, 1, MERGE_MAX},  // max life, low byte
    {0x00E, 3, MERGE_MAX},  // selected sword, shield, bracelet (item numbers grow with upgrades)
    {0x012, 2, MERGE_MAX},  // wallet size, max magic
    {0x03C, 21, MERGE_SLOT}, // item slots
    {0x051, 21, MERGE_OR},  // item-get bits
    {0x06E, 8, MERGE_MAX},  // ammo maxima
    {0x090, 12, MERGE_OR},  // bag-get bits
    {0x0B4, 12, MERGE_OR},  // collection: swords, shields, ..., songs, Triforce shards, pearls
    {0x0C4, 0x81, MERGE_OR}, // charts and the sea map
    // Stage memories: chests, switches, items, visited rooms, map, compass,
    // big key, boss (the key counts, +0x20 of each, travel as counts below).
    {MEMORY, MEMORY_SIZE * STAGES, MERGE_OR},
    {0x5C0, 0x64, MERGE_OR}, // dSv_ocean_c
    // Event flags: bytes 0x00-0x78 are single-bit flags; 0x79-0xFF hold
    // registers (counters, scores: d_save_event_flag.inc), which OR would corrupt.
    {0x624, 0x79, MERGE_OR},
};

// Small keys are spent, so they travel as changes: a pseudo offset per save
// table, the value a signed difference (KEY_DELTA) or, in a team state, the
// count (KEY_COUNT, merged as a maximum).
enum { KEY_DELTA = 0x0F00u, KEY_COUNT = 0x0F10u };

#define SYNC_BYTES 0x800u
static uint8_t g_shadow[SYNC_BYTES];  // the synced bytes as last seen, by offset
static uint8_t g_key_shadow[STAGES];
static bool g_have_shadow;
static atomic_bool g_enabled;
static int g_live_table = -1; // the save table whose live copy counts, -1 none
static int g_seen_table = -1, g_seen_for;

#define QUEUE 2048u
static BluewakeSyncChange g_out[QUEUE], g_in[QUEUE];
static unsigned g_out_count, g_in_count;
static atomic_flag g_lock = ATOMIC_FLAG_INIT;

static void lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) {
    }
}
static void unlock(void) { atomic_flag_clear_explicit(&g_lock, memory_order_release); }

static bool is_key_byte(uint16_t offset) {
    return offset >= MEMORY && offset < MEMORY + MEMORY_SIZE * STAGES && (offset - MEMORY) % MEMORY_SIZE == KEY_NUM;
}

static bool is_padding(uint16_t offset) {
    return offset >= MEMORY && offset < MEMORY + MEMORY_SIZE * STAGES && (offset - MEMORY) % MEMORY_SIZE >= 0x22u;
}

static const Region* region_of(uint16_t offset) {
    if (is_key_byte(offset) || is_padding(offset))
        return NULL;
    for (unsigned i = 0; i < sizeof kRegions / sizeof kRegions[0]; ++i)
        if (offset >= kRegions[i].offset && offset < kRegions[i].offset + kRegions[i].size)
            return &kRegions[i];
    return NULL;
}

static bool is_bottle_slot(uint16_t offset) { return offset >= 0x03C + 14 && offset <= 0x03C + 17; }

// An item slot's upgrades: the Picto Box to the Deluxe one; the Bow to the Bow with
// Fire and Ice Arrows, then with Light Arrows.
static int slot_rank(uint16_t offset, uint8_t item) {
    if (item == 0xFF)
        return -1;
    if (offset == 0x03C + 8)
        return item == 0x26 ? 2 : 1;
    if (offset == 0x03C + 12)
        return item == 0x36 ? 3 : item == 0x35 ? 2 : 1;
    return 1;
}

// Whether a teammate's value improves ours (for the region's merge).
static bool improves(const Region* r, uint16_t offset, uint8_t mine, uint8_t theirs, uint8_t* result) {
    switch (r->merge) {
    case MERGE_OR:
        *result = (uint8_t)(mine | theirs);
        return *result != mine;
    case MERGE_MAX:
        *result = theirs;
        // The selected sword, shield and bracelet are 0xFF when none is equipped: the lowest rank there, not the
        // highest (taken as a number, a teammate with no sword unequipped everyone's sword and shield).
        if (offset >= 0x00E && offset < 0x011)
            return theirs != 0xFF && (mine == 0xFF || theirs > mine);
        return theirs > mine;
    case MERGE_SLOT:
        if (is_bottle_slot(offset))
            return false;
        *result = theirs;
        return slot_rank(offset, theirs) > slot_rank(offset, mine);
    }
    return false;
}

static bool safe_point(CPUState* cpu) {
    if (cpu == NULL || cpu->ram == NULL)
        return false;
    if (mem_read8(cpu, STAGE_NAME) == 0u)
        return false; // no stage yet (boot, title)
    return mem_read8(cpu, EVENT_MODE) == 0u;
}

// The save table of the stage in play, once it has held for a while with
// Link in it (so a scene change's half-loaded state never counts).
static void track_live_table(CPUState* cpu) {
    int table = -1;
    const u32 stag = mem_read32(cpu, STAG_INFO);
    const u32 player = mem_read32(cpu, PLAYER);
    if (stag >= 0x80000000u && stag < 0x81800000u && player >= 0x80000000u && player < 0x81800000u)
        table = (mem_read8(cpu, stag + 9u) >> 1) & 0x7F;
    if (table >= (int)STAGES)
        table = -1;
    if (table != g_seen_table) {
        g_seen_table = table;
        g_seen_for = 0;
    } else if (g_seen_for < STABLE_RETRACES) {
        ++g_seen_for;
    }
    const int live = g_seen_for >= STABLE_RETRACES ? table : -1;
    if (live != g_live_table) {
        g_live_table = live;
        // The live copy's bits now show through the effective bytes below;
        // take them in as seen, so a stage change is not sent as a gain.
        if (g_have_shadow && live >= 0) {
            for (uint16_t f = 0; f < MEMORY_SIZE; ++f) {
                const uint16_t o = (uint16_t)(MEMORY + (uint16_t)live * MEMORY_SIZE + f);
                if (f == KEY_NUM)
                    g_key_shadow[live] = mem_read8(cpu, SAVE + LIVE + KEY_NUM);
                else
                    g_shadow[o] |= mem_read8(cpu, SAVE + LIVE + f);
            }
        }
    }
}

// The byte as the game will keep it: for the stage in play, its save table
// with the live copy's bits.
static uint8_t effective(CPUState* cpu, uint16_t o) {
    if (o == COLLECT4)
        return (uint8_t)(bluewake_bunny_hood_real_collect(cpu) & COLLECT4_SYNC);
    uint8_t b = mem_read8(cpu, SAVE + o);
    if (g_live_table >= 0 && o >= MEMORY + (uint16_t)g_live_table * MEMORY_SIZE &&
        o < MEMORY + (uint16_t)(g_live_table + 1) * MEMORY_SIZE)
        b |= mem_read8(cpu, SAVE + LIVE + (o - MEMORY) % MEMORY_SIZE);
    return b;
}

static void write_merged(CPUState* cpu, uint16_t o, uint8_t value) {
    if (o == COLLECT4) {
        bluewake_bunny_hood_add_owned(cpu, (uint8_t)(value & COLLECT4_SYNC));
        return;
    }
    mem_write8(cpu, SAVE + o, value);
    if (g_live_table >= 0 && o >= MEMORY + (uint16_t)g_live_table * MEMORY_SIZE &&
        o < MEMORY + (uint16_t)(g_live_table + 1) * MEMORY_SIZE) {
        const u32 live = SAVE + LIVE + (o - MEMORY) % MEMORY_SIZE;
        mem_write8(cpu, live, (uint8_t)(mem_read8(cpu, live) | value));
    }
}

static uint8_t key_count(CPUState* cpu, unsigned table) {
    return (int)table == g_live_table ? mem_read8(cpu, SAVE + LIVE + KEY_NUM)
                                      : mem_read8(cpu, SAVE + MEMORY + table * MEMORY_SIZE + KEY_NUM);
}

static void set_key_count(CPUState* cpu, unsigned table, uint8_t count) {
    mem_write8(cpu, SAVE + MEMORY + table * MEMORY_SIZE + KEY_NUM, count);
    if ((int)table == g_live_table)
        mem_write8(cpu, SAVE + LIVE + KEY_NUM, count);
    g_key_shadow[table] = count;
}

void bluewake_team_sync_enable(bool on) {
    atomic_store(&g_enabled, on);
    lock();
    if (!on) {
        g_have_shadow = false;
        g_out_count = g_in_count = 0;
        g_live_table = g_seen_table = -1;
    }
    unlock();
}

static int g_quiet; // retraces since an event last ran

void bluewake_team_sync_retrace(CPUState* cpu) {
    if (!atomic_load(&g_enabled))
        return;
    if (cpu != NULL && cpu->ram != NULL && mem_read8(cpu, EVENT_MODE) != 0u)
        g_quiet = 0;
    else if (g_quiet < QUIET_RETRACES)
        ++g_quiet;
    if (!safe_point(cpu))
        return;
    lock();
    track_live_table(cpu);
    // A teammate's state is merged only in a settled place: the stage held for a while and no event for a while.
    // Flags written while a scene is loading or just after a cutscene could start one again (event 238 at the pirate
    // ship looped on a merge, 2026-10-19); the changes wait in the queue.
    const bool settled = g_live_table >= 0 && g_quiet >= QUIET_RETRACES;
    // A teammate's gains first, so they are not taken for ours.
    for (unsigned i = 0; i < g_in_count && g_have_shadow && settled; ++i) {
        const BluewakeSyncChange c = g_in[i];
        if (c.offset >= KEY_DELTA && c.offset < KEY_DELTA + STAGES) {
            const unsigned t = c.offset - KEY_DELTA;
            int n = key_count(cpu, t) + (int)(int8_t)c.value;
            set_key_count(cpu, t, (uint8_t)(n < 0 ? 0 : n > 99 ? 99 : n));
            fprintf(stderr, "[sync] in keys table %u: %+d -> %d\n", t, (int)(int8_t)c.value, g_key_shadow[t]);
            continue;
        }
        if (c.offset >= KEY_COUNT && c.offset < KEY_COUNT + STAGES) {
            const unsigned t = c.offset - KEY_COUNT;
            if (c.value > key_count(cpu, t))
                set_key_count(cpu, t, c.value);
            continue;
        }
        const Region* r = region_of(c.offset);
        if (r == NULL)
            continue;
        const uint8_t mine = effective(cpu, c.offset);
        const uint8_t theirs = c.offset == COLLECT4 ? (uint8_t)(c.value & COLLECT4_SYNC) : c.value; // an older build's worn bits are ignored
        uint8_t merged;
        if (improves(r, c.offset, mine, theirs, &merged)) {
            write_merged(cpu, c.offset, merged);
            fprintf(stderr, "[sync] in 0x%03X: %02X -> %02X\n", c.offset, mine, merged);
        }
        g_shadow[c.offset] = effective(cpu, c.offset);
    }
    if (g_have_shadow && settled)
        g_in_count = 0;
    // Our own gains since the last look.
    for (unsigned k = 0; k < sizeof kRegions / sizeof kRegions[0]; ++k) {
        const Region* r = &kRegions[k];
        for (uint16_t o = r->offset; o < r->offset + r->size; ++o) {
            if (region_of(o) != r)
                continue; // a key count or padding inside the stage memories
            const uint8_t now = effective(cpu, o);
            if (!g_have_shadow) {
                g_shadow[o] = now;
                continue;
            }
            if (now == g_shadow[o])
                continue;
            uint8_t merged;
            if (improves(r, o, g_shadow[o], now, &merged) && g_out_count < QUEUE)
                g_out[g_out_count++] = (BluewakeSyncChange){o, r->merge == MERGE_OR ? (uint8_t)(now & ~g_shadow[o]) : now};
            g_shadow[o] = now;
        }
    }
    for (unsigned t = 0; t < STAGES; ++t) {
        const uint8_t now = key_count(cpu, t);
        if (g_have_shadow && now != g_key_shadow[t] && g_out_count < QUEUE)
            g_out[g_out_count++] = (BluewakeSyncChange){(uint16_t)(KEY_DELTA + t), (uint8_t)(int8_t)(now - g_key_shadow[t])};
        g_key_shadow[t] = now;
    }
    g_have_shadow = true;
    unlock();
}

unsigned bluewake_team_sync_take(BluewakeSyncChange* out, unsigned capacity) {
    lock();
    const unsigned n = g_out_count < capacity ? g_out_count : capacity;
    memcpy(out, g_out, n * sizeof *out);
    memmove(g_out, g_out + n, (g_out_count - n) * sizeof *out);
    g_out_count -= n;
    unlock();
    return n;
}

void bluewake_team_sync_give(const BluewakeSyncChange* changes, unsigned count) {
    lock();
    for (unsigned i = 0; i < count && g_in_count < QUEUE; ++i)
        if (region_of(changes[i].offset) != NULL || (changes[i].offset >= KEY_DELTA && changes[i].offset < KEY_COUNT + STAGES))
            g_in[g_in_count++] = changes[i];
    unlock();
}

unsigned bluewake_team_sync_snapshot(BluewakeSyncChange* out, unsigned capacity) {
    lock();
    unsigned n = 0;
    if (g_have_shadow) {
        for (unsigned k = 0; k < sizeof kRegions / sizeof kRegions[0]; ++k)
            for (uint16_t o = kRegions[k].offset; o < kRegions[k].offset + kRegions[k].size && n < capacity; ++o)
                if (region_of(o) == &kRegions[k] && !(kRegions[k].merge == MERGE_OR && g_shadow[o] == 0u))
                    out[n++] = (BluewakeSyncChange){o, g_shadow[o]};
        for (unsigned t = 0; t < STAGES && n < capacity; ++t)
            if (g_key_shadow[t] != 0u)
                out[n++] = (BluewakeSyncChange){(uint16_t)(KEY_COUNT + t), g_key_shadow[t]};
    }
    unlock();
    return n;
}
