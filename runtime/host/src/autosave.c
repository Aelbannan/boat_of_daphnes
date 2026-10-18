#include "autosave.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_state.h"
#include "guest_coro.h"

enum {
    GAME_INFO = 0x803C4C08u,     // g_dComIfG_gameInfo.info (dSv_info_c)
    INFO_DATA_NUM = 0x1290u,     // mDataNum, then mNewFile, mNoFile; mMemCardCheckID (u64) at 0x1298
    INFO_NEW_FILE = 0x1291u,
    INFO_NO_FILE = 0x1292u,
    INFO_CHECK_ID = 0x1298u,
    STAGE_DATA = 0x803C9D58u,    // play.mStageData (dStage_stageDt_c), its vtable entry 44 is getStagInfo
    VT_GET_STAG_INFO = 0xB0u,
    CARD_CTRL = 0x803B39A0u,     // g_mDoMemCd_control: mData (3 files, 0x1650 bytes) first
    CARD_SERIAL = 0x1688u,       // mCardSerialNo (u64)
    CARD_COMMAND = 0x165Cu,      // mCommand: 0 when the card thread has none
    CARD_STATE = 0x1660u,        // field_0x1660: 1 ready, 4 a store done and not yet collected
    CARD_DATA_SIZE = 0x1650u,
    PLAYER = 0x803CA754u,        // play.mpPlayerPtr[0]
    PLAYER_PROC = 0x31D8u,       // daPy_lk_c::mCurProc
    EVENT_MODE = 0x803C9EA2u,    // play.mEvtCtrl.mMode
    NEXT_STAGE_GO = 0x803C9D48u + 0xCu, // play.mNextStage enable
    FN_EXCHANGE = 0x80055580u,   // dComIfGs_exchangePlayerRecollectionData
    FN_PUT_SAVE = 0x8005D988u,   // dSv_info_c::putSave(int stageNo)
    FN_GAME_START = 0x800548FCu, // dComIfGs_setGameStartStage
    FN_MEM_TO_CARD = 0x8005E780u, // dSv_info_c::memory_to_card(char* card, int dataNum)
    FN_CHECKSUM = 0x8001A454u,   // mDoMemCdRWm_SetCheckSumGameData(u8*, u8)
    FN_CARD_SAVE = 0x800191C4u,  // mDoMemCd_Ctrl_c::save(void*, u32 size, u32 offset)
    FN_SAVE_SYNC = 0x8001931Cu,  // mDoMemCd_Ctrl_c::SaveSync: 0 waiting, 1 done, 2 failed
    // dSv_player_c inside dSv_save_c at the start of dSv_info_c: what makes an item "new".
    SAVE_ITEMS = 0x03Cu, SAVE_ITEMS_END = 0x066u,     // items and the get-item flags
    SAVE_BAGS = 0x076u, SAVE_BAGS_END = 0x0A8u,       // bag items, get-bag flags, bag records
    SAVE_COLLECT = 0x0B4u, SAVE_COLLECT_END = 0x0C1u, // swords, shields, songs, pearls, triforce
    SAVE_MAX_LIFE = 0x000u,
    SETTLE_FRAMES = 180,        // in a scene this long before a save
    WAIT_FRAMES = 600,          // the card thread's time to finish
    RETRY_FRAMES = 3600,        // after a failure
};

static atomic_int g_interval_minutes;
static atomic_bool g_on_item;
static atomic_int g_done_count, g_done_slot, g_done_ok;
static int g_taken;

void bluewake_autosave_configure(int interval_minutes, bool on_item) {
    atomic_store(&g_interval_minutes, interval_minutes < 0 ? 0 : interval_minutes > 30 ? 30 : interval_minutes);
    atomic_store(&g_on_item, on_item);
}

bool bluewake_autosave_take_done(int* slot, bool* ok) {
    const int now = atomic_load(&g_done_count);
    if (now == g_taken)
        return false;
    g_taken = now;
    *slot = atomic_load(&g_done_slot);
    *ok = atomic_load(&g_done_ok) != 0;
    return true;
}

static bool guest_ptr(u32 p) { return p >= 0x80000000u && p < 0x81800000u; }

// The state of the sequence and the timers (the game thread).
typedef enum { IDLE, WRITING, WAITING } Phase;
static Phase g_phase;
static unsigned g_play_frames;   // retraces since the scene changed (play scene only)
static unsigned g_timer;         // retraces of play since the last save
static unsigned g_since_fail = RETRY_FRAMES;
static bool g_want;              // a save is due: it waits for a quiet moment
static const char* g_why = "";
static bool g_hash_valid;
static u32 g_hash;
static char g_scene[9];
static int g_scene_room = -2;
static unsigned g_wait_frames;
static int g_slot;
static GuestCoro* g_co;
static unsigned long long g_logged;

static u32 fnv(u32 h, u8 b) { return (h ^ b) * 16777619u; }

