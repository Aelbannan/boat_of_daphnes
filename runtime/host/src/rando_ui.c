// See rando_ui.h.
#include "rando_ui.h"

#include "guest_coro.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    FN_SE_START = 0x800F0D10u,     // mDoAud_seStart(id, pos, mapinfo, reverb)
    FN_BGM_START = 0x802A34A4u,    // JAIZelBasic::bgmStart(this, id, 0, 0): what the game's own mDoAud_bgmStart calls
    FN_LOAD_1ST = 0x80007424u,     // mDoAud_load1stDynamicWave(): 1 once the scene's load timer (set by setSceneName: 36 frames) has run out; clears the scene lock
    FN_ARCHIVE_HEAP = 0x80011AB4u, // mDoExt_getArchiveHeap()
    FN_ARCHIVE_MOUNT = 0x802B8008u,// JKRArchive::mount(path, mode, heap, direction)
    FN_LOADER_UNMOUNT = 0x802B6854u,// JKRFileLoader::unmount(this): one mount count less, deleted at zero
    PLAY_ITEMICON = 0x803CA680u,   // g_dComIfG_gameInfo.play.mpItemIconArchive (+0x47D8): itemicon.arc, mounted once by dScnLogo
    PLAY_MSGDT = 0x803CA6A0u,      // ... mpEnglishTextArchive (+0x47F8), what dComIfGp_setMsgDtArchive sets: bmgres.arc
    FN_SET_SCENE = 0x800073D8u,    // mDoAud_setSceneName(name, room, layer): loads the scene's wave banks and starts its music
    SPOT_DIR_NAME = 0x8039C5B8u,   // JAIZelBasic::spot_dir_name: the scene names the audio knows (char* each)
    FN_CCC_INIT = 0x800227A0u,     // cCc_Init(): builds DMC[] from DynamicNameTable (c_dylink.cpp)
    DMC_INITIALIZED = 0x803F69C0u, // c_dylink.cpp's DMC_initialized
    SITE = 0x8017FD6Cu,
    QUEUE = 8,
    KIND_BGM = 0x40000000u,        // a queue entry: sound number, or this plus a BGM sequence's low 16 bits
    KIND_SUBBGM = 0x10000000u,     // ... or this plus a sub-BGM sequence's low 16 bits (JAIZelBasic::subBgmStart: the jingles)
    FN_SUBBGM_START = 0x802A47B8u, // JAIZelBasic::subBgmStart(this, id): d_menu_fmap's fmMapWrite starts 0x8000005D with it
    KIND_AFTER_SUB = 0x08000000u,  // with KIND_BGM: start only once the sub-BGM (a jingle) has ended, so the music follows it at once
    KIND_SCENE = 0x20000000u,      // ... or this plus an audio scene's index in spot_dir_name (0xFFFF: none, the file select's)
};

static atomic_uint g_queue[QUEUE];
static atomic_uint g_head, g_tail;  // the app writes the tail, the game thread reads the head
static GuestCoro* g_coro;
static atomic_int g_wave_pending;  // a setSceneName was made: mDoAud_load1stDynamicWave has to finish it (the game's play scene does, for the scene it makes)
static unsigned g_orig_scene = 0xFFFFFFFFu;  // JAIZelBasic's set scene (field 0x220) when the file select was entered: what 'none' goes back to
static atomic_int g_rebuild;  // 0 none, 1 asked for, 2 running, 3 asked for (resident archives only), 4 running (same)
static atomic_bool g_hook_ran;

bool bluewake_rando_ui_hook_ran(void) { return atomic_load(&g_hook_ran); }

bool bluewake_rando_ui_rebuild_dyl(void) {
    if (!atomic_load(&g_hook_ran))
        return false;
    atomic_store(&g_rebuild, 1);
    return true;
}

// A seed to seed switch (the randomizer stays on, the code in force is the same): only the seed's resident archives are mounted again.
bool bluewake_rando_ui_reload_resident(void) {
    if (!atomic_load(&g_hook_ran))
        return false;
    atomic_store(&g_rebuild, 3);
    return true;
}

