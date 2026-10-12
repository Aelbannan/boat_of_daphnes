#ifndef BLUEWAKE_HD_LOOK_H
#define BLUEWAKE_HD_LOOK_H

#include "core/cpu.h"
#include <stdbool.h>

// The Wind Waker HD look (docs/WWHD_LOOK.md): the HD release's changes to how
// the game is drawn, rebuilt on the GameCube game. Every part is off by
// default. BLUEWAKE_HD_LOOK=1 turns on every part that exists; a part's own
// variable (=1 or =0) overrides that.
//
// Distant islands (BLUEWAKE_HD_DISTANT_ISLANDS): each island's far-away model
// (d_a_lod_bg) appears and is unloaded 100,000 units further out, as in HD.
// Ambient occlusion (BLUEWAKE_HD_AO), far blur (BLUEWAKE_HD_FAR_BLUR) and
// bloom (BLUEWAKE_HD_BLOOM): Aurora's post_fx, on the finished scene before
// the HUD.
// Read again by the settings menus.
void bluewake_hd_look_reload(void);
// Once per retrace.
void bluewake_hd_look_retrace(CPUState* cpu);

extern bool bluewake_hd_distant_islands;

#endif
