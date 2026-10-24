// Live switch sync: see switch_sync.h.
#include "switch_sync.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    INFO = 0x803C4C08u,       // g_dComIfG_gameInfo.info (dSv_info_c)
    MEMORY_BIT = INFO + 0x778u + 4u, // mMemory (the stage in play): dSv_memBit_c::mSwitch[4] at +4
    DAN_BIT = INFO + 0x79Cu + 4u,    // mDan: mSwitch[2] at +4
    ZONES = INFO + 0x7A8u,    // mZone[32] (dSv_zone_c, 0x4C each): mRoomNo s8 at +0, mZoneBit.mSwitch[3] (u16) at +2
    ZONE_SIZE = 0x4Cu,
    ZONE_MAX = 32u,
    CUR_STAGE = 0x803C9D3Cu,  // play.mCurStage: the stage's name first
    MEMORY_SWITCH = 0x80u,    // dSv_info_c: 0x00-0x7F the stage's memory, 0x80-0xBF the dungeon visit's, then the zone's
    DAN_SWITCH = 0x40u,
    ZONE_SWITCH = 0x30u,
    QUEUE = 64u,
    EVENT_MODE = 0x803C9EA2u, // play.mEvtCtrl.mMode: 0 when no event runs
    PLAYER = 0x803CA754u,     // play.mpPlayerPtr[0]
    SYNCED_SWITCHES = MEMORY_SWITCH + DAN_SWITCH, // the stage's memory and the dungeon visit's: persistent. Zone switches (the
                                                  // room's temporary actor state) never travel: a ship's actors toggle them all the time.
    DEBOUNCE_MS = 1000,       // the same switch is sent at most this often (the last value wins)
    SAME_VALUE_MS = 10000,    // a switch already at that value (sent or applied) is not sent again within this
    QUIET_MS = 2500,          // a teammate's switches wait this long after an event or a scene change
    TRACKED = 48u,
};

typedef struct Tracked {
    char stage[9];
    int8_t room;
    uint8_t no;
    bool used, on, pending, pending_on;
    uint64_t at_ms;  // when the value was last sent or applied
} Tracked;
static Tracked g_tracked[TRACKED];
static unsigned g_tracked_next;

static uint64_t now_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static Tracked* track(const char* stage, int8_t room, uint8_t no) {
    for (unsigned i = 0; i < TRACKED; ++i)
        if (g_tracked[i].used && g_tracked[i].no == no && g_tracked[i].room == room && strncmp(g_tracked[i].stage, stage, 8) == 0)
            return &g_tracked[i];
    Tracked* t = &g_tracked[g_tracked_next++ % TRACKED];
    memset(t, 0, sizeof *t);
    memcpy(t->stage, stage, 8);
    t->room = room;
    t->no = no;
    t->used = true;
    return t;
}

static atomic_bool g_enabled;
static atomic_flag g_lock = ATOMIC_FLAG_INIT;
static BluewakeSwitchEvent g_out[QUEUE], g_in[QUEUE];
static unsigned g_out_count, g_in_count;

static void lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) {
    }
}
static void unlock(void) { atomic_flag_clear_explicit(&g_lock, memory_order_release); }

void bluewake_switch_sync_enable(bool on) {
    atomic_store(&g_enabled, on);
    if (!on) {
        lock();
        memset(g_tracked, 0, sizeof g_tracked);
        g_out_count = g_in_count = 0;
        unlock();
    }
}

unsigned bluewake_switch_sync_take(BluewakeSwitchEvent* out, unsigned max) {
    lock();
    const unsigned n = g_out_count < max ? g_out_count : max;
    memcpy(out, g_out, n * sizeof *out);
    memmove(g_out, g_out + n, (g_out_count - n) * sizeof *out);
    g_out_count -= n;
    unlock();
    return n;
}

void bluewake_switch_sync_give(const BluewakeSwitchEvent* event) {
    if (!atomic_load(&g_enabled) || event->no >= SYNCED_SWITCHES)
        return;
    lock();
    if (g_in_count < QUEUE)
        g_in[g_in_count++] = *event;
    unlock();
}

static void stage_name(CPUState* cpu, char out[9]) {
    for (u32 i = 0; i < 8u; ++i)
        out[i] = (char)mem_read8(cpu, CUR_STAGE + i);
    out[8] = '\0';
}

