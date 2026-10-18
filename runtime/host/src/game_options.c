#include "switch_sync.h"
#include "game_options.h"
#include "bmg_patch.h"
#include "voice_pitch.h"
#include "autosave.h"
#include "widescreen.h"
#include "treasure_glow.h"
#include "player_model.h"
#include "rando_seed.h"
#include "rando_ui.h"
#include "puppet.h"
#include "puppet_pvp.h"
#include "bunny_hood.h"
#include "text_swap.h"
#include "blank_thp.h"
#include "gxruntime/dvd.h"

#include <dlfcn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The native code at the options' hook sites (the ids in mods/betterww/
// options.txt). Each does what Better Wind Waker's added assembly does at the
// same place, on the guest state the site hands it.
enum {
    HOOK_SWING_TURN = 1,
    HOOK_INVERT_CAMERA_X = 2,
    HOOK_SAIL_WIND = 3,
    HOOK_SAIL_BRAKE = 4,
    HOOK_SAIL_IDLE_BRAKE = 5,
    // Better Wind Waker extras (mods/betterww/options.txt): 18 is every main.dol
    // hook of Hero Mode, No Low-Health Beep and Fix Vanilla Bugs, told apart by
    // the site's address; 19-26 are the REL hooks of Fix Vanilla Bugs.
    HOOK_BWW_DOL = 18,
    HOOK_FIX_STALFOS = 19,
    HOOK_FIX_MINIBLIN = 20,
    HOOK_FIX_POE_DEAD = 21,
    HOOK_FIX_POE_UNKILL = 22,
    HOOK_FIX_MAGTAIL = 23,
    HOOK_FIX_PHANTOM_GANON = 24,
    HOOK_FIX_BDK_LAND_INIT = 25,
    HOOK_FIX_BDK_LAND_TIMEOUT = 26,
    HOOK_FIX_LAST = 26,
    HOOK_GX_ARRAY_SOURCE = 33,
    HOOK_GX_IMAGE_SOURCE = 35,
    HOOK_SEARCH_BY_ID = 36,
    HOOK_SWITCH_ON = 37,
    HOOK_SWITCH_OFF = 38,
    HOOK_DVD_CHECK_DISK = 39,
    HOOK_JUDGE_BY_ID = 40,
    HOOK_OS_RESET_SYSTEM = 41,
    HOOK_IDLE_SKIP = 42,
};

typedef u32 (*NativeHookFn)(CPUState* ctx, u32 id);
typedef void (*OptionWriteFn)(void* user, u32 address, const u8* bytes, u32 size);

typedef const char* (*OptionFn)(u32, u32*, u32*, const char**);
typedef u32 (*WritesFn)(OptionWriteFn, void*);

static volatile unsigned char* g_flags;
static OptionFn g_option;
static CPUState* g_cpu;
static u32 g_instant_text;
static u32 g_brisk_sail;
static u32 g_swift_sail;
static u32 g_invert_camera_x;
static u32 g_hud_edges;
void dol_gx_note_array_source(u32 attr, u32 guest_pointer);
void dol_gx_note_image_source(u32 guest_pointer);

// The values each option writes at boot (options.txt "write" lines), kept with
// what the game's memory held before, so the setting can be turned on and off
// while the game runs: Settings changes them through bluewake_game_options_set.
typedef struct OptionWrite {
    u32 index;
    u32 address;
    u32 size;
    u8* now;
    u8* before;
} OptionWrite;
static OptionWrite* g_option_writes;
static u32 g_option_write_count, g_option_write_capacity;
typedef struct WriteCapture {
    CPUState* cpu;
    u32 index;
} WriteCapture;

static bool option_on(u32 index) { return g_flags != NULL && index != 0u && g_flags[index] != 0u; }

