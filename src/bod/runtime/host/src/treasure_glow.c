// Treasure Light Up Close: see treasure_glow.h.
//
// daSalvage_c::calcAlpha (d_a_salvage.cpp:388) draws a salvage point's beam
// only while near * 100 <= distance <= 25000, where near is 80 for chart
// salvage and 20 (10 in a room) for the rest; closer, the beam fades out. The
// hook sits before the multiply that forms near * 100 (f30 = near) and sets
// near to 0 for the slots chosen, so the far branch keeps the beam on. The far
// limit, whether the point is still there and all salvage logic are untouched.
#include "treasure_glow.h"

#include <stdlib.h>

static volatile int g_mode;

enum {
    SALVAGE_TAG_DATA = 0x803F6B44u, // daSalvage_c::mTagData_p (dSalvage_control_c*)
    SALVAGE_INFO = 0x4u,            // its mInfo[160], 0x38 bytes each
    SALVAGE_INFO_SIZE = 0x38u,
    SALVAGE_KIND = 0x2Fu,           // 0: chart salvage; 2-4: switch, fixed and night glints
    SALVAGE_ITEM = 0x30u,           // the item; 0x61-0x68 are the Triforce Shards
};

void bluewake_treasure_glow_set(int mode) {
    g_mode = mode < BLUEWAKE_TREASURE_GLOW_OFF || mode > BLUEWAKE_TREASURE_GLOW_ALL ? 0 : mode;
}

void bluewake_treasure_glow_attach(void) {
    const char* env = getenv("BLUEWAKE_TREASURE_GLOW");
    bluewake_treasure_glow_set(env != NULL ? atoi(env) : 0);
}

static void set_single(CPUState* cpu, u32 reg, float value) {
    // As lfs leaves it: the single's value in both halves.
    cpu->fpr[reg] = value;
    cpu->ps1[reg] = value;
}

u32 bluewake_treasure_glow_hook(CPUState* cpu) {
    const int mode = g_mode;
    if (mode == BLUEWAKE_TREASURE_GLOW_OFF)
        return 0u;
    const u32 control = mem_read32(cpu, SALVAGE_TAG_DATA);
    const u32 slot = cpu->gpr[28];
    if (control < 0x80000000u || control >= 0x81800000u || slot >= 160u)
        return 0u;
    const u32 info = control + SALVAGE_INFO + slot * SALVAGE_INFO_SIZE;
    const u8 kind = mem_read8(cpu, info + SALVAGE_KIND);
    const u8 item = mem_read8(cpu, info + SALVAGE_ITEM);
    const int triforce = item >= 0x61u && item <= 0x68u;
    int keep = 0;
    if (mode == BLUEWAKE_TREASURE_GLOW_ALL)
        keep = 1;
    else if (kind == 0u)
        keep = mode == BLUEWAKE_TREASURE_GLOW_TRIFORCE ? triforce : !triforce;
    if (keep)
        set_single(cpu, 30, 0.0f);
    return 0u;
}
