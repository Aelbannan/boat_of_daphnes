// SPDX-License-Identifier: GPL-3.0-or-later
// Boat of Daphnes' randomizer seed runtime (runtime/host/src/rando_seed.c, unchanged, with the site tables
// tools/rando/variant_sites.py generated for this pipeline: rando_sites.inc) and the plain-typed calls this
// runtime's side (src/runtime/bod_rando.cpp) makes into it: its own headers clash with BoD's.
#define BLUEWAKE_RANDO_SITES_INC "../../../shim/rando_sites.inc"
#include "../runtime/host/src/rando_seed.c"

// The instruction a seed hook's site holds for the pack in force (the reference word with the seed's item in it).
unsigned bod_rando_site_word(unsigned id, int* ok) {
    *ok = 0;
    if (!bluewake_rando_is_hook(id) || !g_pack.loaded)
        return 0u;
    bool good = false;
    const u32 word = bluewake_rando_site_word(&kRandoHooks[id - BLUEWAKE_RANDO_HOOK_FIRST], &g_pack, &good);
    *ok = good ? 1 : 0;
    return word;
}

// The seed's switches, data bytes and game variables (bluewake_rando_apply) through the runtime's writer.
unsigned bod_rando_apply(BlueWakeRandoWriteFn write, BlueWakeRandoSectionFn section, unsigned char* flags, unsigned* skipped) {
    *skipped = 0u;
    if (!g_pack.loaded)
        return 0u;
    const BlueWakeRandoSink sink = {write, section, NULL, flags};
    return bluewake_rando_apply(&g_pack, &sink, skipped);
}

// Whether a seed is loaded, and its option values (hero mode, invert camera: Better Wind Waker's own options).
int bod_rando_loaded(void) { return g_pack.loaded ? 1 : 0; }
unsigned bod_rando_flag_for_option(const char* option) { return bluewake_rando_flag_for_option(option); }
unsigned bod_rando_targeting_symbol(void) { return bluewake_rando_targeting_symbol(); }
const char* bod_rando_value(const char* option) { return g_pack.loaded ? bluewake_rando_option(&g_pack, option) : ""; }
float bod_rando_damage(void) { return g_pack.loaded && g_pack.has_damage_multiplier ? g_pack.damage_multiplier : 0.f; }
const char* bod_rando_name(void) { return g_pack.loaded ? g_pack.seed : ""; }
const char* bod_rando_hash(void) { return g_pack.loaded ? g_pack.seed_hash : ""; }