static float f32_from_bits(u32 bits) {
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static void set_single(CPUState* cpu, u32 reg, float value) {
    // As lfs leaves it: the single's value in both halves.
    cpu->fpr[reg] = value;
    cpu->ps1[reg] = value;
}

static s32 truncate_to_s32(float value) {
    // fctiwz: toward zero, saturating.
    if (value != value)
        return (s32)0x80000000u;
    if (value >= 2147483647.0f)
        return 0x7FFFFFFF;
    if (value <= -2147483648.0f)
        return (s32)0x80000000u;
    return (s32)value;
}

// Turn while swinging (procRopeSwing, before the lfs the patch branched from):
// the stick's horizontal axis turns Link by up to the rope's base rotation
// speed a frame, ignoring the stick's first quarter. r31 is the player.
static u32 hook_swing_turn(CPUState* cpu) {
    const float stick = f32_from_bits(mem_read32(cpu, 0x803A4DF0u));
    const float base = f32_from_bits(mem_read32(cpu, 0x803FA2E8u));
    const s32 velocity = truncate_to_s32(base * stick);
    const s32 threshold = (s32)((u32)truncate_to_s32(base) >> 2);
    if (velocity >= threshold || velocity <= -threshold) {
        const u32 player = cpu->gpr[31];
        const u16 angle = (u16)(mem_read16(cpu, player + 0x20Eu) - (u32)velocity);
        mem_write16(cpu, player + 0x20Eu, angle);
        mem_write16(cpu, player + 0x206u, angle);
    }
    return 0u;
}

// Swift Sail's wind, after KoRL's sail sound call: the wind blows the way he
// faces, rounded to the nearest eighth of a turn, through the game's own
// dKyw_tact_wind_set(0, angle). The call is made by returning into it with the
// link register pointing back at this site; the second visit is the return.
static u32 hook_sail_wind(CPUState* cpu) {
    static bool returning;
    if (returning) {
        returning = false;
        return 0u;
    }
    const u32 korl = mem_read32(cpu, 0x803CA75Cu);
    if (korl == 0u)
        return 0u;
    const s32 facing = -(s32)(s16)mem_read16(cpu, korl + 0x206u) + 0x4000 + 0x1000;
    returning = true;
    cpu->gpr[3] = 0u;
    cpu->gpr[4] = (u32)facing & 0xFFFFE000u;
    cpu->lr = cpu->pc;
    return 0x8008A7D0u; // dKyw_tact_wind_set__Fss
}

// Skip the intro story with Start: in dScnOpen_c::execute, after the story's
// process ran a frame (0x80232C80, r31 the scene, r3 its process), Start on the
// story told after a new game starts (process name 14, fpcNm_OPEN_SCENE_e)
// sets the process to its last state (44), which the next instructions take
// as the story's end: the game's own fade into Outset (changeGameScene). The
// title screen's story (15) is already skipped by A, B or Start.
static atomic_bool g_intro_skip_start = true;

void bluewake_intro_skip_set(bool on) { atomic_store(&g_intro_skip_start, on); }

// The story's music is a stream: as the title screen's story does when it is
// skipped (dScnOpen_c::execute), the stream is stopped first (bgmStop(20)) and
// the scene ends once its buffer is free (mDoAud_isUsedHeapForStreamBuffer),
// or the next scene cannot load (the screen stayed black). The calls are made
// by returning into them with the link register at this site; r3 is the
// process again before the code here goes on.
enum { kIntroIdle, kIntroStopping, kIntroWaiting, kIntroChecking };
static int g_intro_step = kIntroIdle;

static u32 hook_intro_skip(CPUState* cpu) {
    enum {
        kSite = 0x80232C80u, kBgmStop = 0x802A4658u, kStreamHeapUsed = 0x80006DC0u,
        kPadTrig = 0x803A4DF0u + 0x33u, kTrigStart = 0x10u, kOpenScene = 14u, kStateEnd = 44,
    };
    const u32 scene = cpu->gpr[31];
    if (scene < 0x80000000u || scene >= 0x81800000u || mem_read16(cpu, scene + 0x8u) != kOpenScene) {
        g_intro_step = kIntroIdle;
        return 0u;
    }
    const u32 proc = mem_read32(cpu, scene + 0x1D0u); // dScnOpen_c::mpProc
    const s32 state = (s32)mem_read32(cpu, proc + 0x2B0u);
    switch (g_intro_step) {
    case kIntroIdle:
        if (!atomic_load(&g_intro_skip_start) || state < 5 || state >= kStateEnd ||
            (mem_read8(cpu, kPadTrig) & kTrigStart) == 0u)
            return 0u;
        fprintf(stderr, "[options] intro story skipped with Start (state %d)\n", (int)state);
        g_intro_step = kIntroStopping;
        cpu->gpr[3] = mem_read32(cpu, cpu->gpr[13] - 0x69D0u); // mDoAud_zelAudio_c's interface
        cpu->gpr[4] = 20u;
        cpu->gpr[5] = 0u;
        cpu->lr = kSite;
        return kBgmStop;
    case kIntroStopping: // back from bgmStop
        g_intro_step = kIntroWaiting;
        cpu->gpr[3] = proc;
        return 0u;
    case kIntroWaiting: // a later frame: is the stream's buffer still in use?
        g_intro_step = kIntroChecking;
        cpu->lr = kSite;
        return kStreamHeapUsed;
    default: // back from mDoAud_isUsedHeapForStreamBuffer
        if (cpu->gpr[3] == 0u) {
            mem_write32(cpu, proc + 0x2B0u, (u32)kStateEnd);
            g_intro_step = kIntroIdle;
        } else {
            g_intro_step = kIntroWaiting;
        }
        cpu->gpr[3] = proc;
        return 0u;
    }
}

// --- Better Wind Waker extras: wwrando's patches ---------------------------------
// asm/patches/hero_mode.asm, remove_low_health_beep_anim.asm and
// fix_vanilla_bugs.asm (wwrando, MIT). Addresses are those of GZLE01; the
// guest's gameinfo (g_dComIfG_gameInfo) is at 0x803C4C08.
enum { kGameInfo = 0x803C4C08u };

// Hero Mode: damage taken is doubled (Better Wind Waker's Gecko code and README;
// wwrando's own Hero Mode multiplies by 4).
#define BWW_HERO_DAMAGE_MULTIPLIER 2.0f
// A host value: a randomizer seed with hero mode sets wwrando's 4.0 (bluewake_game_options_set_hero_multiplier).
static float g_hero_multiplier = BWW_HERO_DAMAGE_MULTIPLIER;
void bluewake_game_options_set_hero_multiplier(float multiplier) { g_hero_multiplier = multiplier; }

// daPy_lk_c::setDamagePoint(f1): a negative amount is damage.
static u32 hook_hero_damage(CPUState* cpu) {
    const float amount = (float)cpu->fpr[1];
    if (amount < 0.0f)
        set_single(cpu, 1, amount * g_hero_multiplier);
    return 0u;
}

static s32 guest_s16(CPUState* cpu, u32 address) { return (s32)(s16)mem_read16(cpu, address); }

// processHeartGaugeSound, the one-heart alarm (life 3-4 quarters): none when the
// maximum is one heart (max life is in quarter hearts, a heart piece counted).
static u32 hook_beep_one_heart(CPUState* cpu) {
    static bool logged;
    if (!logged) { // once a session: the alarm site ran (proof the option is wired)
        logged = true;
        fprintf(stderr, "[options] no_low_hp_beep: one-heart alarm site (max life %d)\n", guest_s16(cpu, kGameInfo));
    }
    if (guest_s16(cpu, kGameInfo) < 8)
        return 0x802A3038u; // the "field_0x0040--" before the epilogue
    return 0u;
}

// checkRestHPAnime: r3 = 1 here. No animation until two quarter hearts are
// missing from the whole hearts of the maximum. (wwrando reads r4; on the
// branch from 0x80111ED0 r4 was not loaded, so the life is read here.)
static u32 hook_beep_rest_anim(CPUState* cpu) {
    const s32 life = guest_s16(cpu, kGameInfo + 2u);
    const s32 whole = guest_s16(cpu, kGameInfo) & ~3;
    static bool logged;
    if (!logged) {
        logged = true;
        fprintf(stderr, "[options] no_low_hp_beep: low-health animation site (life %d of %d)\n", life, whole);
    }
    if (whole - life < 2)
        cpu->gpr[3] = 0u;
    return 0u;
}

// Skull Hammer: can guard without a shield (wwrando's check_can_defend).
static bool can_defend(CPUState* cpu, u32 player) {
    return mem_read16(cpu, player + 0x3560u) == 0x33u || mem_read8(cpu, kGameInfo + 0xFu) != 0xFFu;
}

// Calls a guest function from a hook: the first visit enters it with the link
// register at the site, the second visit (its return) lets the site go on.
static u32 hook_fix_sub_bgm(CPUState* cpu) {
    static bool returning;
    if (returning) {
        returning = false;
        return 0u;
    }
    returning = true;
    cpu->gpr[3] = mem_read32(cpu, 0x803F7710u); // JAIZelBasic's instance
    cpu->lr = cpu->pc;
    return 0x802A4CDCu; // JAIZelBasic::subBgmStop
}

// dStage_playerInit's entry (r4 the room's player list: +4 the count, +8 the entries of 0x20, each's start point in
// the low byte of angle.z at +0x1D): a start point the list does not have is the game's JUT_ASSERT, an OSPanic that
// halts the game for good (PPCHalt) - the freezes after Ballad of Gales arrivals and other warps (2026-10-17: the
// arrival point 99 of a sea room on a layer whose list lacks it). Link starts at the list's first point instead.
static u32 hook_safe_spawn(CPUState* cpu) {
    enum { START_STAGE = 0x803C9D3Cu }; // play.mCurStage (dStage_startStage_c): name[8], point s16 +8, room +0xA, layer +0xB
    if (mem_read32(cpu, 0x803CA754u) != 0u) // dComIfGp_getPlayer(0): the function returns at once
        return 0u;
    const s16 point = (s16)mem_read16(cpu, START_STAGE + 8u);
    const u32 data = cpu->gpr[4];
    if (point < 0 || data < 0x80000000u || data >= 0x81800000u)
        return 0u;
    const u32 num = mem_read32(cpu, data + 4u), entries = mem_read32(cpu, data + 8u);
    if (num == 0u || num > 256u || entries < 0x80000000u || entries + 0x20u * num > 0x81800000u)
        return 0u;
    for (u32 i = 0; i < num; ++i)
        if (mem_read8(cpu, entries + 0x20u * i + 0x1Du) == (u8)point)
            return 0u;
    const u8 first = mem_read8(cpu, entries + 0x1Du);
    char stage[9] = {0};
    for (u32 i = 0; i < 8u; ++i)
        stage[i] = (char)mem_read8(cpu, START_STAGE + i);
    fprintf(stderr, "[stage] start point %d is not among the %u of %s room %d layer %d: Link starts at point %u\n",
            point, num, stage, (s8)mem_read8(cpu, START_STAGE + 0xAu), (s8)mem_read8(cpu, START_STAGE + 0xBu), first);
    mem_write16(cpu, START_STAGE + 8u, first);
    return 0u;
}

// Better Wind Waker extras: id 18 by the address of the site, 19 and up by id.
static u32 hook_fix(CPUState* cpu, u32 id) {
    switch (id == HOOK_BWW_DOL ? cpu->pc : id) {
    case 0x801102D0u:
        return hook_hero_damage(cpu);
    case 0x802A2FDCu:
        return hook_beep_one_heart(cpu);
    case 0x80111EE0u:
        return hook_beep_rest_anim(cpu);
    case 0x800F13A8u:
        // Helmaroc King's mask shards have no pointer in r30: hide the sight.
        return cpu->gpr[30] == 0u ? 0x800F13C0u : 0u;
    case 0x800C3420u:
        // The Deluxe Picto Box replaces the Picto Box on X, Y and Z.
        for (u32 i = 0; i < 3u; ++i)
            if (mem_read8(cpu, 0x803CA7DBu + i) == 0x23u)
                mem_write8(cpu, 0x803CA7DBu + i, 0x26u);
        return 0u;
    case HOOK_FIX_STALFOS: {
        // r3 the upper body, r31 the lower body: the lower body starts dying
        // to light arrows with the upper one.
        const u32 upper = cpu->gpr[3];
        if (upper != 0u && mem_read8(cpu, upper + 0x1FAEu) != 0u && mem_read8(cpu, cpu->gpr[31] + 0x1FAEu) == 0u)
            mem_write8(cpu, cpu->gpr[31] + 0x1FAEu, 1u);
        return 0u;
    }
    case HOOK_FIX_MINIBLIN: {
        // r29 the Miniblin: a single one (behaviour type not 0) sets its
        // switch when light arrows kill it.
        const u32 mini = cpu->gpr[29];
        if (mem_read8(cpu, mini + 0x2B4u) != 0u)
            mem_write8(cpu, mini + 0x995u, mem_read8(cpu, mini + 0x2B8u));
        return 0u;
    }
    case HOOK_FIX_POE_DEAD: {
        // Big_pow_down_check after fopAcM_SearchByID: a Poe is dead when its HP
        // is 0 or less, or when light arrows are killing it.
        const u32 poe = cpu->gpr[31];
        if ((s8)mem_read8(cpu, poe + 0x285u) <= 0 || mem_read8(cpu, poe + 0x88Au) > 0u)
            return 0u;
        cpu->gpr[3] = 0u;
        return cpu->pc + 0x97Cu - 0x904u; // the epilogue, with r3 = 0
    }
    case HOOK_FIX_POE_UNKILL:
        mem_write8(cpu, cpu->gpr[31] + 0x88Au, 0u);
        return 0u;
    case HOOK_FIX_MAGTAIL:
        // r3 the hit object: light arrows on a respawning Magtail's head.
        if ((mem_read32(cpu, cpu->gpr[3] + 0x10u) & 0x00100000u) != 0u)
            mem_write8(cpu, cpu->gpr[30] + 0x1CBCu, 1u);
        return 0u;
    case HOOK_FIX_PHANTOM_GANON: {
        // The fight starts only when Link is less than 1000 above Phantom Ganon.
        const float dy = f32_from_bits(mem_read32(cpu, cpu->gpr[1] + 0x1Cu));
        const float limit = f32_from_bits(mem_read32(cpu, cpu->gpr[31] + 0x88u));
        set_single(cpu, 4, dy);
        set_single(cpu, 3, limit);
        if (dy >= limit)
            return cpu->pc + 0x5204u - 0x4F50u;
        return 0u;
    }
    case 0x80235340u:
        return hook_fix_sub_bgm(cpu);
    case 0x80041AF4u:
        return hook_safe_spawn(cpu);
    case 0x800D6194u:
        mem_write32(cpu, cpu->gpr[31] + 0x3F4u, 0u); // dCcD_GObjAt's mpCallback
        return 0u;
    case 0x8010E288u:
        return can_defend(cpu, cpu->gpr[31]) ? 0x8010E29Cu : 0x8010E2A4u;
    case 0x8010E504u:
        return can_defend(cpu, cpu->gpr[31]) ? 0x8010E518u : 0x8010E554u;
    case 0x800D4EBCu:
        // r3 Ganondorf: reflecting Zelda's light arrow starts the camera event
        // only from phase 3 (25 HP or less).
        return (s8)mem_read8(cpu, cpu->gpr[3] + 0x285u) > 25 ? 0x800D4F68u : 0u;
    case HOOK_FIX_BDK_LAND_INIT:
        mem_write16(cpu, cpu->gpr[30] + 0xB0u, 0u); // the landing timer, in the unused half of params
        return 0u;
    case HOOK_FIX_BDK_LAND_TIMEOUT: {
        const u32 king = cpu->gpr[30];
        const s32 sub = guest_s16(cpu, king + 0x2C8u);
        if (sub > 0 && sub < 3) {
            const s32 timer = guest_s16(cpu, king + 0xB0u) + 1;
            mem_write16(cpu, king + 0xB0u, (u32)timer);
            if (timer > 10 * 30) { // 10 seconds trying to land: fly up and try again
                mem_write16(cpu, king + 0x2C6u, 1u);
                mem_write16(cpu, king + 0x2C8u, 0u);
            }
        }
        return 0u;
    }
    default:
        return 0u;
    }
}

static void capture_write(void* user, u32 address, const u8* bytes, u32 size) {
    WriteCapture* capture = user;
    if (g_option_write_count == g_option_write_capacity) {
        const u32 capacity = g_option_write_capacity != 0u ? g_option_write_capacity * 2u : 64u;
        OptionWrite* grown = realloc(g_option_writes, capacity * sizeof *grown);
        if (grown == NULL)
            return;
        g_option_writes = grown;
        g_option_write_capacity = capacity;
    }
    OptionWrite* write = &g_option_writes[g_option_write_count];
    write->index = capture->index;
    write->address = address;
    write->size = size;
    write->now = malloc(size);
    write->before = malloc(size);
    if (write->now == NULL || write->before == NULL) {
        free(write->now);
        free(write->before);
        return;
    }
    memcpy(write->now, bytes, size);
    for (u32 i = 0; i < size; ++i)
        write->before[i] = mem_read8(capture->cpu, address + i);
    ++g_option_write_count;
}

static void put_bytes(CPUState* cpu, u32 address, const u8* bytes, u32 size) {
    // A word at once where it is one: the game never sees half of a float.
    if (size == 4u && (address & 3u) == 0u)
        mem_write32(cpu, address, ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) | ((u32)bytes[2] << 8) | bytes[3]);
    else if (size == 2u && (address & 1u) == 0u)
        mem_write16(cpu, address, (u16)(((u32)bytes[0] << 8) | bytes[1]));
    else
        for (u32 i = 0; i < size; ++i)
            mem_write8(cpu, address + i, bytes[i]);
}

