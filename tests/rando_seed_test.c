// The randomizer seed's host functions (runtime/host/src/rando_seed.c), on a fake CPUState and a flat memory.
// Built with the small table tests/rando_seed_test_sites.inc (BLUEWAKE_RANDO_SITES_INC).
// The unit under test is included, so the test sees its generated table (kRandoHooks, ...).
#include "rando_seed.c"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { kBase = 0x80000000u, kSize = 0x00500000u };
static unsigned char g_ram[kSize];
static unsigned char g_flags[256];
// Writes above MEM1 (linked REL data): the test records them instead of storing them.
static struct { u32 address; u8 value; } g_linked[8];
static int g_linked_count;

static void write_ram(void* user, u32 address, const u8* bytes, u32 size) {
    (void)user;
    if (address >= 0xC0000000u) {
        assert(size == 1u && g_linked_count < 8);
        g_linked[g_linked_count].address = address;
        g_linked[g_linked_count].value = bytes[0];
        g_linked_count++;
        return;
    }
    assert(address >= kBase && address - kBase + size <= kSize);
    memcpy(g_ram + (address - kBase), bytes, size);
}

// A layout of one module: module 7's section 5 is linked at 0xC0700200; nothing else.
static u32 section(void* user, u32 module, u32 index) {
    (void)user;
    return module == 7u && index == 5u ? 0xC0700200u : 0u;
}

static void put_option(BlueWakeRandoPack* p, const char* name, const char* value) {
    snprintf(p->options[p->num_options].name, sizeof p->options[0].name, "%s", name);
    snprintf(p->options[p->num_options].value, sizeof p->options[0].value, "%s", value);
    p->num_options++;
}

static BlueWakeRandoPack make_pack(void) {
    BlueWakeRandoPack p;
    memset(&p, 0, sizeof p);
    p.loaded = true;
    snprintf(p.seed, sizeof p.seed, "TESTSEED");
    snprintf(p.seed_hash, sizeof p.seed_hash, "Ganon Gohma");
    p.num_locations = 320;
    for (u32 i = 0; i < p.num_locations; ++i)
        p.item_id[i] = (s16)(i & 0x7F);
    p.item_id[119] = 0xCC;   // a boss drop: id 0xCC makes the immediate 0xCCFF (negative)
    p.item_id[177] = 103;
    p.item_id[230] = 5;
    p.item_id[9] = -1;       // open: an Ice Trap chest keeps the reference's item
    p.item_id[113] = 0x2A;
    p.item_id[33] = 0x31;
    p.starting_room = 31;
    put_option(&p, "hero_mode", "false");
    put_option(&p, "sword_mode", "Swordless");
    put_option(&p, "remove_music", "true");
    p.sword_mode = 2;
    p.triforce_shards = 3;
    p.quarter_hearts = 0x0123;
    p.magic = 16;
    p.skip_rematch_bosses = 1;
    p.pigs_bitfield = 5;
    p.targeting_mode = 1;
    p.heros_clothes = 1;
    p.gear_count = 3;
    p.gear[0] = 0x6E;
    p.gear[1] = 0x3B;
    p.gear[2] = 0x72;
    return p;
}

static void test_site_words(void) {
    BlueWakeRandoPack p = make_pack();
    bool ok = false;
    // the new-game room: the low byte of `li r0, room`
    assert(bluewake_rando_site_word(&kRandoHooks[0], &p, &ok) == 0x3800001Fu && ok);
    // a boss drop: the id in the high byte of the immediate, the 0xFF below kept
    assert(bluewake_rando_site_word(&kRandoHooks[1], &p, &ok) == 0x38E0CCFFu && ok);
    assert(bluewake_rando_site_word(&kRandoHooks[2], &p, &ok) == 0x38800000u + (112u & 0x7Fu) && ok);
    assert(bluewake_rando_site_word(&kRandoHooks[3], &p, &ok) == 0x28000067u && ok);
    // an open location keeps the reference word
    assert(bluewake_rando_site_word(&kRandoHooks[5], &p, &ok) == 0x38600007u && ok);
    // a location outside the pack is reported
    p.num_locations = 100;
    bluewake_rando_site_word(&kRandoHooks[3], &p, &ok);
    assert(!ok);
}

