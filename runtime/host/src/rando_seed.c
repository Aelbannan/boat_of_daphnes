// See rando_seed.h. The tables come from scripts/rando/variant_sites.py.
#include "rando_seed.h"

#include <stdio.h>
#include <string.h>

#ifdef BLUEWAKE_RANDO_SITES_INC
#include BLUEWAKE_RANDO_SITES_INC
#else
// No generated tables (a build without the randomizer variant): no sites, no switches.
#define BLUEWAKE_RANDO_SEED_FLAG 0u
#define BLUEWAKE_RANDO_HOOK_FIRST 1000u
#define BLUEWAKE_RANDO_HOOK_COUNT 0u
static const BlueWakeRandoHook kRandoHooks[1] = {{0}};
#define BLUEWAKE_RANDO_DATA_COUNT 0u
static const BlueWakeRandoData kRandoData[1] = {{0}};
#define BLUEWAKE_RANDO_FLAG_COUNT 0u
static const BlueWakeRandoFlag kRandoFlags[1] = {{0}};
#define BLUEWAKE_RANDO_WRITE_COUNT 0u
static const BlueWakeRandoOffWrite kRandoOffWrites[1] = {{0}};
#define BLUEWAKE_RANDO_SYM_SHARDS 0u
#define BLUEWAKE_RANDO_SYM_CLOTHES 0u
#define BLUEWAKE_RANDO_SYM_SWORD_MODE 0u
#define BLUEWAKE_RANDO_SYM_SKIP_REMATCH 0u
#define BLUEWAKE_RANDO_SYM_GEAR 0u
#define BLUEWAKE_RANDO_GEAR_SLOTS 0u
#define BLUEWAKE_RANDO_SYM_QUARTER_HEARTS 0u
#define BLUEWAKE_RANDO_SYM_MAGIC 0u
#define BLUEWAKE_RANDO_SYM_PIGS 0u
#define BLUEWAKE_RANDO_SYM_TARGETING 0u
#define BLUEWAKE_RANDO_SYM_DAMAGE 0u
#endif

static BlueWakeRandoPack g_pack;

void bluewake_rando_set_pack(const BlueWakeRandoPack* pack) {
    if (pack == NULL)
        memset(&g_pack, 0, sizeof g_pack);
    else
        g_pack = *pack;
}

static bool g_module_has_variant, g_session_runs_it;
void bluewake_rando_set_state(bool module_has_variant, bool session_runs_it) {
    g_module_has_variant = module_has_variant;
    g_session_runs_it = session_runs_it;
}
bool bluewake_rando_module_has_variant(void) { return g_module_has_variant; }
bool bluewake_rando_session_runs_it(void) { return g_session_runs_it; }

const BlueWakeRandoPack* bluewake_rando_pack(void) { return g_pack.loaded ? &g_pack : NULL; }

bool bluewake_rando_is_hook(u32 id) {
    return id >= BLUEWAKE_RANDO_HOOK_FIRST && id - BLUEWAKE_RANDO_HOOK_FIRST < BLUEWAKE_RANDO_HOOK_COUNT;
}

u32 bluewake_rando_site_word(const BlueWakeRandoHook* hook, const BlueWakeRandoPack* pack, bool* ok) {
    s32 value;
    if (ok != NULL)
        *ok = true;
    if (hook->location == BLUEWAKE_RANDO_STARTING_ROOM) {
        value = pack->starting_room;
    } else if (hook->location < pack->num_locations && hook->location < BLUEWAKE_RANDO_MAX_LOCATIONS) {
        value = pack->item_id[hook->location];
        if (value < 0)
            return hook->word;  // open: the reference's item stays
    } else {
        if (ok != NULL)
            *ok = false;
        return hook->word;
    }
    const u32 shift = 8u * (3u - (u32)hook->byte);
    return (hook->word & ~(0xFFu << shift)) | (((u32)value & 0xFFu) << shift);
}

