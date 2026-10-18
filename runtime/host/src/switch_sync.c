// Live switch sync: see switch_sync.h.
#include "switch_sync.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

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
};

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
    if (!atomic_load(&g_enabled))
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
    if (no < 0 || no >= (s32)(MEMORY_SWITCH + DAN_SWITCH + ZONE_SWITCH))
        return 0u;
    // A memory switch cleared (a timed gate, a temporary state) stays this player's own; set ones are shared.
    if (!on && no < (s32)MEMORY_SWITCH)
        return 0u;
    BluewakeSwitchEvent e;
    stage_name(cpu, e.stage);
    e.room = (int8_t)(s32)cpu->gpr[5];
    e.no = (uint8_t)no;
    e.on = on;
    lock();
    if (g_out_count < QUEUE)
        g_out[g_out_count++] = e;
    unlock();
    return 0u;
}

static void set_bit32(CPUState* cpu, u32 words, u32 no, bool on) {
    const u32 at = words + 4u * (no >> 5);
    const u32 bit = 1u << (no & 31u);
    const u32 v = mem_read32(cpu, at);
    mem_write32(cpu, at, on ? (v | bit) : (v & ~bit));
}

void bluewake_switch_sync_retrace(CPUState* cpu) {
    if (!atomic_load(&g_enabled) || g_in_count == 0u)
        return;
    BluewakeSwitchEvent in[QUEUE];
    lock();
    const unsigned n = g_in_count;
    memcpy(in, g_in, n * sizeof *in);
    g_in_count = 0;
    unlock();
    char here[9];
    stage_name(cpu, here);
    for (unsigned i = 0; i < n; ++i) {
        const BluewakeSwitchEvent* e = &in[i];
        if (strncmp(e->stage, here, 8) != 0)
            continue; // another place: its switches are that stage's, not this one's
        const u32 no = e->no;
        if (no < MEMORY_SWITCH) {
            set_bit32(cpu, MEMORY_BIT, no, e->on);
        } else if (no < MEMORY_SWITCH + DAN_SWITCH) {
            set_bit32(cpu, DAN_BIT, no - MEMORY_SWITCH, e->on);
        } else {
            // The zone that holds this room's switches (dComIfGp_roomControl_getZoneNo): the one with that room number.
            for (u32 z = 0; z < ZONE_MAX; ++z) {
                const u32 zone = ZONES + ZONE_SIZE * z;
                if ((s8)mem_read8(cpu, zone) != e->room)
                    continue;
                const u32 bit_no = no - (MEMORY_SWITCH + DAN_SWITCH);
                const u32 at = zone + 2u + 2u * (bit_no >> 4);
                const u16 bit = (u16)(1u << (bit_no & 15u));
                const u16 v = (u16)mem_read16(cpu, at);
                mem_write16(cpu, at, e->on ? (u16)(v | bit) : (u16)(v & ~bit));
                break;
            }
        }
        fprintf(stderr, "[switch-sync] a teammate's switch %u %s (%s room %d)\n", no, e->on ? "on" : "off", here, e->room);
    }
}
