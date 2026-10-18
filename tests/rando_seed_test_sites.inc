// A small site table for tests/rando_seed_test.c, in the shape scripts/rando/variant_sites.py generates.
#define BLUEWAKE_RANDO_SEED_FLAG 128u
#define BLUEWAKE_RANDO_HOOK_FIRST 1000u
#define BLUEWAKE_RANDO_HOOK_COUNT 6u
static const BlueWakeRandoHook kRandoHooks[] = {
    {1000u, 1, 0, 3, BLUEWAKE_RANDO_STARTING_ROOM, 0x3800002Cu, 0x80058BA4u, "main.dol"}, // li r0, 44: the new-game room
    {1001u, 1, 7, 2, 119u, 0x38E000FFu, 0u, "d_a_boss.rel"},    // li r7, 0x00FF: a boss drop, the id in the high byte
    {1002u, 1, 4, 3, 112u, 0x38800000u, 0u, "d_a_x.rel"},       // li r4, 0
    {1003u, 2, 0, 3, 177u, 0x2800002Cu, 0u, "d_a_npc_bs1.rel"}, // cmplwi r0, 44
    {1004u, 3, 23, 3, 230u, 0x2C170050u, 0u, "d_a_npc_bs1.rel"}, // cmpwi r23, 80
    {1005u, 1, 3, 3, 9u, 0x38600007u, 0u, "d_a_y.rel"},         // li r3, 7: location 9 is open in the test pack
};
#define BLUEWAKE_RANDO_DATA_COUNT 3u
static const BlueWakeRandoData kRandoData[] = {
    {113u, 0x8035BD34u, 0u, 0u, 0u},   // a DOL byte
    {33u, 0u, 7u, 5u, 49u},            // a REL data byte: module 7, section 5, offset 49
    {12u, 0u, 9u, 3u, 0u},             // a REL byte in a section the layout lacks
};
#define BLUEWAKE_RANDO_FLAG_COUNT 3u
static const BlueWakeRandoFlag kRandoFlags[] = {
    {"rando_seed", 128u, "", ""},
    {"rando_no_hero_mode", 129u, "hero_mode", "true"},
    {"rando_no_sword_mode_swordless", 130u, "sword_mode", "Swordless"},
};
static const u8 kRandoOffBytes0[] = {0x00u};
#define BLUEWAKE_RANDO_WRITE_COUNT 1u
static const BlueWakeRandoOffWrite kRandoOffWrites[] = {
    {129u, 0x8038B0C3u, kRandoOffBytes0, 1u},
};
#define BLUEWAKE_RANDO_SYM_SHARDS 0x803FDC70u
#define BLUEWAKE_RANDO_SYM_CLOTHES 0x803FDC71u
#define BLUEWAKE_RANDO_SYM_SWORD_MODE 0x803FDC72u
#define BLUEWAKE_RANDO_SYM_SKIP_REMATCH 0x803FDC73u
#define BLUEWAKE_RANDO_SYM_GEAR 0x803FDC74u
#define BLUEWAKE_RANDO_GEAR_SLOTS 88u
#define BLUEWAKE_RANDO_SYM_QUARTER_HEARTS 0x803FDCCCu
#define BLUEWAKE_RANDO_SYM_MAGIC 0x803FDCCEu
#define BLUEWAKE_RANDO_SYM_PIGS 0x803FDCCFu
#define BLUEWAKE_RANDO_SYM_TARGETING 0x803FDCD0u
#define BLUEWAKE_RANDO_SYM_DAMAGE 0x803FE974u