u32 bluewake_rando_hook(CPUState* cpu, u32 id) {
    if (!bluewake_rando_is_hook(id) || !g_pack.loaded)
        return 0u;
    const BlueWakeRandoHook* hook = &kRandoHooks[id - BLUEWAKE_RANDO_HOOK_FIRST];
    bool ok = false;
    const u32 word = bluewake_rando_site_word(hook, &g_pack, &ok);
    if (!ok)
        return 0u;
    const u32 imm = word & 0xFFFFu;
    {
        // Once per site: the evidence in the log that a seed hook ran (and what it wrote).
        static u8 s_logged[BLUEWAKE_RANDO_HOOK_COUNT + 1u];
        const u32 index = id - BLUEWAKE_RANDO_HOOK_FIRST;
        if (index < BLUEWAKE_RANDO_HOOK_COUNT && !s_logged[index]) {
            s_logged[index] = 1u;
            fprintf(stderr, "[rando] hook %u (location %u) wrote item 0x%02X\n", (unsigned)index, (unsigned)hook->location,
                    (unsigned)(hook->kind == BLUEWAKE_RANDO_KIND_LI ? (imm & 0xFFu) : imm));
        }
    }
    if (hook->kind == BLUEWAKE_RANDO_KIND_LI) {
        // li rD, simm: the immediate sign-extended (a boss drop's id sits in the high byte).
        cpu->gpr[hook->reg] = (u32)(s32)(s16)imm;
        return 0u;
    }
    // cmplwi / cmpwi crf0, rA, imm: the compare the instruction made, redone with the pack's immediate (the hook runs
    // after it, and the register is unchanged), with the bits the translator's own compare sets.
    const u32 a = cpu->gpr[hook->reg];
    u32 bits = 0;
    if (hook->kind == BLUEWAKE_RANDO_KIND_CMPLWI) {
        if (a < imm) bits |= 0x8u;
        if (a > imm) bits |= 0x4u;
        if (a == imm) bits |= 0x2u;
    } else {
        const s32 sa = (s32)a, sb = (s32)(s16)imm;
        if (sa < sb) bits |= 0x8u;
        if (sa > sb) bits |= 0x4u;
        if (sa == sb) bits |= 0x2u;
    }
    bits |= (cpu->xer >> 31) & 1u;
    cpu->cr = (cpu->cr & ~(0xFu << 28)) | (bits << 28);
    return 0u;
}

u32 bluewake_rando_flag_for_option(const char* option) {
    for (u32 i = 0; i < BLUEWAKE_RANDO_FLAG_COUNT; ++i)
        if (kRandoFlags[i].option[0] != '\0' && strcmp(kRandoFlags[i].option, option) == 0)
            return kRandoFlags[i].index;
    return 0u;
}

u32 bluewake_rando_targeting_symbol(void) { return BLUEWAKE_RANDO_SYM_TARGETING; }

const char* bluewake_rando_option(const BlueWakeRandoPack* pack, const char* name) {
    for (u32 i = 0; i < pack->num_options && i < BLUEWAKE_RANDO_MAX_OPTIONS; ++i)
        if (strcmp(pack->options[i].name, name) == 0)
            return pack->options[i].value;
    return "";
}

bool bluewake_rando_option_active(const BlueWakeRandoPack* pack, const BlueWakeRandoFlag* flag) {
    return strcmp(bluewake_rando_option(pack, flag->option), flag->value) == 0;
}

static u32 put(const BlueWakeRandoSink* sink, u32 address, const u8* bytes, u32 size) {
    sink->write(sink->user, address, bytes, size);
    return 1u;
}

static u32 put8(const BlueWakeRandoSink* sink, u32 address, u8 value) { return put(sink, address, &value, 1u); }