// The option's written values on (its own) or off (what the memory held).
static u32 apply_option_writes(u32 index, bool on) {
    u32 count = 0u;
    for (u32 i = 0; i < g_option_write_count; ++i) {
        const OptionWrite* write = &g_option_writes[i];
        if (write->index != index)
            continue;
        put_bytes(g_cpu, write->address, on ? write->now : write->before, write->size);
        ++count;
    }
    return count;
}

// Before the boot writes: each option's values, one option at a time (the
// module writes those of the options whose flag is on).
static void capture_option_writes(WritesFn writes, CPUState* cpu, u32 options) {
    unsigned char saved[256];
    memcpy(saved, (const void*)g_flags, sizeof saved);
    for (u32 i = 0; i < options; ++i) {
        u32 index = 0u, default_on = 0u;
        if (g_option(i, &index, &default_on, NULL) == NULL || index == 0u || index > 255u)
            continue;
        memset((void*)g_flags, 0, sizeof saved);
        g_flags[index] = 1u;
        WriteCapture capture = {cpu, index};
        writes(capture_write, &capture);
    }
    memcpy((void*)g_flags, saved, sizeof saved);
}

// fopAcM_SearchByID(fpc_ProcID id, fopAc_ac_c** out) at its entry, answered as it answers and returned from: an id
// still being created gives NULL and TRUE (fpcM_IsCreating, the creating queue g_fpcCtTg_Queue: each node's request at
// +0xC, its process id at +0x3C); else the actor of that id in g_fopAcTg_Queue (each node's process at +0xC, its id at
// +4) and TRUE, or NULL and FALSE. -1 (Link holds nothing) is no process's. The queues are walked here in C instead of
// through cLsIt_Judge's two indirect calls a node.
static u32 hook_search_by_id(CPUState* cpu) {
    enum { CREATE_QUEUE = 0x80372690u, ACTOR_QUEUE = 0x80372028u };
    const u32 id = cpu->gpr[3], out = cpu->gpr[4];
    if (out < 0x80000000u || out >= 0x81800000u || (out & 3u) != 0u)
        return 0u; // not as the game calls it: the translation answers
    bool creating = false;
    u32 found = 0u;
    if (id != 0xFFFFFFFFu) {
        u32 n = mem_read32(cpu, CREATE_QUEUE);
        for (u32 k = 0; k < 4096u && n >= 0x80000000u && n < 0x81800000u; ++k, n = mem_read32(cpu, n + 8u)) {
            const u32 req = mem_read32(cpu, n + 0xCu);
            if (req >= 0x80000000u && req < 0x81800000u && mem_read32(cpu, req + 0x3Cu) == id) {
                creating = true;
                break;
            }
        }
        n = creating ? 0u : mem_read32(cpu, ACTOR_QUEUE);
        for (u32 k = 0; k < 8192u && n >= 0x80000000u && n < 0x81800000u; ++k, n = mem_read32(cpu, n + 8u)) {
            const u32 proc = mem_read32(cpu, n + 0xCu);
            if (proc >= 0x80000000u && proc < 0x81800000u && mem_read32(cpu, proc + 4u) == id) {
                found = proc;
                break;
            }
        }
    }
    mem_write32(cpu, out, found);
    cpu->gpr[3] = (creating || found != 0u) ? 1u : 0u;
    return cpu->lr;
}

