/* BlueWake composite module export glue (P3).
 * Adapts the ModernGekko module-template export for the DOL+REL composite.
 */
#include "generated_composite.h"
#include "StaticRecompABI.h"
#include "module_cpu_contract.h"
#include "dispatch_loop.h"

extern void ppc_set_mem_write_journal(PPCMemWriteJournal fn, void* user);

unsigned dolrecomp_call_depth = 0;
static BluewakeEdgeServiceFn s_edge_service;
static void* s_edge_service_user;
#if defined(BLUEWAKE_DIRECT_CALLS)
/* Use the same selected chunk table and lookup as ordinary dispatch, including
 * mod variants. The donor's direct calls must not bypass player options. */
void (**const bw_chunk_fns)(CPUState*) = s_dolrecomp_chunk_fns;
DolRecompFunction bw_find_chunk(u32 address) {
    return dolrecomp_find_original(address);
}
#endif

#ifdef BLUEWAKE_NATIVE_MATH_CACHED
#include "native_math.h"
static void native_matrix_copy(CPUState* cpu) {
    if (!bluewake_native_math_try(cpu, 0x8030D0C8u)) func_803096E0(cpu);
}
static void native_matrix_concat(CPUState* cpu) {
    if (!bluewake_native_math_try(cpu, 0x8030D0FCu)) func_803096E0(cpu);
}
static void native_matrix_vec(CPUState* cpu) {
    if (!bluewake_native_math_try(cpu, 0x8030DA44u)) func_8030D6E0(cpu);
}
static void native_matrix_array(CPUState* cpu) {
    if (!bluewake_native_math_try(cpu, 0x8030DA98u)) func_8030D6E0(cpu);
}
static DolRecompFunction bluewake_native_math_find(u32 address) {
    /* Cached wrappers always recheck the host handshake, including after disable. */
    switch (address) {
    case 0x8030D0C8u: return native_matrix_copy;
    case 0x8030D0FCu: return native_matrix_concat;
    case 0x8030DA44u: return native_matrix_vec;
    case 0x8030DA98u: return native_matrix_array;
    default: return NULL;
    }
}
#endif

#if defined(BLUEWAKE_DIRECT_CALLS)
int bw_native_call(CPUState* cpu, u32 address) {
#ifdef BLUEWAKE_NATIVE_MATH_CACHED
    return bluewake_native_math_try(cpu, address);
#else
    (void)cpu; (void)address;
    return 0;
#endif
}
#endif

/* The x86-64-v3 dispatch clones come only from DolRecomp's LLVM object
 * backend; the C backend the Builder uses emits none, so an x86-64 build (the
 * Windows port) dispatches through dolrecomp_call unless the source says it
 * carries them. */
#if defined(__x86_64__) && defined(DOLRECOMP_HAS_X86_64_V3_DISPATCH)
#define BLUEWAKE_X86_64_V3_DISPATCH 1
#else
#define BLUEWAKE_X86_64_V3_DISPATCH 0
#endif

#if BLUEWAKE_X86_64_V3_DISPATCH
static int host_has_x86_64_v3(void)
{
#if defined(__GNUC__) || defined(__clang__)
    static int supported = -1;
    if (supported < 0)
    {
        __builtin_cpu_init();
        supported = __builtin_cpu_supports("avx") &&
            __builtin_cpu_supports("avx2") &&
            __builtin_cpu_supports("fma") &&
            __builtin_cpu_supports("bmi") &&
            __builtin_cpu_supports("bmi2") &&
            __builtin_cpu_supports("movbe") &&
            __builtin_cpu_supports("lzcnt");
    }
    return supported;
#else
    return 0;
#endif
}
#endif

static int selected_dispatch(CPUState* ctx, u32 address)
{
#if BLUEWAKE_X86_64_V3_DISPATCH
    if (host_has_x86_64_v3())
        return dolrecomp_call__x86_64_v3(ctx, address);
#endif
    return dolrecomp_call(ctx, address);
}

void dolrecomp_indirect_dispatch(CPUState* ctx, u32 address)
{
    (void)selected_dispatch(ctx, address);
}