void bluewake_rando_ui_sfx(unsigned sound) {
    const unsigned tail = atomic_load(&g_tail);
    if (tail - atomic_load(&g_head) >= QUEUE)
        return;
    atomic_store(&g_queue[tail % QUEUE], sound);
    atomic_store(&g_tail, tail + 1u);
}

void bluewake_rando_ui_scene(unsigned index) { bluewake_rando_ui_sfx(KIND_SCENE | (index & 0xFFFFu)); }
void bluewake_rando_ui_subbgm(unsigned sequence) { bluewake_rando_ui_sfx(KIND_SUBBGM | (sequence & 0xFFFFu)); }
void bluewake_rando_ui_bgm(unsigned sequence) { bluewake_rando_ui_sfx(KIND_BGM | (sequence & 0xFFFFu)); }
void bluewake_rando_ui_bgm_after_subbgm(unsigned sequence) { bluewake_rando_ui_sfx(KIND_BGM | KIND_AFTER_SUB | (sequence & 0xFFFFu)); }


static u32 find_cstr(CPUState* cpu, const char* text) {
    const size_t n = strlen(text);
    for (u32 a = 0x80330000u; a < 0x803A0000u; ++a) {
        if (mem_read8(cpu, a) != (u8)text[0])
            continue;
        size_t k = 1;
        while (k <= n && mem_read8(cpu, a + (u32)k) == (u8)text[k])
            ++k;
        if (k == n + 1u)
            return a;
    }
    return 0u;
}

static void reload_one(GuestCoro* co, const char* path, u32 slot) {
    CPUState* cpu = bluewake_coro_cpu(co);
    const u32 text = find_cstr(cpu, path);
    const u32 old = mem_read32(cpu, slot);
    if (text == 0u || old < 0x80000000u || old >= 0x81800000u) {
        fprintf(stderr, "[mods] resident archive %s not reloaded (path at 0x%08X, archive 0x%08X)\n", path, text, old);
        return;
    }
    const u32 old_hdr = mem_read32(cpu, old + 0x64u);
    const u32 old_len = old_hdr >= 0x80000000u ? mem_read32(cpu, old_hdr + 4u) : 0u;  // SArcHeader::file_length
    const u32 heap = bluewake_guest_call(co, FN_ARCHIVE_HEAP, 0u, 0u, 0u, 0u, 0u, 0u);
    bluewake_guest_call(co, FN_LOADER_UNMOUNT, old, 0u, 0u, 0u, 0u, 0u);
    const u32 fresh = bluewake_guest_call(co, FN_ARCHIVE_MOUNT, text, 1u, heap, 1u, 0u, 0u);
    mem_write32(cpu, slot, fresh);
    const u32 new_hdr = fresh ? mem_read32(cpu, fresh + 0x64u) : 0u;
    const u32 new_len = new_hdr >= 0x80000000u ? mem_read32(cpu, new_hdr + 4u) : 0u;
    u32 fnv = 2166136261u;  // FNV-1a of the mounted archive's bytes, for comparing with the seed's file on disk
    for (u32 i = 0; new_hdr >= 0x80000000u && i < new_len && i < 0x400000u; ++i)
        fnv = (fnv ^ mem_read8(cpu, new_hdr + i)) * 16777619u;
    fprintf(stderr, "[mods] resident archive %s mounted again: 0x%08X -> 0x%08X, %u -> %u bytes, fnv %08X%s\n", path, old, fresh, old_len, new_len, fnv,
            fresh == 0u ? " (FAILED)" : "");
}

static void reload_resident(GuestCoro* co) {
    reload_one(co, "/res/Msg/bmgres.arc", PLAY_MSGDT);
    reload_one(co, "/res/Msg/itemicon.arc", PLAY_ITEMICON);
}