// fopAcIt_Judge(judge, data) at its entry (cLsIt_Judge over g_fopAcTg_Queue with cTgIt_JudgeFilter): when the judge
// is fpcSch_JudgeByID (data the id), the actor with that id, or NULL, found in C. The inline one-argument
// fopAcM_SearchByID comes here, not through the two-argument function: dAttention_c::calcWeight's search for Link's
// grabbed actor for every actor, twice a frame (7% of the game thread in Windfall's crowd, BLUEWAKE_PROF 2026-10-17).
static u32 hook_judge_by_id(CPUState* cpu) {
    enum { JUDGE_BY_ID = 0x80040068u, ACTOR_QUEUE = 0x80372028u };
    const u32 data = cpu->gpr[4];
    if (cpu->gpr[3] != JUDGE_BY_ID || data < 0x80000000u || data >= 0x81800000u || (data & 3u) != 0u)
        return 0u; // another judge: the translation walks with it
    const u32 id = mem_read32(cpu, data);
    u32 found = 0u;
    if (id != 0xFFFFFFFFu) {
        u32 n = mem_read32(cpu, ACTOR_QUEUE);
        for (u32 k = 0; k < 8192u && n >= 0x80000000u && n < 0x81800000u; ++k, n = mem_read32(cpu, n + 8u)) {
            const u32 proc = mem_read32(cpu, n + 0xCu);
            if (proc >= 0x80000000u && proc < 0x81800000u && mem_read32(cpu, proc + 4u) == id) {
                found = proc;
                break;
            }
        }
    }
    cpu->gpr[3] = found;
    return cpu->lr;
}

static atomic_bool g_console_reset_requested;

// SelectThread's idle loop, `while (RunQueueBits == 0);` with interrupts on: only an interrupt can give it a thread to
// run, and every interrupt source is a deadline of the cycle domain (VI, DSP, audio, the GBA's transfer, the
// decrementer). So the turn is charged up to the nearest deadline at once and the loop is left; the dispatcher then
// delivers what is due and comes back here if nothing woke. BLUEWAKE_IDLE_SKIP=0 runs the loop as before.
// [idle] counts (BLUEWAKE_PERF_LOG, every 10 s): calls, skips, cycles skipped, and why a call did not skip.
static unsigned long long g_idle_calls, g_idle_skips, g_idle_cycles, g_idle_no[4];
static u32 hook_idle_skip(CPUState* cpu) {
    ++g_idle_calls;
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("BLUEWAKE_IDLE_SKIP");
        on = !(e != NULL && e[0] == '0');
        fprintf(stderr, "[idle] the idle loop skip is %s\n", on ? "on" : "off (BLUEWAKE_IDLE_SKIP=0)");
    }
    if (!on)
        return 0u;
    if ((cpu->msr & 0x8000u) == 0u || mem_read32(cpu, cpu->gpr[13] - 26288u) != 0u) {
        ++g_idle_no[0];
        return 0u;
    }
    const s64 deadline = cpu->cycle_deadline_budget; // cycles from the turn's start to the nearest deadline; 0 = none
    if (deadline <= 0) {
        ++g_idle_no[1];
        return 0u;
    }
    const s64 target = deadline < 8000000 ? deadline : 8000000; // at most about a retrace at once
    if (-cpu->downcount >= target) {
        ++g_idle_no[2];
        return 0u;
    }
    ++g_idle_skips;
    g_idle_cycles += (unsigned long long)(target + cpu->downcount);
    cpu->downcount = -target;
    return 0x80307EF4u;
}
bool bluewake_host_reset_requested(void) { return atomic_load(&g_console_reset_requested); }