static int chassis_dispatch(CPUState* ctx, u32 address)
{
    /* The inlined form, so the loop sees selected_dispatch as a static function
     * rather than as a pointer it must reload and call indirectly at every
     * guest edge. Same body as the exported entry point. */
    return bluewake_chassis_dispatch_loop(
        ctx, address, selected_dispatch, s_edge_service,
        s_edge_service_user);
}

static void chassis_on_state_loaded(CPUState* ctx)
{
    ppc_fpscr_updated(ctx);
}

#include "module_tables.inc"
#include "rel_modules.inc"
typedef struct BlueWakeRelData
{
    u32 module_id;
    u32 section_index;
    u32 linked_start;
    u32 size;
    const u8* bytes;
} BlueWakeRelData;
typedef struct BlueWakeRelLifecycle
{
    u32 module_id;
    u32 prolog_address;
} BlueWakeRelLifecycle;
#include "rel_data.inc"

/* Not const: bluewake_composite_select_layout points rel_modules at a mod's module layout. */
static StaticRecompModuleDesc s_desc = {
#if defined(BW_GUEST_MEM1)
    BLUEWAKE_FIXED_MEM1_ABI_VERSION,
#elif defined(BLUEWAKE_FIXED_CPU)
    BLUEWAKE_FIXED_CPU_ABI_VERSION,
#else
    STATICRECOMP_ABI_VERSION,
#endif
    GXRUNTIME_CPU_ABI_VERSION,
    (u32)sizeof(CPUState),
    MODULE_GAME_ID,
    DOLRECOMP_ENTRY_POINT,
    chassis_dispatch,
    chassis_on_state_loaded,
    s_code_ranges,
    MODULE_CODE_RANGE_COUNT,
    s_smc_ranges,
    MODULE_SMC_RANGE_COUNT,
    s_chunk_ranges,
    MODULE_CHUNK_RANGE_COUNT,
    s_chunk_hashes,
    s_rel_modules,
    MODULE_REL_MODULE_COUNT,
};

#if defined(_WIN32)
#define RECOMP_MODULE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define RECOMP_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define RECOMP_MODULE_EXPORT
#endif

#if defined(BLUEWAKE_NATIVE_ENTRIES)
/* Wind Waker Recomp's certified native entries (scripts/windows/native_entries.py
 * hooks them only where the translation is the one their tests compared): the
 * J3D FIFO matrix loads, two collision checks, PSMTXMultVecSR, the joint matrix
 * calculations and the actor search by name. Built only with --native-entries;
 * BLUEWAKE_NATIVE_ENTRIES=0 leaves them to the translation. */
#include "native_fifo.h"
#include "native_bg.h"
#include "native_vec.h"
#include "native_mtxcalc.h"
#include "native_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void bluewake_native_entries_enable(void)
{
    static int done;
    if (done) return;
    done = 1;
    const char* setting = getenv("BLUEWAKE_NATIVE_ENTRIES");
    const int on = !(setting && strcmp(setting, "0") == 0);
    bluewake_native_fifo_enabled = on;
    bluewake_native_bg_enabled = on;
    bluewake_native_vec_sr_enabled = on;
    bluewake_native_mtxcalc_enabled = on;
    bluewake_native_search_enabled = on;
    if (on) {
        atexit(bluewake_native_fifo_report);
        atexit(bluewake_native_bg_report);
        atexit(bluewake_native_vec_sr_report);
        atexit(bluewake_native_mtxcalc_report);
        atexit(bluewake_native_search_report);
        fprintf(stderr, "[native-entries] certified native entries enabled\n");
    }
}
#endif

RECOMP_MODULE_EXPORT const StaticRecompModuleDesc* staticrecomp_get_module(void)
{
#if defined(BLUEWAKE_NATIVE_ENTRIES)
    bluewake_native_entries_enable();
#endif
    return &s_desc;
}