static void test_hooks(void) {
    BlueWakeRandoPack p = make_pack();
    CPUState cpu;
    memset(&cpu, 0, sizeof cpu);
    // no pack set: a hook does nothing
    bluewake_rando_set_pack(NULL);
    cpu.gpr[0] = 0x1234u;
    assert(bluewake_rando_hook(&cpu, 1000u) == 0u && cpu.gpr[0] == 0x1234u);
    bluewake_rando_set_pack(&p);
    assert(bluewake_rando_pack() != NULL && strcmp(bluewake_rando_pack()->seed, "TESTSEED") == 0);
    assert(bluewake_rando_is_hook(1000u) && bluewake_rando_is_hook(1005u) && !bluewake_rando_is_hook(999u) &&
           !bluewake_rando_is_hook(1006u));
    // li: the register gets the (sign-extended) immediate; nothing else moves
    assert(bluewake_rando_hook(&cpu, 1000u) == 0u && cpu.gpr[0] == 31u);
    assert(bluewake_rando_hook(&cpu, 1001u) == 0u && cpu.gpr[7] == 0xFFFFCCFFu);
    assert(cpu.gpr[4] == 0u);
    // cmplwi r0, 103 with r0 = 103, 102 and 104: EQ, LT, GT in CR0, the rest of CR untouched
    cpu.cr = 0x0ABCDEF0u;
    cpu.gpr[0] = 103u;
    bluewake_rando_hook(&cpu, 1003u);
    assert(cpu.cr == (0x0ABCDEF0u | 0x20000000u));
    cpu.gpr[0] = 102u;
    bluewake_rando_hook(&cpu, 1003u);
    assert(cpu.cr == (0x0ABCDEF0u | 0x80000000u));
    cpu.gpr[0] = 104u;
    bluewake_rando_hook(&cpu, 1003u);
    assert(cpu.cr == (0x0ABCDEF0u | 0x40000000u));
    // the summary overflow bit comes from XER, as the translator's compare sets it
    cpu.xer = 0x80000000u;
    cpu.gpr[0] = 104u;
    bluewake_rando_hook(&cpu, 1003u);
    assert(cpu.cr == (0x0ABCDEF0u | 0x50000000u));
    cpu.xer = 0;
    // cmplwi is unsigned (0xFFFFFFFF > 103), cmpwi signed (-1 < 5)
    cpu.gpr[0] = 0xFFFFFFFFu;
    bluewake_rando_hook(&cpu, 1003u);
    assert((cpu.cr >> 28) == 0x4u);
    cpu.gpr[23] = 0xFFFFFFFFu;
    bluewake_rando_hook(&cpu, 1004u);
    assert((cpu.cr >> 28) == 0x8u);
    cpu.gpr[23] = 5u;
    bluewake_rando_hook(&cpu, 1004u);
    assert((cpu.cr >> 28) == 0x2u);
    // an unknown id changes nothing
    const u32 cr = cpu.cr;
    assert(bluewake_rando_hook(&cpu, 2000u) == 0u && cpu.cr == cr);
    // an open location (9): the immediate stays the reference's
    assert(bluewake_rando_hook(&cpu, 1005u) == 0u && cpu.gpr[3] == 7u);
    bluewake_rando_set_pack(NULL);
}