// The save data's inventory, collection and heart containers.
static u32 inventory_hash(CPUState* cpu) {
    u32 h = 2166136261u;
    const struct { u32 from, to; } ranges[] = {{SAVE_ITEMS, SAVE_ITEMS_END}, {SAVE_BAGS, SAVE_BAGS_END},
                                               {SAVE_COLLECT, SAVE_COLLECT_END}, {SAVE_MAX_LIFE, SAVE_MAX_LIFE + 2u}};
    for (size_t r = 0; r < sizeof ranges / sizeof ranges[0]; ++r)
        for (u32 o = ranges[r].from; o < ranges[r].to; ++o)
            h = fnv(h, mem_read8(cpu, GAME_INFO + o));
    return h;
}

static bool proc_is_calm(u32 proc) {
    switch (proc) {
    case 0x04: case 0x05: // waiting, free waiting
    case 0x06: case 0x07: // moving, moving while locked on
    case 0x08: case 0x09: // locked on an actor
    case 0x17: case 0x18: // turning
    case 0x88: case 0x89: // the helm of the boat, paddling
        return true;
    default:
        return false;
    }
}

// The pause menu's own test for "can save here" (memCardDataSave), the card thread idle, and a calm game.
static bool quiet_now(CPUState* cpu, u32 menu_flag, const char** why) {
    if (g_play_frames < SETTLE_FRAMES) { *why = "scene just started"; return false; }
    if (menu_flag != 0u) { *why = "menu"; return false; }
    if (mem_read8(cpu, EVENT_MODE) != 0u) { *why = "event"; return false; }
    if (mem_read8(cpu, NEXT_STAGE_GO) != 0u) { *why = "scene change"; return false; }
    const u32 player = mem_read32(cpu, PLAYER);
    if (!guest_ptr(player)) { *why = "no Link"; return false; }
    if (!proc_is_calm(mem_read32(cpu, player + PLAYER_PROC))) { *why = "Link busy"; return false; }
    if (mem_read32(cpu, CARD_CTRL + CARD_COMMAND) != 0u || mem_read32(cpu, CARD_CTRL + CARD_STATE) != 1u) {
        *why = "card busy";
        return false;
    }
    if (mem_read8(cpu, GAME_INFO + INFO_NEW_FILE) != 1u) {
        if (mem_read8(cpu, GAME_INFO + INFO_NO_FILE) != 0u) { *why = "no file"; return false; }
        if (mem_read32(cpu, GAME_INFO + INFO_CHECK_ID) != mem_read32(cpu, CARD_CTRL + CARD_SERIAL) ||
            mem_read32(cpu, GAME_INFO + INFO_CHECK_ID + 4u) != mem_read32(cpu, CARD_CTRL + CARD_SERIAL + 4u)) {
            *why = "other card";
            return false;
        }
    }
    return true;
}

void bluewake_autosave_retrace(CPUState* cpu) {
    const BluewakeLocation here = bluewake_game_location();
    const bool playing = here.scene_proc == BLUEWAKE_SCENE_PLAY && here.has_player;
    if (!playing) {
        g_play_frames = 0;
        g_hash_valid = false;
        g_scene[0] = '\0';
        if (g_phase == IDLE)
            g_want = false;
        return;
    }
    if (strncmp(g_scene, here.stage, 8) != 0 || g_scene_room != here.room) {
        memcpy(g_scene, here.stage, 9);
        g_scene_room = here.room;
        g_play_frames = 0;
    }
    ++g_play_frames;
    if (g_since_fail < RETRY_FRAMES)
        ++g_since_fail;
    if (g_phase != IDLE)
        return;
    const int minutes = atomic_load(&g_interval_minutes);
    static int test_seconds = -1;
    if (test_seconds < 0) {
        const char* v = getenv("BLUEWAKE_AUTOSAVE_TEST_SECONDS"); // a test's short timer; the setting is in minutes
        test_seconds = v != NULL ? atoi(v) : 0;
    }
    const unsigned limit = test_seconds > 0 ? (unsigned)test_seconds * 60u : (unsigned)minutes * 3600u;
    ++g_timer;
    if (!g_want && limit != 0u && g_timer >= limit) {
        g_want = true;
        g_why = "timer";
    }
    if ((g_play_frames % 30u) == 0u && g_play_frames >= SETTLE_FRAMES) {
        const u32 h = inventory_hash(cpu);
        if (!g_hash_valid) {
            g_hash = h;
            g_hash_valid = true;
        } else if (h != g_hash) {
            g_hash = h;
            if (atomic_load(&g_on_item) && !g_want) {
                g_want = true;
                g_why = "new item";
            }
        }
    }
}

bool bluewake_autosave_active(void) { return g_co != NULL && bluewake_coro_active(g_co); }