/* The REL tables in force: the base's, or a mod's module layout (mod_variants.inc) once selected. */
static const BlueWakeRelData* s_active_rel_data = s_rel_data;
static u32 s_active_rel_data_count = MODULE_REL_DATA_COUNT;
static const BlueWakeRelLifecycle* s_active_rel_lifecycle = s_rel_lifecycle;
static u32 s_active_rel_lifecycle_count = MODULE_REL_LIFECYCLE_COUNT;

RECOMP_MODULE_EXPORT const BlueWakeRelData* staticrecomp_get_rel_data(u32* count)
{
    if (count) *count = s_active_rel_data_count;
    return s_active_rel_data;
}

RECOMP_MODULE_EXPORT const BlueWakeRelLifecycle* staticrecomp_get_rel_lifecycle(u32* count)
{
    if (count) *count = s_active_rel_lifecycle_count;
    return s_active_rel_lifecycle;
}

/* Host-only diagnostics need to reach the runtime instance embedded here. */
RECOMP_MODULE_EXPORT void bluewake_set_mem_write_journal(
    PPCMemWriteJournal fn, void* user)
{
    ppc_set_mem_write_journal(fn, user);
}

RECOMP_MODULE_EXPORT void bluewake_set_edge_service(
    BluewakeEdgeServiceFn fn, void* user)
{
    s_edge_service = fn;
    s_edge_service_user = user;
}

/* Mods (scripts/mods/build_mod_variants.py). A mod is compiled in as variant
 * chunks, extra chunks and data writes; the host enables mods once, at boot,
 * before the first dispatch fills the pc cache. The types are declared in
 * generated_composite.h, whose dispatcher consults the extra chunks. */
typedef void (*BlueWakeModWriteFn)(void* user, u32 address, const u8* bytes, u32 size);
#include "mod_variants.inc"
static u32 s_mod_enabled_mask;

static DolRecompFunction bluewake_mod_extra_find(u32 address)
{
#if MODULE_MOD_COUNT > 0
    for (u32 i = 0; i < MODULE_MOD_EXTRA_CHUNK_COUNT; ++i) {
        const BlueWakeModExtraChunk* c = &s_mod_extra_chunks[i];
        if ((s_mod_enabled_mask & (1u << c->mod)) != 0u && c->start <= address &&
            address < c->end && ((address - c->start) & 3u) == 0u)
            return c->fn;
    }
#else
    (void)address;
#endif
    return NULL;
}

#ifndef MODULE_MOD_LAYOUT_COUNT
#define MODULE_MOD_LAYOUT_COUNT 0u
#endif

/* Switches the module's REL tables (the module descriptor's rel_modules, the data images and the prologs) to the
 * layout of the first enabled mod that has one, or back to the base's when none is enabled. A mod that changes the
 * shape of REL data or adds modules carries its own tables (build_mod_variants.py --layout); the host calls this
 * right after loading the module and before it reads any REL table, and again when it turns such a mod on or off
 * while the game runs (bluewake_composite_set_mods). Returns the mod's bit number plus one, or 0 for the base layout. */
RECOMP_MODULE_EXPORT u32 bluewake_composite_select_layout(u32 mask)
{
    s_desc.rel_modules = s_rel_modules;
    s_desc.num_rel_modules = MODULE_REL_MODULE_COUNT;
    s_active_rel_data = s_rel_data;
    s_active_rel_data_count = MODULE_REL_DATA_COUNT;
    s_active_rel_lifecycle = s_rel_lifecycle;
    s_active_rel_lifecycle_count = MODULE_REL_LIFECYCLE_COUNT;
#if MODULE_MOD_LAYOUT_COUNT > 0
    for (u32 i = 0; i < MODULE_MOD_LAYOUT_COUNT; ++i) {
        const BlueWakeModLayout* layout = &s_mod_layouts[i];
        if ((mask & (1u << layout->mod)) == 0u)
            continue;
        s_desc.rel_modules = layout->modules;
        s_desc.num_rel_modules = layout->num_modules;
        s_active_rel_data = layout->data;
        s_active_rel_data_count = layout->num_data;
        s_active_rel_lifecycle = layout->lifecycle;
        s_active_rel_lifecycle_count = layout->num_lifecycle;
        return layout->mod + 1u;
    }
#else
    (void)mask;
#endif
    return 0u;
}