static void test_apply(void) {
    BlueWakeRandoPack p = make_pack();
    memset(g_ram, 0xEE, sizeof g_ram);
    memset(g_flags, 0, sizeof g_flags);
    g_linked_count = 0;
    BlueWakeRandoSink sink = {write_ram, section, NULL, g_flags};
    u32 skipped = 0;
    // the option switches: the seed's is on; hero mode is off in the seed, so its switch (restore vanilla) is set;
    // swordless is on in the seed, so its switch stays clear
    bluewake_rando_apply(&p, &sink, &skipped);
    assert(g_flags[128] == 1 && g_flags[129] == 1 && g_flags[130] == 0);
    // the hero mode byte restored to vanilla
    assert(g_ram[0x8038B0C3u - kBase] == 0x00);
    // data bytes: a DOL byte, a REL byte through the layout (module 7 section 5 at 0xC0700200, offset 49); the third has no
    // section in the layout: skipped
    assert(g_ram[0x8035BD34u - kBase] == 0x2A);
    assert(g_linked_count == 1 && g_linked[0].address == 0xC0700200u + 49u && g_linked[0].value == 0x31);
    assert(skipped == 1u);
    // game variables
    assert(g_ram[0x803FDC70u - kBase] == 3 && g_ram[0x803FDC71u - kBase] == 1 && g_ram[0x803FDC72u - kBase] == 2 &&
           g_ram[0x803FDC73u - kBase] == 1);
    assert(g_ram[0x803FDC74u - kBase] == 0x6E && g_ram[0x803FDC75u - kBase] == 0x3B && g_ram[0x803FDC76u - kBase] == 0x72);
    assert(g_ram[0x803FDC77u - kBase] == 0xFF);  // the end mark
    assert(g_ram[0x803FDCCCu - kBase] == 0x01 && g_ram[0x803FDCCDu - kBase] == 0x23);
    assert(g_ram[0x803FDCCEu - kBase] == 16 && g_ram[0x803FDCCFu - kBase] == 5 && g_ram[0x803FDCD0u - kBase] == 1);
    // no damage multiplier unless the seed has one
    assert(g_ram[0x803FE974u - kBase] == 0xEE);
    p.has_damage_multiplier = true;
    p.damage_multiplier = 4.0f;
    bluewake_rando_apply(&p, &sink, &skipped);
    assert(g_ram[0x803FE974u - kBase] == 0x40 && g_ram[0x803FE975u - kBase] == 0x80 && g_ram[0x803FE976u - kBase] == 0 &&
           g_ram[0x803FE977u - kBase] == 0);
    // hero mode on in the seed: the restore switch clears and the byte is left as the reference has it
    snprintf(p.options[0].value, sizeof p.options[0].value, "true");
    memset(g_ram, 0xEE, sizeof g_ram);
    bluewake_rando_apply(&p, &sink, &skipped);
    assert(g_flags[129] == 0 && g_ram[0x8038B0C3u - kBase] == 0xEE);
    // an open item (location 113 = -1) skips its byte
    p.item_id[113] = -1;
    memset(g_ram, 0xEE, sizeof g_ram);
    bluewake_rando_apply(&p, &sink, &skipped);
    assert(g_ram[0x8035BD34u - kBase] == 0xEE && skipped == 2u);
    // a long gear list keeps the end mark inside the symbol's room and the next symbol whole
    p.gear_count = BLUEWAKE_RANDO_MAX_GEAR;
    memset(p.gear, 0x11, sizeof p.gear);
    memset(g_ram, 0xEE, sizeof g_ram);
    bluewake_rando_apply(&p, &sink, &skipped);
    assert(g_ram[0x803FDC74u - kBase + 86u] == 0x11 && g_ram[0x803FDC74u - kBase + 87u] == 0xFF);
    assert(g_ram[0x803FDCCCu - kBase] == 0x01);
    // an option the pack does not name counts as not matching: its restore switch is set
    p.num_options = 0;
    bluewake_rando_apply(&p, &sink, &skipped);
    assert(g_flags[129] == 1 && g_flags[130] == 1 && strcmp(bluewake_rando_option(&p, "hero_mode"), "") == 0);
}

int main(void) {
    test_site_words();
    test_hooks();
    test_apply();
    puts("rando_seed_test: ok");
    return 0;
}
