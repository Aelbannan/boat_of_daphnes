// Reveal Sea Chart (Settings > Enhancements): the pause menu's Sea Chart drawn as if every square were discovered.
//
// Only what the chart DRAWS is changed: the visibility of its fog, island and "unexplored" panes, set from the host each
// frame while the chart is on screen (the same way puppet.c puts other players' arrows on it). The save's map bits
// (dSv_player_map_c::mFmapBits) are never written, so the save file and Anchor's team sync always carry the real progress.
// Turning the setting off puts the panes back to what the game made of the real bits.
#ifndef BLUEWAKE_CHART_REVEAL_H
#define BLUEWAKE_CHART_REVEAL_H

#include "core/cpu.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void bluewake_chart_reveal_enable(bool on); // any thread (the settings menu)
bool bluewake_chart_reveal_enabled(void);
// The play scene's draw hook (0x80234F5C), every frame, before the 2D list is drawn: sets the chart's panes and, when an
// island name needs the game's message call, starts it (returns the callee's address like the other draw-hook users, else 0).
u32 bluewake_chart_reveal_hook(CPUState* cpu, u32 site);
// A guest call of the hook is in flight: its site must be let through to it first.
bool bluewake_chart_reveal_active(void);

#ifdef __cplusplus
}
#endif

#endif