static u32 native_hook(CPUState* cpu, u32 id) {
    // The first call of each hook is logged: it shows a setting's code ran.
    static u64 seen;
    if (id < 64u && (seen & (1ull << id)) == 0u) {
        seen |= 1ull << id;
        fprintf(stderr, "[options] hook %u first ran at pc=0x%08X\n", id, cpu->pc);
    }
    switch (id) {
    case HOOK_SEARCH_BY_ID:
        return hook_search_by_id(cpu);
    case HOOK_JUDGE_BY_ID:
        return hook_judge_by_id(cpu);
    case HOOK_OS_RESET_SYSTEM:
        // OSResetSystem(reset, resetCode, forceMenu): the app starts the game again (bluewake_host_reset_requested);
        // the guest waits in PPCHalt meanwhile.
        fprintf(stderr, "[reset] the game asked for a console reset (OSResetSystem %u, code 0x%08X, lr=0x%08X): restarting\n",
                cpu->gpr[3], cpu->gpr[4], cpu->lr);
        atomic_store(&g_console_reset_requested, true);
        return 0x8030150Cu; // PPCHalt
    case HOOK_IDLE_SKIP:
        return hook_idle_skip(cpu);
    case HOOK_DVD_CHECK_DISK:
        cpu->gpr[3] = 1u; // DVDCheckDisk: TRUE, a disc is in
        return cpu->lr;
    case HOOK_SWITCH_ON:
    case HOOK_SWITCH_OFF:
        return bluewake_switch_sync_hook(cpu, id == HOOK_SWITCH_ON);
    case HOOK_SWING_TURN:
        return hook_swing_turn(cpu);
    case HOOK_INVERT_CAMERA_X:
        if (getenv("BLUEWAKE_PVP_TEST_SITE") != NULL) {
            // Testing with a module built before hook 51 (dScnPly_Execute): this site, which the camera runs each frame in
            // the play scene's execute phase, stands in for it (BLUEWAKE_OPTIONS has invert_camera_x).
            bluewake_pvp_test_execute(cpu);
            return 0u;
        }
        // After the C-stick's horizontal axis is loaded for the camera.
        cpu->fpr[1] = -cpu->fpr[1];
        return 0u;
    case HOOK_SAIL_WIND:
        return hook_sail_wind(cpu);
    case HOOK_SAIL_BRAKE:
        // cLib_addCalc's largest and smallest step when A stops the boat
        // (1.0 and 0.1 in the game).
        set_single(cpu, 3, option_on(g_brisk_sail) ? 3.0f : 2.0f);
        set_single(cpu, 4, 0.2f);
        return 0u;
    case HOOK_SAIL_IDLE_BRAKE:
        // ... and when Link is knocked off a moving boat (1.0 and 0.05).
        set_single(cpu, 3, option_on(g_brisk_sail) ? 4.0f : 2.0f);
        set_single(cpu, 4, option_on(g_brisk_sail) ? 0.2f : 0.1f);
        return 0u;
    case HOOK_GX_ARRAY_SOURCE:
        // Entry of GXSetArray (r3 the attribute, r4 the pointer): tells the renderer where the array
        // came from in full, as the CP register keeps only the physical address.
        dol_gx_note_array_source(cpu->gpr[3], cpu->gpr[4]);
        return 0u;
    case HOOK_GX_IMAGE_SOURCE:
        // Entry of GXInitTexObj, GXInitTexObjCI and GXInitTlutObj (r4 the image or palette pointer): the same for
        // a texture a module keeps in its own data (the flags' pictures).
        dol_gx_note_image_source(cpu->gpr[4]);
        return 0u;
    case BLUEWAKE_PLAYER_MODEL_HOOK:
        return bluewake_player_model_hook(cpu);
    case BLUEWAKE_INTRO_SKIP_HOOK:
        return hook_intro_skip(cpu);
    case BLUEWAKE_TREASURE_GLOW_HOOK:
        return bluewake_treasure_glow_hook(cpu);
    case BLUEWAKE_VOICE_PITCH_HOOK:
        // Link Voice Pitch: linkVoiceStart's seStart call, f2 (the pitch) just copied from the 1.0 in f1.
        return bluewake_voice_pitch_apply(cpu);
    case BLUEWAKE_PUPPET_HOOK_DRAW:
        // The same site draws the Bunny Hood: whichever has a guest call in flight goes on first.
        // The autosave's guest calls are made from it too.
        if (bluewake_bunny_hood_draw_active())
            return bluewake_bunny_hood_draw_hook(cpu, 0x80234F5Cu);
        if (bluewake_autosave_active())
            return bluewake_autosave_hook(cpu, 0x80234F5Cu);
        {
            u32 next = bluewake_puppet_hook(cpu, id);
            if (next == 0u)
                next = bluewake_bunny_hood_draw_hook(cpu, 0x80234F5Cu);
            return next != 0u ? next : bluewake_autosave_hook(cpu, 0x80234F5Cu);
        }
    case BLUEWAKE_PVP_HOOK_EXECUTE:
        return bluewake_pvp_execute_hook(cpu);
    case BLUEWAKE_PUPPET_HOOK_DELETE:
        if (bluewake_bunny_hood_delete_active())
            return bluewake_bunny_hood_delete_hook(cpu, 0x802350BCu);
        {
            const u32 next = bluewake_puppet_hook(cpu, id);
            return next != 0u ? next : bluewake_bunny_hood_delete_hook(cpu, 0x802350BCu);
        }
    case BLUEWAKE_RANDO_UI_HOOK:
        return bluewake_rando_ui_hook(cpu, id);
    case BLUEWAKE_BUNNY_HOOK_ITEM_GET:
    case BLUEWAKE_BUNNY_HOOK_SPEED:
    case BLUEWAKE_BUNNY_HOOK_MENU:
    case BLUEWAKE_BUNNY_HOOK_MENU_END:
        return bluewake_bunny_hood_hook(cpu, id);
    default:
        if (bluewake_rando_is_hook(id))
            return bluewake_rando_hook(cpu, id);
        if (id >= HOOK_BWW_DOL && id <= HOOK_FIX_LAST)
            return hook_fix(cpu, id);
        if ((id >= BLUEWAKE_WIDESCREEN_HOOK_FIRST && id <= BLUEWAKE_WIDESCREEN_HOOK_LAST) ||
            id == BLUEWAKE_WIDESCREEN_CLIP_HOOK || id == BLUEWAKE_WIDESCREEN_CLOTH_HOOK ||
            id == BLUEWAKE_WIDESCREEN_SCISSOR_HOOK)
            return bluewake_widescreen_native_hook(cpu, id);
        return 0u;
    }
}

static void write_guest(void* user, u32 address, const u8* bytes, u32 size) {
    for (u32 i = 0; i < size; ++i)
        mem_write8((CPUState*)user, address + i, bytes[i]);
}

static bool named(const char* list, const char* name, bool* on) {
    const size_t len = strlen(name);
    bool found = false;
    for (const char* p = list; p != NULL && *p != '\0';) {
        const char* end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        bool value = true;
        const char* word = p;
        if (n > 0 && *word == '-') {
            value = false;
            ++word;
            --n;
        }
        if (n == len && strncmp(word, name, n) == 0) {
            *on = value;
            found = true;
        }
        p = end ? end + 1 : NULL;
    }
    return found;
}