/* One layout's tables, for the host's guest-data aliases (a layout that is not in force still needs its storage when the
 * game switches to it while running). mod_bit 0xFFFFFFFF asks for the base layout's. False if there is no such layout. */
typedef struct BlueWakeLayoutView
{
    const StaticRecompRelModule* modules;
    u32 num_modules;
    const BlueWakeRelData* data;
    u32 num_data;
} BlueWakeLayoutView;

RECOMP_MODULE_EXPORT int bluewake_composite_layout_tables(u32 mod_bit, BlueWakeLayoutView* out)
{
    if (out == NULL)
        return 0;
    if (mod_bit == 0xFFFFFFFFu) {
        out->modules = s_rel_modules;
        out->num_modules = MODULE_REL_MODULE_COUNT;
        out->data = s_rel_data;
        out->num_data = MODULE_REL_DATA_COUNT;
        return 1;
    }
#if MODULE_MOD_LAYOUT_COUNT > 0
    for (u32 i = 0; i < MODULE_MOD_LAYOUT_COUNT; ++i) {
        const BlueWakeModLayout* layout = &s_mod_layouts[i];
        if (layout->mod != mod_bit)
            continue;
        out->modules = layout->modules;
        out->num_modules = layout->num_modules;
        out->data = layout->data;
        out->num_data = layout->num_data;
        return 1;
    }
#endif
    return 0;
}

RECOMP_MODULE_EXPORT u32 bluewake_composite_mod_count(void)
{
    return MODULE_MOD_COUNT;
}

/* How many chunk starts go through a trampoline (build_mod_variants.py --switchable): above zero, the module can turn a switchable
 * mod off again and no pc cache entry goes stale when it turns one on. The host switches the randomizer while the game runs only
 * with this (a module without the export, or with 0, is switched by restarting the game). */
RECOMP_MODULE_EXPORT u32 bluewake_composite_switch_count(void)
{
#ifdef MODULE_MOD_SWITCH_COUNT
    return MODULE_MOD_SWITCH_COUNT;
#else
    return 0u;
#endif
}

RECOMP_MODULE_EXPORT const char* bluewake_composite_mod_name(u32 index)
{
#if MODULE_MOD_COUNT > 0
    return index < MODULE_MOD_COUNT ? s_mod_names[index] : NULL;
#else
    (void)index;
    return NULL;
#endif
}

/* Points the chunk table at the enabled mods' variants; returns how many
 * chunks were replaced. A variant applies when every mod it requires is
 * enabled; the table lists combined variants last, so they win. */
#ifndef MODULE_MOD_SWITCH_COUNT
#define MODULE_MOD_SWITCH_COUNT 0u
#define MODULE_MOD_SWITCH_MASK 0u
#endif

#if MODULE_MOD_SWITCH_COUNT > 0
/* The chunk table's slot of each switchable chunk, and the chunk's own function: the table holds the trampoline
 * (mod_variants.inc) from the first apply on; the trampoline calls the base chunk or the variant the mask selects. */
static u32 s_mod_switch_slot[MODULE_MOD_SWITCH_COUNT];
static DolRecompFunction s_mod_switch_base[MODULE_MOD_SWITCH_COUNT];
static u32 s_mod_switch_ready;