static void abort_with_exchange(GuestCoro* co, bool exchanged) {
    if (exchanged)
        bluewake_guest_call(co, FN_EXCHANGE, 0, 0, 0, 0, 0, 0);
    g_phase = IDLE;
    g_since_fail = 0;
    atomic_store(&g_done_slot, g_slot);
    atomic_store(&g_done_ok, 0);
    atomic_fetch_add(&g_done_count, 1);
}

static void body(GuestCoro* co, void* arg) {
    (void)arg;
    CPUState* cpu = bluewake_coro_cpu(co);
    if (g_phase == WRITING) {
        g_slot = mem_read8(cpu, GAME_INFO + INFO_DATA_NUM);
        if (g_slot > 2) {
            g_phase = IDLE;
            g_want = false;
            return;
        }
        bluewake_guest_call(co, FN_EXCHANGE, 0, 0, 0, 0, 0, 0);
        const u32 vtable = mem_read32(cpu, STAGE_DATA);
        const u32 get_info = guest_ptr(vtable) ? mem_read32(cpu, vtable + VT_GET_STAG_INFO) : 0u;
        const u32 info = guest_ptr(get_info) ? bluewake_guest_call(co, get_info, STAGE_DATA, 0, 0, 0, 0, 0) : 0u;
        if (!guest_ptr(info)) {
            fprintf(stderr, "[autosave] no stage info; not saved\n");
            abort_with_exchange(co, true);
            return;
        }
        const u32 save_no = (mem_read8(cpu, info + 9u) >> 1) & 0x7Fu;
        bluewake_guest_call(co, FN_PUT_SAVE, GAME_INFO, save_no, 0, 0, 0, 0);
        bluewake_guest_call(co, FN_GAME_START, 0, 0, 0, 0, 0, 0);
        const u32 result = bluewake_guest_call(co, FN_MEM_TO_CARD, GAME_INFO, CARD_CTRL, (u32)g_slot, 0, 0, 0);
        if (result == 0xFFFFFFFFu) {
            fprintf(stderr, "[autosave] the save data is too big for its slot; not saved\n");
            abort_with_exchange(co, true);
            return;
        }
        bluewake_guest_call(co, FN_CHECKSUM, CARD_CTRL, (u32)g_slot, 0, 0, 0, 0);
        // The card control's own image is the buffer (the pause menu keeps a copy of it): the copy onto itself changes nothing.
        bluewake_guest_call(co, FN_CARD_SAVE, CARD_CTRL, CARD_CTRL, CARD_DATA_SIZE, 0, 0, 0);
        g_phase = WAITING;
        g_wait_frames = 0;
        return;
    }
    // WAITING: the card thread writes the file; SaveSync says when.
    const u32 r = bluewake_guest_call(co, FN_SAVE_SYNC, CARD_CTRL, 0, 0, 0, 0, 0);
    if (r == 1u) {
        mem_write32(cpu, GAME_INFO + INFO_CHECK_ID, mem_read32(cpu, CARD_CTRL + CARD_SERIAL));
        mem_write32(cpu, GAME_INFO + INFO_CHECK_ID + 4u, mem_read32(cpu, CARD_CTRL + CARD_SERIAL + 4u));
        mem_write8(cpu, GAME_INFO + INFO_NEW_FILE, 0u);
        mem_write8(cpu, GAME_INFO + INFO_NO_FILE, 0u);
        bluewake_guest_call(co, FN_EXCHANGE, 0, 0, 0, 0, 0, 0);
        g_phase = IDLE;
        g_timer = 0;
        g_want = false;
        g_hash = inventory_hash(cpu);
        g_hash_valid = true;
        atomic_store(&g_done_slot, g_slot);
        atomic_store(&g_done_ok, 1);
        atomic_fetch_add(&g_done_count, 1);
        fprintf(stderr, "[autosave] file %d saved (%s)\n", g_slot + 1, g_why);
    } else if (r == 2u || ++g_wait_frames > WAIT_FRAMES) {
        fprintf(stderr, "[autosave] file %d: the card %s\n", g_slot + 1, r == 2u ? "refused the save" : "did not answer");
        g_want = false;
        abort_with_exchange(co, true);
    }
}

u32 bluewake_autosave_hook(CPUState* cpu, u32 site) {
    if (g_co == NULL) {
        if (!g_want && g_phase == IDLE)
            return 0u;
        g_co = bluewake_coro_create(body);
    }
    if (bluewake_coro_active(g_co))
        return bluewake_coro_step(g_co, cpu, site, NULL);
    if (g_phase == IDLE) {
        if (!g_want || g_since_fail < RETRY_FRAMES)
            return 0u;
        const char* why = "";
        if (!quiet_now(cpu, cpu->gpr[3] & 0xFFu, &why)) {
            static const char* last;
            if (why != last && g_logged++ < 40u)
                fprintf(stderr, "[autosave] due (%s), waiting: %s\n", g_why, why);
            last = why;
            return 0u;
        }
        g_phase = WRITING;
    }
    return bluewake_coro_step(g_co, cpu, site, NULL);
}
