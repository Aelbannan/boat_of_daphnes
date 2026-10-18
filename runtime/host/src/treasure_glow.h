// Enhancements > Items > Sailing > Treasure Light Up Close: the light beam over
// sunken treasure (daSalvage_c, d_a_salvage.rel) fades out as the boat comes
// near it; this keeps it on for the kinds chosen. docs/fork/research/treasure-glow.md.
#ifndef BLUEWAKE_TREASURE_GLOW_H
#define BLUEWAKE_TREASURE_GLOW_H

#include "core/cpu.h"

#define BLUEWAKE_TREASURE_GLOW_HOOK 12u

enum {
    BLUEWAKE_TREASURE_GLOW_OFF = 0,
    BLUEWAKE_TREASURE_GLOW_TREASURE = 1, // Treasure Chart salvage
    BLUEWAKE_TREASURE_GLOW_TRIFORCE = 2, // Triforce Chart salvage (the Triforce Shards)
    BLUEWAKE_TREASURE_GLOW_ALL = 3,      // every salvage light, the plain glints too
};

// The menu (live) and the launch (BLUEWAKE_TREASURE_GLOW=0..3).
void bluewake_treasure_glow_set(int mode);
void bluewake_treasure_glow_attach(void);
// Hook 12, before fmuls f0,f0,f30 in daSalvage_c::calcAlpha.
u32 bluewake_treasure_glow_hook(CPUState* cpu);

#endif
