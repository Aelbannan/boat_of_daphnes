// The host's side of the randomizer seed: writes through the guest accessor and reads the module layout in force.
// The logic is rando_seed.c's; see rando_seed.h.
#include "rando_seed.h"

#include "game_options.h"

#include <dlfcn.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static CPUState* g_cpu;
static const StaticRecompModuleDesc* g_module;

// What the seed's writes replaced, in the order they were made: bluewake_rando_disable puts it back in reverse, so a byte
// written twice ends as it was before the first.
typedef struct UndoByte {
    u32 address;
    u8 value;
} UndoByte;
static UndoByte* g_undo;
static size_t g_undo_count, g_undo_capacity;
static volatile unsigned char* g_flags;
static bool g_attached;                       // a seed is applied: the prefs below are written to its switches and symbol
static bool g_pref_music, g_pref_compass, g_pref_switch; // the player's settings (Remove Music, Invert Sea Compass, Switch Targeting)
static atomic_bool g_prefs_dirty;

// The sea compass's and music's switches are clear while wwrando's word (the option on) is in force, set for the game's own word.
static void apply_prefs(void) {
    if (!g_attached || g_flags == NULL || g_cpu == NULL)
        return;
    const u32 music = bluewake_rando_flag_for_option("remove_music");
    const u32 compass = bluewake_rando_flag_for_option("invert_sea_compass_x_axis");
    if (music != 0u && music < 256u)
        g_flags[music] = g_pref_music ? 0u : 1u;
    if (compass != 0u && compass < 256u)
        g_flags[compass] = g_pref_compass ? 0u : 1u;
    const u32 symbol = bluewake_rando_targeting_symbol();
    if (symbol != 0u)
        mem_write8(g_cpu, symbol, g_pref_switch ? 1u : 0u);
}

void bluewake_rando_pref_set(const char* option, bool on) {
    if (strcmp(option, "remove_music") == 0)
        g_pref_music = on;
    else if (strcmp(option, "invert_sea_compass_x_axis") == 0)
        g_pref_compass = on;
    else if (strcmp(option, "switch_targeting_mode") == 0)
        g_pref_switch = on;
    else
        return;
    atomic_store(&g_prefs_dirty, true);
}

void bluewake_rando_prefs_retrace(void) {
    if (atomic_exchange(&g_prefs_dirty, false))
        apply_prefs();
}

static void write_guest(void* user, u32 address, const u8* bytes, u32 size) {
    (void)user;
    // Through the guest accessor, as the mods' boot writes are: REL data lives at linked addresses the aliases map.
    for (u32 i = 0; i < size; ++i) {
        if (g_undo_count == g_undo_capacity) {
            const size_t capacity = g_undo_capacity ? g_undo_capacity * 2u : 1024u;
            UndoByte* grown = (UndoByte*)realloc(g_undo, capacity * sizeof *grown);
            if (grown != NULL) {
                g_undo = grown;
                g_undo_capacity = capacity;
            }
        }
        if (g_undo_count < g_undo_capacity)
            g_undo[g_undo_count++] = (UndoByte){address + i, mem_read8(g_cpu, address + i)};
        mem_write8(g_cpu, address + i, bytes[i]);
    }
}

// The linked address of a module's section in the layout the module is using (the rando mod's, whose RELs the game loads).
static u32 section_start(void* user, u32 module, u32 section) {
    (void)user;
    for (u32 m = 0; g_module != NULL && m < g_module->num_rel_modules; ++m) {
        const StaticRecompRelModule* rel = &g_module->rel_modules[m];
        if (rel->module_id != module)
            continue;
        for (u32 s = 0; s < rel->num_sections; ++s)
            if (rel->sections[s].section_index == section)
                return rel->sections[s].linked_start;
        break;
    }
    return 0u;
}

void bluewake_rando_enable(void* lib, CPUState* cpu, const StaticRecompModuleDesc* module) {
    typedef volatile unsigned char* (*FlagsFn)(void);
    const BlueWakeRandoPack* pack = bluewake_rando_pack();
    if (pack == NULL) {
        fprintf(stderr, "[rando] the randomizer mod is on but no seed is loaded\n");
        return;
    }
    FlagsFn flags = (FlagsFn)dlsym(lib, "bluewake_composite_option_flags");
    if (flags == NULL) {
        fprintf(stderr, "[rando] the game module has no option switches: the randomizer variant is not in this build\n");
        return;
    }
    g_cpu = cpu;
    g_module = module;
    const BlueWakeRandoSink sink = {write_guest, section_start, NULL, flags()};
    u32 skipped = 0u;
    const u32 writes = bluewake_rando_apply(pack, &sink, &skipped);
    // The player's preferences decide the options that are the player's (not the seed's): Hero Mode and the camera's invert are
    // Better Wind Waker's and the Controls setting's own; Remove Music, Invert Sea Compass X-Axis and Switch Targeting follow the
    // settings menu (bluewake_rando_pref_set), live. The seed's values for them (the reference's, or an old seed's) are put aside.
    g_flags = flags();
    g_attached = true;
    apply_prefs();
    fprintf(stderr, "[rando] seed %s (%s): %u writes, %u data bytes not placed\n", pack->seed, pack->seed_hash, writes, skipped);
}

// The randomizer is turned off while the game runs (main.c host_rando_inplace_retrace): what the seed wrote comes out, and
// the options it turned on go back.
void bluewake_rando_disable(void) {
    if (g_cpu == NULL)
        return;
    for (size_t i = g_undo_count; i-- > 0u;)
        mem_write8(g_cpu, g_undo[i].address, g_undo[i].value);
    fprintf(stderr, "[rando] seed put away: %zu guest bytes restored\n", g_undo_count);
    g_undo_count = 0u;
    g_attached = false;
}