void bluewake_game_options_enable(void* lib, CPUState* cpu, bool mod_enabled) {
    typedef volatile unsigned char* (*FlagsFn)(void);
    typedef void (*SetHookFn)(NativeHookFn);
    typedef u32 (*CountFn)(void);
    FlagsFn flags = (FlagsFn)dlsym(lib, "bluewake_composite_option_flags");
    SetHookFn set_hook = (SetHookFn)dlsym(lib, "bluewake_composite_set_native_hook");
    CountFn count = (CountFn)dlsym(lib, "bluewake_composite_option_count");
    OptionFn option = (OptionFn)dlsym(lib, "bluewake_composite_option");
    WritesFn writes = (WritesFn)dlsym(lib, "bluewake_composite_option_writes");
    const u32 options = count ? count() : 0u;
    if (flags == NULL || set_hook == NULL || option == NULL || options == 0u)
        return;
    g_flags = flags();
    g_option = option;
    g_cpu = cpu;
    set_hook(native_hook);

    const char* wanted = getenv("BLUEWAKE_OPTIONS");
    const bool none = wanted != NULL && strncmp(wanted, "none", 4) == 0;
    u32 swift = 0u;
    char list[768] = "";
    for (u32 i = 0; i < options; ++i) {
        u32 index = 0u, default_on = 0u;
        const char* name = option(i, &index, &default_on, NULL);
        bool on = default_on != 0u && !none;
        named(wanted, name, &on);
        // Better Wind Waker's switches (1-15) need its setting; the port's own
        // options (16 and up: widescreen culling, the HUD at the edges) do not.
        if (index < 16u)
            on = on && mod_enabled;
        if (strcmp(name, "instant_text") == 0)
            g_instant_text = index;
        else if (strcmp(name, "brisk_sail") == 0)
            g_brisk_sail = index;
        else if (strcmp(name, "invert_camera_x") == 0)
            g_invert_camera_x = index;
        else if (strcmp(name, "swift_sail") == 0)
            swift = g_swift_sail = index;
        else if (strcmp(name, "hud_edges") == 0)
            g_hud_edges = index;
        g_flags[index] = on ? 1u : 0u;
        if (on)
            snprintf(list + strlen(list), sizeof list - strlen(list), "%s%s", list[0] ? "," : "", name);
    }
    // Brisk Sail is Swift Sail with harder braking: it needs Swift Sail's sites.
    if (option_on(g_brisk_sail) && swift != 0u)
        g_flags[swift] = 1u;
    if (writes != NULL)
        capture_option_writes(writes, cpu, options);
    const u32 written = writes ? writes(write_guest, cpu) : 0u;
    fprintf(stderr, "[options] %s (%u values written)\n", list[0] ? list : "none", written);
}

bool bluewake_game_options_invert_camera_x(void) { return option_on(g_invert_camera_x); }

bool bluewake_game_options_hud_edges(void) { return option_on(g_hud_edges); }

// The values some options write at boot (options.txt) follow the switch while
// the game runs: Hero Mode's drop table and the manual camera's limits, which
// the game reads every frame.
bool bluewake_game_options_set(const char* name, bool on) {
    if (g_flags == NULL || g_option == NULL || name == NULL)
        return false;
    for (u32 i = 0;; ++i) {
        u32 index = 0u, defaults = 0u;
        const char* option = g_option(i, &index, &defaults, NULL);
        if (option == NULL)
            return false;
        if (strcmp(option, name) == 0 && index != 0u) {
            const bool changed = (g_flags[index] != 0u) != on;
            g_flags[index] = on ? 1u : 0u;
            if (changed) {
                const u32 written = apply_option_writes(index, on);
                fprintf(stderr, "[options] %s %s (%u values %s)\n", name, on ? "on" : "off", written,
                        on ? "written" : "put back");
            }
            return true;
        }
    }
}

// Whether the game module has an option of that name (a module built before it does not).
bool bluewake_game_options_has(const char* name) {
    if (g_option == NULL || name == NULL)
        return false;
    for (u32 i = 0;; ++i) {
        u32 index = 0u, defaults = 0u;
        const char* option = g_option(i, &index, &defaults, NULL);
        if (option == NULL)
            return false;
        if (strcmp(option, name) == 0)
            return true;
    }
}

bool bluewake_game_options_get(const char* name) {
    if (g_flags == NULL || g_option == NULL || name == NULL)
        return false;
    for (u32 i = 0;; ++i) {
        u32 index = 0u, defaults = 0u;
        const char* option = g_option(i, &index, &defaults, NULL);
        if (option == NULL)
            return false;
        if (strcmp(option, name) == 0 && index != 0u)
            return g_flags[index] != 0u;
    }
}

const char* bluewake_game_options_describe(u32 position, const char** title, bool* default_on, bool* on) {
    u32 index = 0u, defaults = 0u;
    const char* text = NULL;
    const char* name = g_option != NULL ? g_option(position, &index, &defaults, &text) : NULL;
    if (name == NULL)
        return NULL;
    if (title)
        *title = text;
    if (default_on)
        *default_on = defaults != 0u;
    if (on)
        *on = option_on(index);
    return name;
}

// Instant text: what Better Wind Waker does to the message data on the disc,
// done to the copy the game has loaded. Every message draws its whole box at
// once (initial draw type 1), and its timed waits (1A 07 00 00 07 nn nn) and
// timed waits before a prompt (1A 07 00 00 03 nn nn) wait no time.
// Every copy patched is kept with what it changed (offset, original byte,
// patched byte), so turning the setting off puts the messages back: the
// journal is only applied while the memory still holds exactly the patched
// bytes, so a message file loaded over the copy since is never touched.
typedef struct BmgChange {
    u32 offset;
    u8 before, after;
} BmgChange;
typedef struct BmgCopy {
    u32 address;
    BmgChange* changes;
    u32 count, capacity;
} BmgCopy;
enum { BMG_COPIES = 8 };
static BmgCopy g_bmg_copies[BMG_COPIES];
static u32 g_bmg_copy_count;
static u64 g_retrace;
static bool g_instant_applied; // the setting as the messages were last treated

typedef struct BmgGuestWrite {
    CPUState* cpu;
    BmgCopy* copy;
} BmgGuestWrite;

static void write_bmg_byte(void* user, size_t offset, uint8_t value) {
    BmgGuestWrite* write = user;
    BmgCopy* copy = write->copy;
    const u32 address = copy->address + (u32)offset;
    if (copy->count == copy->capacity) {
        const u32 capacity = copy->capacity != 0u ? copy->capacity * 2u : 4096u;
        BmgChange* grown = realloc(copy->changes, capacity * sizeof *grown);
        if (grown == NULL) {
            copy->capacity = copy->count; // cannot journal: still patch, the restore stays partial
            mem_write8(write->cpu, address, value);
            return;
        }
        copy->changes = grown;
        copy->capacity = capacity;
    }
    copy->changes[copy->count++] = (BmgChange){(u32)offset, mem_read8(write->cpu, address), value};
    mem_write8(write->cpu, address, value);
}

static bool bmg_copy_intact(CPUState* cpu, const BmgCopy* copy) {
    if (mem_read32(cpu, copy->address) != 0x4D455347u || mem_read32(cpu, copy->address + 4u) != 0x626D6731u)
        return false;
    // A message file loaded over the copy at the same address is not patched.
    return copy->count == 0u ||
           (mem_read8(cpu, copy->address + copy->changes[0].offset) == copy->changes[0].after &&
            mem_read8(cpu, copy->address + copy->changes[copy->count - 1u].offset) ==
                copy->changes[copy->count - 1u].after);
}

static void bmg_copy_forget(BmgCopy* copy) {
    free(copy->changes);
    *copy = (BmgCopy){0};
}

static void bmg_copy_remove(u32 index) {
    bmg_copy_forget(&g_bmg_copies[index]);
    g_bmg_copies[index] = g_bmg_copies[--g_bmg_copy_count];
    g_bmg_copies[g_bmg_copy_count] = (BmgCopy){0};
}

static bool patch_bmg(CPUState* cpu, u32 bmg, u32* messages_out, u32* waits_out) {
    const u32 offset = bmg - 0x80000000u;
    if (cpu == NULL || cpu->ram == NULL || offset >= cpu->ram_size || g_bmg_copy_count >= BMG_COPIES)
        return false;
    BmgCopy* copy = &g_bmg_copies[g_bmg_copy_count];
    *copy = (BmgCopy){.address = bmg};
    BmgGuestWrite write = {cpu, copy};
    if (!bluewake_bmg_patch(cpu->ram + offset, cpu->ram_size - offset, write_bmg_byte, &write, messages_out,
                            waits_out)) {
        bmg_copy_forget(copy);
        return false;
    }
    ++g_bmg_copy_count;
    return true;
}

