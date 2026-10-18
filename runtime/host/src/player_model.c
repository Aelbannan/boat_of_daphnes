// Player models without a restart: see player_model.h.
#include "player_model.h"

#include "game_state.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum {
    HOOK_SITE = 0x802356F8u,   // phase_0: after bl JAInter::BankWave::checkAllWaveLoadStatus
    SET_RES = 0x8006EF34u,     // dRes_control_c::setRes(name, info, count, path, direction, heap)
    SYNC_RES = 0x8006F01Cu,    // dRes_control_c::syncRes(name, info, count): 1 loading, 0 done, -1 failed
    DELETE_RES = 0x8006F074u,  // dRes_control_c::deleteRes(name, info, count)
    OBJECT_INFO = 0x803E0BC8u, // g_dComIfG_gameInfo.mResControl.mObjectInfo[64] (+0x1BFC0)
    INFO_SIZE = 0x24u,         // dRes_info_c: mArchiveName[14], mCount (u16) at +0x0E
    INFO_COUNT = 64u,
    STR_LINK = 0x8035A0D8u,    // "Link" in main.dol's string data
    STR_OBJECT = 0x8034F5A7u,  // "/res/Object/"
    WAIT_LIMIT = 300,          // scene starts' frames to wait for Link's count to drop to 1
};

typedef enum Step {
    STEP_IDLE,
    STEP_PENDING, // the next play scene swaps
    STEP_DELETE,  // deleteRes called; back at the site when it returns
    STEP_SET,     // setRes called
    STEP_SYNC,    // syncRes called
    STEP_RESYNC,  // still loading: syncRes again at the next frame's visit
} Step;

static _Atomic int g_request; // set by the app, taken at a scene start
static Step g_step;
static int g_wait;
static atomic_bool g_live;
static _Atomic int g_state; // as bluewake_player_model_state

static int link_count(CPUState* cpu) {
    for (u32 i = 0; i < INFO_COUNT; ++i) {
        const u32 info = OBJECT_INFO + i * INFO_SIZE;
        const u16 count = mem_read16(cpu, info + 0x0Eu);
        if (count != 0u && mem_read32(cpu, info) == 0x4C696E6Bu && mem_read8(cpu, info + 4u) == 0u) // "Link\0"
            return count;
    }
    return 0;
}

// Calls a guest function by returning into it with the link register at the
// hook site: the hook runs again when it returns, r3 its result.
static u32 call(CPUState* cpu, u32 function) {
    cpu->lr = HOOK_SITE;
    return function;
}

u32 bluewake_player_model_hook(CPUState* cpu) {
    atomic_store(&g_live, true);
    switch (g_step) {
    case STEP_IDLE:
        if (atomic_exchange(&g_request, 0) == 0)
            return 0u;
        g_step = STEP_PENDING;
        g_wait = 0;
        // fall through: this scene start swaps
    case STEP_PENDING: {
        const int count = link_count(cpu);
        if (count != 1) {
            // Some actor of the last scene still holds it (or it is not
            // loaded): try again next frame, give up after a while.
            if (++g_wait > WAIT_LIMIT || count == 0) {
                fprintf(stderr, "[model] Link's archive is held (count %d): not swapped; the model changes at the next start\n", count);
                g_step = STEP_IDLE;
                atomic_store(&g_state, -1);
                return 0u;
            }
            cpu->gpr[3] = 1u; // phase_0 waits (cPhs_INIT_e)
            return 0u;
        }
        g_step = STEP_DELETE;
        cpu->gpr[3] = STR_LINK;
        cpu->gpr[4] = OBJECT_INFO;
        cpu->gpr[5] = INFO_COUNT;
        return call(cpu, DELETE_RES);
    }
    case STEP_DELETE:
        g_step = STEP_SET;
        cpu->gpr[3] = STR_LINK;
        cpu->gpr[4] = OBJECT_INFO;
        cpu->gpr[5] = INFO_COUNT;
        cpu->gpr[6] = STR_OBJECT;
        cpu->gpr[7] = 0u; // JKRArchive::DEFAULT_MOUNT_DIRECTION, as the logo scene mounts it
        cpu->gpr[8] = 0u; // no heap: the archive heap
        return call(cpu, SET_RES);
    case STEP_SET:
        if (cpu->gpr[3] != 1u) {
            fprintf(stderr, "[model] mounting the new Link.arc failed\n");
            g_step = STEP_IDLE;
            atomic_store(&g_state, -1);
            cpu->gpr[3] = 1u;
            return 0u;
        }
        g_step = STEP_SYNC;
        cpu->gpr[3] = STR_LINK;
        cpu->gpr[4] = OBJECT_INFO;
        cpu->gpr[5] = INFO_COUNT;
        return call(cpu, SYNC_RES);
    case STEP_SYNC: {
        const s32 result = (s32)cpu->gpr[3];
        if (result > 0) {
            // Still loading: phase_0 waits a frame, then syncs again.
            g_step = STEP_RESYNC;
            cpu->gpr[3] = 1u;
            return 0u;
        }
        g_step = STEP_IDLE;
        atomic_store(&g_state, result == 0 ? 0 : -1);
        fprintf(stderr, "[model] Link's archive %s\n", result == 0 ? "swapped" : "failed to load");
        cpu->gpr[3] = 1u; // one more frame before the scene goes on
        return 0u;
    }
    case STEP_RESYNC:
    default:
        g_step = STEP_SYNC;
        cpu->gpr[3] = STR_LINK;
        cpu->gpr[4] = OBJECT_INFO;
        cpu->gpr[5] = INFO_COUNT;
        return call(cpu, SYNC_RES);
    }
}

bool bluewake_player_model_live(void) { return atomic_load(&g_live); }

void bluewake_player_model_swap(void) {
    atomic_store(&g_request, 1);
    atomic_store(&g_state, 1);
    // Reload the area where Link stands, so a play scene starts now.
    const BluewakeLocation where = bluewake_game_location();
    if (where.has_player)
        bluewake_request_teleport(where.stage, where.room, where.pos, where.angle_y);
}

int bluewake_player_model_state(void) {
    const int s = atomic_load(&g_state);
    return s == 1 && g_step != STEP_IDLE ? 2 : s;
}
