// Cosmetics: the colors chosen in the Cosmetics Editor (windows/src/
// cosmetics_editor.cpp), held by id for the code that applies them: trail
// colors here (guest data, written every retrace), Link's clothes and the HUD
// through the renderer's texture recolor.
#ifndef BLUEWAKE_COSMETICS_H
#define BLUEWAKE_COSMETICS_H

#include "core/cpu.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A color for an id ("Trails.SwordNormal", "Link.Hero.Tunic", ...); changed =
// false returns it to the game's own.
void bluewake_cosmetics_set(const char* id, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool changed);
// The color now set for an id: false when the game's own is in use.
bool bluewake_cosmetics_get(const char* id, uint8_t rgba[4]);
// Once per retrace (guest data that the game reads where it draws).
void bluewake_cosmetics_retrace(CPUState* cpu);
// A J2D pane about to draw (the pane hook, widescreen.c): the HUD buttons'
// tints (A, X, Y, R) take the chosen colors.
void bluewake_cosmetics_pane(CPUState* cpu, u32 pane);

#ifdef __cplusplus
}
#endif

#endif