// Puts one patched copy back. False when the memory no longer holds what the
// patch wrote (another message file was loaded there).
static bool unpatch_bmg(CPUState* cpu, const BmgCopy* copy) {
    if (!bmg_copy_intact(cpu, copy))
        return false;
    for (u32 i = 0; i < copy->count; ++i)
        if (mem_read8(cpu, copy->address + copy->changes[i].offset) != copy->changes[i].after)
            return false;
    for (u32 i = copy->count; i-- > 0;)
        mem_write8(cpu, copy->address + copy->changes[i].offset, copy->changes[i].before);
    return true;
}

static u32 bmg_bytes_changed(const BmgCopy* copy) {
    u32 n = 0u;
    for (u32 i = 0; i < copy->count; ++i)
        n += copy->changes[i].before != copy->changes[i].after;
    return n;
}

static bool bmg_copy_known(u32 address) {
    for (u32 i = 0; i < g_bmg_copy_count; ++i)
        if (g_bmg_copies[i].address == address)
            return true;
    return false;
}

static void instant_text_retrace(CPUState* cpu) {
    if (cpu->ram == NULL)
        return;
    const bool on = option_on(g_instant_text);
    if (!on) {
        // Turned off: every message file patched goes back as it was loaded.
        if (g_instant_applied || g_bmg_copy_count != 0u) {
            u32 restored = 0u, lost = 0u, bytes = 0u;
            for (u32 i = g_bmg_copy_count; i-- > 0;) {
                const u32 changed = bmg_bytes_changed(&g_bmg_copies[i]);
                if (unpatch_bmg(cpu, &g_bmg_copies[i])) {
                    ++restored;
                    bytes += changed;
                }
                else
                    ++lost;
                bmg_copy_remove(i);
            }
            fprintf(stderr, "[options] instant text off: %u message files put back, %u bytes restored (%u had been replaced)\n",
                    restored, bytes, lost);
        }
        g_instant_applied = false;
        return;
    }
    // On: copies that were freed or replaced are forgotten (they are not ours
    // to touch any more); then, once a second or at once after turning it on,
    // look for message files loaded since.
    for (u32 i = g_bmg_copy_count; i-- > 0;)
        if (!bmg_copy_intact(cpu, &g_bmg_copies[i]))
            bmg_copy_remove(i);
    const bool first = !g_instant_applied;
    g_instant_applied = true;
    ++g_retrace;
    if (!first && (g_retrace % 60u) != 0u)
        return;
    if (!first && g_bmg_copy_count != 0u)
        return;
    // Not found yet, or the copy moved: find the loaded message data
    // ("MESGbmg1") and patch every copy.
    static u32 scans;
    if (++scans == 1u || scans % 60u == 0u || first)
        fprintf(stderr, "[options] instant text: looking for the messages (scan %u, %u bytes)\n", scans,
                cpu->ram_size);
    const u8* ram = cpu->ram;
    for (u32 offset = 0; offset + 8u <= cpu->ram_size;) {
        const u8* hit = memmem(ram + offset, cpu->ram_size - offset, "MESGbmg1", 8);
        if (hit == NULL)
            break;
        const u32 address = 0x80000000u + (u32)(hit - ram);
        u32 messages = 0u, waits = 0u;
        if (!bmg_copy_known(address) && patch_bmg(cpu, address, &messages, &waits))
            fprintf(stderr, "[options] instant text: %u messages at 0x%08X, %u timed waits removed, %u bytes changed\n",
                    messages, address, waits, bmg_bytes_changed(&g_bmg_copies[g_bmg_copy_count - 1u]));
        offset = (u32)(hit - ram) + 8u;
    }
}

// Swift Sail and Brisk Sail: the Sail item's name reads "Swift Sail" or "Brisk Sail" while the option is on.
// The name is message 0x1CF of the game's zel_00.bmg ("Sail", on the pause menu's item screen); the item-get
// text (message 221, "You got a boat's sail!") carries the name in eleven letters, the length of both new
// names. The message data has no room for a longer name, so the entry's offset points at the file header's 15
// unused bytes, where the new name is written (a pointer the game forms as data + offset, which wraps to
// there). Every byte is journaled as the instant-text patch's are, so turning the option off, or loading
// another message file over the copy, puts everything back.
enum { SAIL_NAME_MESSAGE = 0x1CF, SAIL_GET_MESSAGE = 221, BMG_HEADER_PAD = 0x11, BMG_HEADER_PAD_SIZE = 15 };
static BmgCopy g_sail_copies[BMG_COPIES];
static u32 g_sail_copy_count;
static int g_sail_mode; // 0 vanilla, 1 Swift, 2 Brisk: what the copies in g_sail_copies were patched to
static u64 g_sail_retrace;

static int sail_wanted(void) {
    if (option_on(g_brisk_sail))
        return 2;
    return option_on(g_swift_sail) ? 1 : 0;
}

static void sail_put(CPUState* cpu, BmgCopy* copy, u32 offset, u8 value) {
    BmgGuestWrite write = {cpu, copy};
    write_bmg_byte(&write, offset, value);
}

// The entry of a message in the copy's INF1 (its address in guest memory), 0 if none.
static u32 sail_entry(CPUState* cpu, u32 bmg, u32 inf, u32 entries, u32 entry_size, u16 message) {
    for (u32 i = 0u; i < entries; ++i) {
        const u32 entry = inf + 16u + i * entry_size;
        if (mem_read16(cpu, entry + 4u) == message)
            return entry;
    }
    (void)bmg;
    return 0u;
}

