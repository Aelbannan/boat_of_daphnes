// The randomizer's seed in the running game (R3, host side).
//
// A seed pack (Randomizer\<seed>\seed.json, scripts/rando/generate_seed.py) says where wwrando's generator put every item.
// The module carries wwrando's code as a mod variant ("rando"); what differs per seed is data, and it reaches the game three ways:
//   * 45 code sites whose instruction holds an item id (li and compares) run a native hook after the instruction: the
//     hook rewrites the register, or the compare's result, from the pack (bluewake_rando_hook);
//   * 42 data bytes (items in DOL and REL data tables) and the game variables of wwrando's custom symbols (starting gear,
//     hearts, magic, sword mode, ...) are written into guest memory (bluewake_rando_apply);
//   * the option words (swordless, hero mode, ...) are option sites; the switches rando_no_<option> restore the vanilla
//     word of an option that is off in this seed (also set by bluewake_rando_apply).
// The sites come from scripts/rando/variant_sites.py (docs/fork/rando-inhouse.md, rando-variant.md section 3).
#ifndef BLUEWAKE_RANDO_SEED_H
#define BLUEWAKE_RANDO_SEED_H

#include "StaticRecompABI.h"
#include "core/cpu.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUEWAKE_RANDO_MAX_LOCATIONS 400u  // wwrando 1.10.0 has 320 item locations
#define BLUEWAKE_RANDO_MAX_OPTIONS 192u
#define BLUEWAKE_RANDO_MAX_GEAR 88u        // the custom symbol's room: 87 items and the 0xFF end mark

// What the host keeps of a seed.json.
typedef struct BlueWakeRandoPack {
    bool loaded;
    char seed[64];           // "seed": the sanitised seed string
    char seed_hash[64];      // "seed_hash": the two names of the name entry screen ("Ganon Gohma")
    char permalink[256];     // "permalink"
    char wwrando_commit[48]; // "generator.wwrando_commit": the pack's identity beside the seed
    u32 num_locations;
    s16 item_id[BLUEWAKE_RANDO_MAX_LOCATIONS];  // locations[i].item_id; -1: none (an Ice Trap chest keeps the vanilla item)
    s32 starting_room;       // starting.island.room (44 = Outset)
    u32 num_options;
    struct {
        char name[48];
        char value[48];      // booleans as "true" / "false", numbers in decimal, choices as wwrando names them
    } options[BLUEWAKE_RANDO_MAX_OPTIONS];
    // starting.game_variables
    u8 sword_mode;           // 0 sword, 1 none, 2 swordless
    u8 triforce_shards;
    u16 quarter_hearts;
    u8 magic;
    u8 skip_rematch_bosses;
    u8 pigs_bitfield;
    u8 targeting_mode;       // option_targeting_mode: 1 for switch targeting, else 0
    bool has_damage_multiplier;
    float damage_multiplier; // hero mode: 4.0
    u8 heros_clothes;        // should_start_with_heros_clothes: 1 unless the seed has casual clothes
    u32 gear_count;
    u8 gear[BLUEWAKE_RANDO_MAX_GEAR];  // starting.gear[].item_id
} BlueWakeRandoPack;

// The seed pack the game plays, or NULL for none. The host copies it; the app sets it before the game starts.
void bluewake_rando_set_pack(const BlueWakeRandoPack* pack);
const BlueWakeRandoPack* bluewake_rando_pack(void);

// Hook kinds of the generated site table (rando_sites.inc).
enum { BLUEWAKE_RANDO_KIND_LI = 1, BLUEWAKE_RANDO_KIND_CMPLWI = 2, BLUEWAKE_RANDO_KIND_CMPWI = 3 };
// A hook with this as its location takes the pack's starting room (the new-game `li r0, room`).
#define BLUEWAKE_RANDO_STARTING_ROOM 0xFFFFu

typedef struct BlueWakeRandoHook {
    u32 id;           // the native hook id of the site
    int kind;
    int reg;          // li: the register written; compares: the register compared
    int byte;         // which byte of the instruction holds the id (2 or 3)
    u32 location;     // the pack's location id, or BLUEWAKE_RANDO_STARTING_ROOM
    u32 word;         // the instruction as the reference build has it
    u32 address;      // DOL sites: the guest address (REL sites: 0)
    const char* file; // the module file the site is in ("main.dol" for the DOL)
} BlueWakeRandoHook;