// The game's own onSwitch or offSwitch was called (r4 the switch, r5 the room): what this player did, for the team.
// Only switches this game sets itself come here; a teammate's are written below without the call (no echo).
u32 bluewake_switch_sync_hook(CPUState* cpu, bool on) {
    if (!atomic_load(&g_enabled))
        return 0u;
    const s32 no = (s32)cpu->gpr[4];
    if (no < 0 || no >= (s32)SYNCED_SWITCHES)
        return 0u; // zone switches are temporary room state, not shared
    // A memory switch cleared (a timed gate, a temporary state) stays this player's own; set ones are shared.
    if (!on && no < (s32)MEMORY_SWITCH)
        return 0u;
    BluewakeSwitchEvent e;
    stage_name(cpu, e.stage);
    e.room = (int8_t)(s32)cpu->gpr[5];
    e.no = (uint8_t)no;
    e.on = on;
    const uint64_t now = now_ms();
    lock();
    Tracked* t = track(e.stage, e.room, e.no);
    if (t->at_ms != 0u && t->on == on && now - t->at_ms < SAME_VALUE_MS) {
        // Already at that value (just sent, or just set from a teammate: no echo).
        t->pending = false;
    } else if (t->at_ms != 0u && now - t->at_ms < DEBOUNCE_MS) {
        t->pending = true; // the retrace sends the last value once the second is over
        t->pending_on = on;
    } else {
        t->at_ms = now;
        t->on = on;
        t->pending = false;
        if (g_out_count < QUEUE)
            g_out[g_out_count++] = e;
    }
    unlock();
    return 0u;
}

static void set_bit32(CPUState* cpu, u32 words, u32 no, bool on) {
    const u32 at = words + 4u * (no >> 5);
    const u32 bit = 1u << (no & 31u);
    const u32 v = mem_read32(cpu, at);
    mem_write32(cpu, at, on ? (v | bit) : (v & ~bit));
}

static uint64_t g_quiet_from_ms; // the last moment an event ran or the scene was changing
static char g_last_stage[9];
static u32 g_last_player;

void bluewake_switch_sync_retrace(CPUState* cpu) {
    if (!atomic_load(&g_enabled))
        return;
    const uint64_t now = now_ms();
    char here[9];
    stage_name(cpu, here);
    const u32 player = mem_read32(cpu, PLAYER);
    if (mem_read8(cpu, EVENT_MODE) != 0u || strncmp(here, g_last_stage, 8) != 0 || player == 0u || player != g_last_player ||
        here[0] == 0)
        g_quiet_from_ms = now;
    memcpy(g_last_stage, here, 9);
    g_last_player = player;
    // Switches held back by the debounce go out once their second is over.
    lock();
    for (unsigned i = 0; i < TRACKED; ++i) {
        Tracked* t = &g_tracked[i];
        if (!t->used || !t->pending || now - t->at_ms < DEBOUNCE_MS)
            continue;
        t->pending = false;
        if (t->pending_on == t->on)
            continue;
        t->on = t->pending_on;
        t->at_ms = now;
        if (g_out_count < QUEUE) {
            BluewakeSwitchEvent e;
            memcpy(e.stage, t->stage, 9);
            e.room = t->room;
            e.no = t->no;
            e.on = t->on;
            g_out[g_out_count++] = e;
        }
    }
    unlock();
    // A teammate's switches wait for a quiet moment: no event running and no scene change in the last few seconds
    // (a switch written into a room that is loading, or under a running event, restarted events and cutscenes).
    if (g_in_count == 0u || now - g_quiet_from_ms < QUIET_MS)
        return;
    BluewakeSwitchEvent in[QUEUE];
    lock();
    const unsigned n = g_in_count;
    memcpy(in, g_in, n * sizeof *in);
    g_in_count = 0;
    unlock();
    for (unsigned i = 0; i < n; ++i) {
        const BluewakeSwitchEvent* e = &in[i];
        if (strncmp(e->stage, here, 8) != 0)
            continue; // another place: its switches are that stage's, not this one's
        const u32 no = e->no;
        if (no >= SYNCED_SWITCHES)
            continue;
        if (no < MEMORY_SWITCH) {
            set_bit32(cpu, MEMORY_BIT, no, e->on);
        } else {
            set_bit32(cpu, DAN_BIT, no - MEMORY_SWITCH, e->on);
        }
        lock();
        Tracked* t = track(e->stage, e->room, e->no);
        t->at_ms = now;
        t->on = e->on;
        t->pending = false;
        unlock();
        fprintf(stderr, "[switch-sync] a teammate's switch %u %s (%s room %d)\n", no, e->on ? "on" : "off", here, e->room);
    }
}