static bool sail_patch_copy(CPUState* cpu, u32 bmg, int mode) {
    if (g_sail_copy_count >= BMG_COPIES || bmg + 64u >= 0x80000000u + cpu->ram_size)
        return false;
    const u32 blocks = mem_read32(cpu, bmg + 8u), sections = mem_read32(cpu, bmg + 12u);
    if (blocks < 2u || blocks > 0x00400000u / 32u || sections == 0u || sections > 16u ||
        bmg + blocks * 32u > 0x80000000u + cpu->ram_size)
        return false;
    u32 inf = 0u, dat = 0u, at = bmg + 32u;
    for (u32 i = 0u; i < sections; ++i) {
        const u32 magic = mem_read32(cpu, at), size = mem_read32(cpu, at + 4u);
        if (size < 8u || at + size > bmg + blocks * 32u)
            return false;
        if (magic == 0x494E4631u)
            inf = at;
        else if (magic == 0x44415431u)
            dat = at;
        at += size;
    }
    if (inf == 0u || dat == 0u)
        return false;
    const u32 entries = mem_read16(cpu, inf + 8u), entry_size = mem_read16(cpu, inf + 10u);
    if (entry_size < 14u || entries == 0u)
        return false;
    const u32 name_entry = sail_entry(cpu, bmg, inf, entries, entry_size, SAIL_NAME_MESSAGE);
    if (name_entry == 0u)
        return false;
    const u32 name_text = dat + 8u + mem_read32(cpu, name_entry);
    if (mem_read32(cpu, name_text) != 0x5361696Cu /* "Sail" */ || mem_read8(cpu, name_text + 4u) != 0u)
        return false; // not the English item names
    for (u32 i = 0u; i < BMG_HEADER_PAD_SIZE; ++i)
        if (mem_read8(cpu, bmg + BMG_HEADER_PAD + i) != 0u)
            return false;
    BmgCopy* copy = &g_sail_copies[g_sail_copy_count];
    *copy = (BmgCopy){.address = bmg};
    const char* name = mode == 2 ? "Brisk Sail" : "Swift Sail";
    for (u32 i = 0u; i < 11u; ++i)
        sail_put(cpu, copy, BMG_HEADER_PAD + i, (u8)name[i]);
    const u32 offset = (bmg + BMG_HEADER_PAD) - (dat + 8u); // wraps: the pad lies before the text
    for (u32 i = 0u; i < 4u; ++i)
        sail_put(cpu, copy, name_entry - bmg + i, (u8)(offset >> (24u - 8u * i)));
    // The item-get message: "boat's sail" (eleven letters) becomes the name and a space.
    const u32 get_entry = sail_entry(cpu, bmg, inf, entries, entry_size, SAIL_GET_MESSAGE);
    if (get_entry != 0u) {
        const u32 text = dat + 8u + mem_read32(cpu, get_entry);
        for (u32 p = text; p < text + 400u && mem_read8(cpu, p) != 0u; ++p) {
            static const char vanilla[] = "boat's sail";
            u32 k = 0u;
            while (k < 11u && mem_read8(cpu, p + k) == (u8)vanilla[k])
                ++k;
            if (k == 11u) {
                for (u32 j = 0u; j < 10u; ++j)
                    sail_put(cpu, copy, p - bmg + j, (u8)name[j]);
                sail_put(cpu, copy, p - bmg + 10u, (u8)' ');
                break;
            }
        }
    }
    ++g_sail_copy_count;
    fprintf(stderr, "[options] sail name: \"%s\" in the message file at 0x%08X (%u bytes changed)\n", name, bmg,
            bmg_bytes_changed(copy));
    return true;
}

static void sail_forget(u32 index) {
    bmg_copy_forget(&g_sail_copies[index]);
    g_sail_copies[index] = g_sail_copies[--g_sail_copy_count];
    g_sail_copies[g_sail_copy_count] = (BmgCopy){0};
}

static void sail_name_retrace(CPUState* cpu) {
    if (cpu->ram == NULL)
        return;
    const int want = sail_wanted();
    const bool changed = want != g_sail_mode;
    if (!changed && (want == 0 || ++g_sail_retrace % 60u != 0u))
        return;
    if (changed) {
        // Turned off or changed: every copy goes back as it was.
        for (u32 i = g_sail_copy_count; i-- > 0;) {
            const bool put_back = unpatch_bmg(cpu, &g_sail_copies[i]);
            fprintf(stderr, "[options] sail name: message file at 0x%08X %s%c", g_sail_copies[i].address,
                    put_back ? "put back" : "no longer ours", 10);
            sail_forget(i);
        }
        g_sail_mode = want;
        if (want == 0)
            return;
    }
    for (u32 i = g_sail_copy_count; i-- > 0;) // replaced since: not ours any more
        if (!bmg_copy_intact(cpu, &g_sail_copies[i]))
            sail_forget(i);
    if (g_sail_copy_count != 0u)
        return;
    const u8* ram = cpu->ram;
    for (u32 offset = 0; offset + 8u <= cpu->ram_size;) {
        const u8* hit = memmem(ram + offset, cpu->ram_size - offset, "MESGbmg1", 8);
        if (hit == NULL)
            break;
        sail_patch_copy(cpu, 0x80000000u + (u32)(hit - ram), want);
        offset = (u32)(hit - ram) + 8u;
    }
}

// --- Player preferences that work in every session (Controls and Enhancements, not the Randomizer) -----------------------------
//
// Use 'Switch' Targeting Mode: the game's own targeting mode (OptAttentionType, the Options menu's Z-Targeting: 0 is Hold, 1 is
// Switch) is the byte at g_dComIfG_gameInfo + 0x1A6 = 0x803C4DAE. While the setting is on the byte is held at 1 (a loaded file's
// own value is replaced, the Options menu's choice too); turning it off puts back Hold once, and the Options menu governs again.
// Remove Title and Ending Videos: the title's idle movie and the ending's are served as wwrando's very small blank movie from the
// DVD overlay (a change applies to the next open of the file).
static atomic_bool g_pref_switch_targeting, g_pref_remove_videos;
static bool g_switch_targeting_forced;
void bluewake_pref_switch_targeting_set(bool on) { atomic_store(&g_pref_switch_targeting, on); }
void bluewake_pref_remove_videos_set(bool on) { atomic_store(&g_pref_remove_videos, on); }

static void preferences_retrace(CPUState* cpu) {
    enum { kTargeting = 0x803C4DAEu };
    static const char* const kVideos[] = {"thpdemo/title_loop.thp", "thpdemo/end_st_epilogue.thp"};
    static unsigned tick;
    static int videos_state = -1; // what the last check found: 0 off, 1 served
    if (atomic_load(&g_pref_switch_targeting)) {
        if (mem_read8(cpu, kTargeting) != 1u)
            mem_write8(cpu, kTargeting, 1u);
        g_switch_targeting_forced = true;
    } else if (g_switch_targeting_forced) {
        mem_write8(cpu, kTargeting, 0u);
        g_switch_targeting_forced = false;
    }
    // The overlay is checked about once a second (the randomizer's in-place switch drops every overlay, so it is made again).
    if ((++tick % 60u) != 1u)
        return;
    const bool want = atomic_load(&g_pref_remove_videos);
    for (size_t i = 0; i < sizeof kVideos / sizeof *kVideos; ++i) {
        const bool has = dvd_overlay_has(kVideos[i]);
        if (want && !has) {
            const bool ok = dvd_overlay_set(kVideos[i], kBlankThp, (u32)sizeof kBlankThp);
            if (videos_state != 1)
                fprintf(stderr, "[options] remove title and ending videos: %s served blank (%s)\n", kVideos[i], ok ? "ok" : "failed");
        } else if (!want && has) {
            dvd_overlay_clear(kVideos[i]);
            fprintf(stderr, "[options] remove title and ending videos off: %s is the disc's again\n", kVideos[i]);
        }
    }
    videos_state = want ? 1 : 0;
}

void bluewake_game_options_retrace(CPUState* cpu) {
    {
        static int log = -1;
        static unsigned n;
        if (log < 0)
            log = getenv("BLUEWAKE_PERF_LOG") != NULL;
        if (log && ++n % 600u == 0u) {
            fprintf(stderr, "[idle] calls/s=%llu skips/s=%llu mcyc_skipped/s=%.1f no: ee_or_bits=%llu no_deadline=%llu at_deadline=%llu\n",
                    g_idle_calls / 10u, g_idle_skips / 10u, (double)g_idle_cycles / 10e6, g_idle_no[0] / 10u, g_idle_no[1] / 10u,
                    g_idle_no[2] / 10u);
            g_idle_calls = g_idle_skips = g_idle_cycles = g_idle_no[0] = g_idle_no[1] = g_idle_no[2] = 0u;
        }
    }
    preferences_retrace(cpu);
    bluewake_bunny_hood_retrace(cpu);
    instant_text_retrace(cpu);
    sail_name_retrace(cpu);
    bluewake_text_swap_retrace(cpu);
}