typedef struct BlueWakeRandoData {
    u32 location;       // the pack's location id
    u32 address;        // DOL: the guest address of the byte
    u32 module;         // REL: the module id, 0 for the DOL
    u32 section;        // REL: the section index
    u32 section_offset; // REL: the byte's offset in the section
} BlueWakeRandoData;

typedef struct BlueWakeRandoFlag {
    const char* name;   // the option switch's name
    u32 index;          // its slot in dolrecomp_option_flags[]
    const char* option; // the wwrando option it follows ("" for the seed switch)
    const char* value;  // the value that turns the option's word on ("true" for a boolean)
} BlueWakeRandoFlag;

typedef struct BlueWakeRandoOffWrite {
    u32 flag;           // written while this switch is set (the option is off)
    u32 address;
    const u8* bytes;
    u32 size;
} BlueWakeRandoOffWrite;

// What a hook leaves the instruction as: the reference word with the pack's item id in its varying byte. An item the pack
// leaves open (-1) keeps the reference word's value. *ok is false if the site's location is outside the pack.
u32 bluewake_rando_site_word(const BlueWakeRandoHook* hook, const BlueWakeRandoPack* pack, bool* ok);
// The native hook (ids from BLUEWAKE_RANDO_HOOK_FIRST): li sets its register; a compare recomputes CR0 from the register
// and the new immediate. Returns 0 (go on): every site is hooked after its instruction.
u32 bluewake_rando_hook(CPUState* cpu, u32 id);
bool bluewake_rando_is_hook(u32 id);

// Writes of a seed into guest memory: a callback so the host writes through its accessor and the tests into an array.
typedef void (*BlueWakeRandoWriteFn)(void* user, u32 address, const u8* bytes, u32 size);
// Where a module's section is linked (the module layout in force): the section's linked start, or 0 if unknown.
typedef u32 (*BlueWakeRandoSectionFn)(void* user, u32 module, u32 section);

typedef struct BlueWakeRandoSink {
    BlueWakeRandoWriteFn write;
    BlueWakeRandoSectionFn section;
    void* user;
    volatile unsigned char* flags;  // dolrecomp_option_flags
} BlueWakeRandoSink;

// Applies a seed once, before the game's first frame: the option switches, the data bytes, the game variables. Returns the
// number of writes made; *skipped counts the data bytes that could not be placed (a section the layout lacks, an open item).
u32 bluewake_rando_apply(const BlueWakeRandoPack* pack, const BlueWakeRandoSink* sink, u32* skipped);
// The option's value in the pack ("" if the pack has no such option).
const char* bluewake_rando_option(const BlueWakeRandoPack* pack, const char* name);
// The option switch (slot in dolrecomp_option_flags[]) that follows a wwrando option, 0 if the variant has none. The switch is clear
// while the option's word is wwrando's and set while it is the game's own.
u32 bluewake_rando_flag_for_option(const char* option);
// The guest address of wwrando's option_targeting_mode byte (0 without the variant's tables).
u32 bluewake_rando_targeting_symbol(void);
// Whether a switch's option is in the state that keeps wwrando's word (the flag is then clear).
bool bluewake_rando_option_active(const BlueWakeRandoPack* pack, const BlueWakeRandoFlag* flag);

// What the module and the session have: whether the game module carries the randomizer variant (a mod named "rando") and
// whether this session runs it. Set by the host at boot; the app's file select reads them.
void bluewake_rando_set_state(bool module_has_variant, bool session_runs_it);
bool bluewake_rando_module_has_variant(void);
bool bluewake_rando_session_runs_it(void);

// The host's step at boot, after the game options are enabled: applies the pack if the "rando" mod is on and a pack is set.
// `module` is the loaded game module (its REL layout), `lib` its handle.
void bluewake_rando_enable(void* lib, CPUState* cpu, const StaticRecompModuleDesc* module);
// The randomizer is turned off while the game runs: what bluewake_rando_enable wrote is put back and the options it set go back.
void bluewake_rando_disable(void);
// The player's settings for the options that are theirs, not the seed's (Remove Music, Invert Sea Compass X-Axis, Switch Targeting
// Mode; the wwrando option's name): written to the seed's switches and symbol once a seed is applied, and live after (the next
// retrace). Without a randomizer session they change nothing here (Switch Targeting has its own path in game_options.c).
void bluewake_rando_pref_set(const char* option, bool on);
void bluewake_rando_prefs_retrace(void);

#ifdef __cplusplus
}
#endif

#endif