static void body(GuestCoro* co, void* arg) {
    (void)arg;
    int expected = 1;
    if (atomic_compare_exchange_strong(&g_rebuild, &expected, 2)) {
        // cCc_Init asserts that it has not run (DMC_initialized, an sbss byte): it has, for the other code's table.
        mem_write8(bluewake_coro_cpu(co), DMC_INITIALIZED, 0u);
        bluewake_guest_call(co, FN_CCC_INIT, 0u, 0u, 0u, 0u, 0u, 0u);
        // The archives the game mounts once, at boot (dScnLogo), and keeps: the seed's copies (the DVD's overlay, which the app set before
        // asking for the switch) are not read unless they are mounted again: bmgres.arc (every message: shop labels, hints, item-get texts)
        // and itemicon.arc (the item icons). JKRArchive::mount returns the one already mounted for the same file, so it goes first.
        reload_resident(co);
        fprintf(stderr, "[mods] the dynamic module controls were built again for the code in force (cCc_Init)\n");
        atomic_store(&g_rebuild, 0);
        bluewake_rando_inplace_done();
    }
    int reload_only = 3;
    if (atomic_compare_exchange_strong(&g_rebuild, &reload_only, 4)) {
        reload_resident(co);
        fprintf(stderr, "[mods] the new seed's message and item icon archives were mounted again\n");
        atomic_store(&g_rebuild, 0);
        bluewake_rando_inplace_done();
    }
    for (;;) {
        // A setSceneName was made: the game ignores every further one (the new game's stage's included) until mDoAud_load1stDynamicWave has
        // answered 1, which the game's play scene does for the scene it makes. Finish it here, one frame's call at a time.
        if (atomic_load(&g_wave_pending) != 0) {
            if (bluewake_guest_call(co, FN_LOAD_1ST, 0u, 0u, 0u, 0u, 0u, 0u) == 0u)
                return;
            atomic_store(&g_wave_pending, 0);
            fprintf(stderr, "[rando] file select audio scene loaded (mDoAud_load1stDynamicWave)\n");
        }
        const unsigned head = atomic_load(&g_head);
        if (head == atomic_load(&g_tail))
            return;
        const unsigned sound = atomic_load(&g_queue[head % QUEUE]);
        if ((sound & KIND_BGM) != 0u && (sound & KIND_AFTER_SUB) != 0u) {
            CPUState* cpu = bluewake_coro_cpu(co);
            const u32 interface = mem_read32(cpu, cpu->gpr[13] - 0x69D0u);
            static int waited;
            const u32 sub = interface >= 0x80000000u && interface < 0x81800000u ? mem_read32(cpu, interface + 0x74u) : 0xFFFFFFFFu;
            if (sub != 0xFFFFFFFFu && sub != 0u && ++waited < 900)
                return; // the jingle is still playing: the entry stays queued
            if (waited > 0)
                fprintf(stderr, "[rando] the jingle ended after %d frames: the music follows\n", waited);
            waited = 0;
        }
        atomic_store(&g_head, head + 1u);
        if ((sound & KIND_SCENE) != 0u) {
            CPUState* cpu = bluewake_coro_cpu(co);
            unsigned index = sound & 0xFFFFu;
            const u32 interface = mem_read32(cpu, cpu->gpr[13] - 0x69D0u);
            if (g_orig_scene == 0xFFFFFFFFu && interface >= 0x80000000u && interface < 0x81800000u)
                g_orig_scene = mem_read8(cpu, interface + 0x220u);
            // "None" is the audio scene the file select had (the vanilla one), not scene 0: the game's next setSceneName (the new game's
            // stage) then finds the state it expects. A pending load (36 frames after a setSceneName, finished by load1stDynamicWave)
            // is finished first: while one is pending the game ignores every setSceneName, the new game's included.
            if (index == 0xFFFFu && g_orig_scene != 0xFFFFFFFFu && g_orig_scene != 0u)
                index = g_orig_scene - 1u;
            const u32 name = index == 0xFFFFu ? 0u : mem_read32(cpu, SPOT_DIR_NAME + index * 4u);
            bluewake_guest_call(co, FN_SET_SCENE, name, 0u, 0u, 0u, 0u, 0u);
            atomic_store(&g_wave_pending, 1);
            fprintf(stderr, "[rando] file select audio scene %s (mDoAud_setSceneName)\n", sound == (KIND_SCENE | 0xFFFFu) ? "back" : "spot");
            continue;
        }
        if ((sound & KIND_SUBBGM) != 0u) {
            CPUState* cpu = bluewake_coro_cpu(co);
            const u32 interface = mem_read32(cpu, cpu->gpr[13] - 0x69D0u);
            const u32 sequence = 0x80000000u | (sound & 0xFFFFu);
            bluewake_guest_call(co, FN_SUBBGM_START, interface, sequence, 0u, 0u, 0u, 0u);
            fprintf(stderr, "[rando] file select sub-BGM 0x%08X started (JAIZelBasic::subBgmStart)\n", sequence);
            continue;
        }
        if ((sound & KIND_BGM) != 0u) {
            // The main BGM, as d_s_name.cpp starts the file select's: the audio interface at r13 - 0x69D0.
            CPUState* cpu = bluewake_coro_cpu(co);
            const u32 interface = mem_read32(cpu, cpu->gpr[13] - 0x69D0u);
            const u32 sequence = 0x80000000u | (sound & 0xFFFFu);
            bluewake_guest_call(co, FN_BGM_START, interface, sequence, 0u, 0u, 0u, 0u);
            fprintf(stderr, "[rando] file select music 0x%08X started (JAIZelBasic::bgmStart)\n", sequence);
            continue;
        }
        bluewake_guest_call(co, FN_SE_START, sound, 0u, 0u, 0u, 0u, 0u);
        fprintf(stderr, "[rando] file select sound 0x%04X played (mDoAud_seStart)\n", sound);
    }
}