static int bluewake_mod_switch_index(u32 start)
{
    u32 lo = 0, hi = MODULE_MOD_SWITCH_COUNT;  /* s_mod_switch_starts is sorted */
    while (lo < hi) {
        const u32 mid = lo + (hi - lo) / 2u;
        if (s_mod_switch_starts[mid] < start)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo < MODULE_MOD_SWITCH_COUNT && s_mod_switch_starts[lo] == start ? (int)lo : -1;
}
#endif

/* Chooses the variant chunks of the mods in `mask` (every mod, at boot; the host calls it again through
 * bluewake_composite_set_mods when a switchable mod is turned on or off). The chunks of a switchable mod are reached
 * through their trampolines, whose target is set here; the rest are written into the chunk table. */
RECOMP_MODULE_EXPORT u32 bluewake_composite_apply_mods(u32 mask)
{
    u32 replaced = 0;
    s_mod_enabled_mask = mask;
#if MODULE_MOD_COUNT > 0
#if MODULE_MOD_SWITCH_COUNT > 0
    if (!s_mod_switch_ready) {
        for (u32 n = 0; n < MODULE_MOD_SWITCH_COUNT; ++n) {
            u32 k = 0;
            while (k < DOLRECOMP_CHUNK_COUNT && s_dolrecomp_chunk_starts[k] != s_mod_switch_starts[n])
                ++k;
            s_mod_switch_slot[n] = k;
            if (k < DOLRECOMP_CHUNK_COUNT) {
                s_mod_switch_base[n] = s_dolrecomp_chunk_fns[k];
                s_dolrecomp_chunk_fns[k] = s_mod_switch_tramps[n];
            }
            s_mod_switch_target[n] = s_mod_switch_base[n];
        }
        s_mod_switch_ready = 1;
    }
    for (u32 n = 0; n < MODULE_MOD_SWITCH_COUNT; ++n)
        s_mod_switch_target[n] = s_mod_switch_base[n];
#endif
    for (u32 i = 0; i < MODULE_MOD_CHUNK_COUNT; ++i) {
        const BlueWakeModChunk* c = &s_mod_chunks[i];
        if ((mask & c->mask) != c->mask)
            continue;
#if MODULE_MOD_SWITCH_COUNT > 0
        {
            const int n = bluewake_mod_switch_index(c->start);
            if (n >= 0) {
                s_mod_switch_target[n] = c->fn;  /* the last match wins: the most specific variant */
                continue;
            }
        }
#endif
        for (u32 k = 0; k < DOLRECOMP_CHUNK_COUNT; ++k) {
            if (s_dolrecomp_chunk_starts[k] == c->start) {
                s_dolrecomp_chunk_fns[k] = c->fn;
                replaced++;
                break;
            }
        }
    }
#if MODULE_MOD_SWITCH_COUNT > 0
    for (u32 n = 0; n < MODULE_MOD_SWITCH_COUNT; ++n)
        if (s_mod_switch_target[n] != s_mod_switch_base[n])
            replaced++;
#endif
#else
    (void)mask;
#endif
    return replaced;
}

/* Turns mods on and off while the game runs: the chunks, the module layout (the REL tables the host reads again) and
 * the mask the extra chunks are found by. Only the mods build_mod_variants.py --switchable names may differ from the
 * mask given at boot; the host rewrites the guest data of the layout and the mods' data writes itself. Returns the
 * layout mod's bit number plus one, or 0 for the base layout. */
RECOMP_MODULE_EXPORT u32 bluewake_composite_set_mods(u32 mask)
{
    const u32 layout = bluewake_composite_select_layout(mask);
    bluewake_composite_apply_mods(mask);
    return layout;
}

/* Hands the enabled mods' data writes to the host, which writes guest RAM:
 * every write at boot (per_frame_only 0), the per-frame ones (a Gecko code's
 * data writes) at every retrace (per_frame_only 1). */
RECOMP_MODULE_EXPORT u32 bluewake_composite_mod_writes(
    u32 mask, u32 per_frame_only, BlueWakeModWriteFn fn, void* user)
{
    u32 count = 0;
#if MODULE_MOD_COUNT > 0
    for (u32 i = 0; i < MODULE_MOD_WRITE_COUNT; ++i) {
        const BlueWakeModWrite* w = &s_mod_writes[i];
        if ((mask & (1u << w->mod)) == 0u)
            continue;
        if (per_frame_only && !w->every_frame)
            continue;
        if (fn)
            fn(user, w->address, w->bytes, w->size);
        count++;
    }
#else
    (void)mask;
    (void)per_frame_only;
    (void)fn;
    (void)user;
#endif
    return count;
}