u32 bluewake_rando_apply(const BlueWakeRandoPack* pack, const BlueWakeRandoSink* sink, u32* skipped) {
    u32 writes = 0u, missed = 0u;
    // 1. The switches: the seed's, and for each option that is off in the seed the one that restores its vanilla word.
    if (sink->flags != NULL) {
        for (u32 i = 0; i < BLUEWAKE_RANDO_FLAG_COUNT; ++i) {
            const BlueWakeRandoFlag* flag = &kRandoFlags[i];
            if (flag->index == 0u || flag->index >= 256u)
                continue;
            sink->flags[flag->index] = flag->option[0] == '\0' ? 1u : (bluewake_rando_option_active(pack, flag) ? 0u : 1u);
        }
    }
    // 2. The vanilla bytes an option that is off restores (data the option changes; its words are option sites).
    for (u32 i = 0; i < BLUEWAKE_RANDO_WRITE_COUNT; ++i) {
        const BlueWakeRandoOffWrite* w = &kRandoOffWrites[i];
        if (sink->flags != NULL && sink->flags[w->flag] != 0u)
            writes += put(sink, w->address, w->bytes, w->size);
    }
    // 3. The item bytes in data tables.
    for (u32 i = 0; i < BLUEWAKE_RANDO_DATA_COUNT; ++i) {
        const BlueWakeRandoData* d = &kRandoData[i];
        if (d->location >= pack->num_locations || d->location >= BLUEWAKE_RANDO_MAX_LOCATIONS || pack->item_id[d->location] < 0) {
            ++missed;
            continue;
        }
        u32 address = d->address;
        if (d->module != 0u) {
            const u32 start = sink->section != NULL ? sink->section(sink->user, d->module, d->section) : 0u;
            if (start == 0u) {
                ++missed;
                continue;
            }
            address = start + d->section_offset;
        }
        writes += put8(sink, address, (u8)pack->item_id[d->location]);
    }
    // 4. The game variables of wwrando's custom symbols (tweaks.py): what the new-game code reads.
    if (BLUEWAKE_RANDO_SYM_GEAR != 0u) {
        writes += put8(sink, BLUEWAKE_RANDO_SYM_SHARDS, pack->triforce_shards);
        writes += put8(sink, BLUEWAKE_RANDO_SYM_CLOTHES, pack->heros_clothes);
        writes += put8(sink, BLUEWAKE_RANDO_SYM_SWORD_MODE, pack->sword_mode);
        writes += put8(sink, BLUEWAKE_RANDO_SYM_SKIP_REMATCH, pack->skip_rematch_bosses);
        u8 gear[BLUEWAKE_RANDO_MAX_GEAR];
        memset(gear, 0, sizeof gear);
        u32 count = pack->gear_count < BLUEWAKE_RANDO_MAX_GEAR - 1u ? pack->gear_count : BLUEWAKE_RANDO_MAX_GEAR - 1u;
        if (count + 1u > BLUEWAKE_RANDO_GEAR_SLOTS)
            count = BLUEWAKE_RANDO_GEAR_SLOTS - 1u;
        memcpy(gear, pack->gear, count);
        gear[count] = 0xFFu;  // the end mark
        writes += put(sink, BLUEWAKE_RANDO_SYM_GEAR, gear, count + 1u);
        const u8 hearts[2] = {(u8)(pack->quarter_hearts >> 8), (u8)pack->quarter_hearts};
        writes += put(sink, BLUEWAKE_RANDO_SYM_QUARTER_HEARTS, hearts, 2u);
        writes += put8(sink, BLUEWAKE_RANDO_SYM_MAGIC, pack->magic);
        writes += put8(sink, BLUEWAKE_RANDO_SYM_PIGS, pack->pigs_bitfield);
        writes += put8(sink, BLUEWAKE_RANDO_SYM_TARGETING, pack->targeting_mode);
        if (pack->has_damage_multiplier) {
            u32 bits;
            memcpy(&bits, &pack->damage_multiplier, sizeof bits);
            const u8 f[4] = {(u8)(bits >> 24), (u8)(bits >> 16), (u8)(bits >> 8), (u8)bits};
            writes += put(sink, BLUEWAKE_RANDO_SYM_DAMAGE, f, 4u);
        }
    }
    if (skipped != NULL)
        *skipped = missed;
    return writes;
}