u32 bluewake_rando_ui_hook(CPUState* cpu, u32 id) {
    if (id != BLUEWAKE_RANDO_UI_HOOK)
        return 0u;
    atomic_store(&g_hook_ran, true);
    {   // A test trigger (the ear checks of the jingles): BLUEWAKE_TEST_SUBBGM / BLUEWAKE_TEST_SE (hex), once, a few seconds into the file select.
        static unsigned calls;
        static int tested;
        if (++calls == 600u && !tested) {
            tested = 1;
            const char* sub = getenv("BLUEWAKE_TEST_SUBBGM");
            const char* se = getenv("BLUEWAKE_TEST_SE");
            if (sub != NULL) bluewake_rando_ui_subbgm((unsigned)strtoul(sub, NULL, 16));
            if (se != NULL) bluewake_rando_ui_sfx((unsigned)strtoul(se, NULL, 16));
        }
    }
    if (g_coro == NULL)
        g_coro = bluewake_coro_create(body);
    if (!bluewake_coro_active(g_coro)) {
        // What the game has as its main BGM (JAIZelBasic::mMainBgmNum), logged when it changes: the log of what plays.
        static u32 s_last = 0xFFFFFFFFu;
        const u32 interface = mem_read32(cpu, cpu->gpr[13] - 0x69D0u);
        if (interface >= 0x80000000u && interface < 0x81800000u) {
            const u32 now = mem_read32(cpu, interface + 0x78u);
            if (now != s_last) {
                fprintf(stderr, "[rando] main BGM is now 0x%08X (subBgm 0x%08X)\n", now, mem_read32(cpu, interface + 0x74u));
                s_last = now;
            }
        }
        if (atomic_load(&g_head) == atomic_load(&g_tail) && atomic_load(&g_rebuild) != 1 && atomic_load(&g_rebuild) != 3 && atomic_load(&g_wave_pending) == 0)
            return 0u;
    }
    return bluewake_coro_step(g_coro, cpu, SITE, NULL);
}
