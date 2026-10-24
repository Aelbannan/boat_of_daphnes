#ifndef BLUEWAKE_WIDESCREEN_H
#define BLUEWAKE_WIDESCREEN_H

#include "core/cpu.h"

#include <stdbool.h>

// The picture fills any window the way Ship of Harkinian does it
// (docs/fork/research/aspect-hud.md). The renderer does most of it: the game's
// 4:3 frame is the centre of a render target the window's shape, and
// full-width draws show more of the scene at the sides (Aurora's widescreen
// policy). This module does the game's side, all from the renderer's current
// squeeze s = (4/3) / target aspect:
//   - every retrace: the fade box, the scene-change wipe and the minimap
//     widened or moved to the window's edges;
//   - at four draw functions' entries, markers in the GX stream: the pause and
//     game-over backdrops capture the whole window, and the telescope and
//     Pictobox views get black side margins. The backdrops draw the game's
//     last scene copy (mFrameBufferTex, a slice), so each retrace tells the
//     renderer its address and it keeps a window-wide copy of the scene too;
//   - the native code of the option-site hooks in mods/betterww/options.txt:
//     widescreen actor culling and the HUD at the screen edges.
// E, the HUD's edge offset, is in the game's 2D units (a 659-unit-wide box):
// E = 329.5 * (1/s - 1), negative in a window taller than 4:3.

void bluewake_widescreen_attach(CPUState* cpu);
void bluewake_widescreen_retrace(CPUState* cpu);
// The native hook ids (options.txt) this module serves.
#define BLUEWAKE_WIDESCREEN_HOOK_FIRST 6u
#define BLUEWAKE_WIDESCREEN_HOOK_LAST 11u
// J2DPane::draw's clip rectangle for a moved pane (hook 15, after 12-14 went to other options).
#define BLUEWAKE_WIDESCREEN_CLIP_HOOK 15u
// The menu cloths' projection (hook 16).
#define BLUEWAKE_WIDESCREEN_CLOTH_HOOK 16u
// J2DGrafContext::setScissor's limit on the scissor's right edge (hook 17).
#define BLUEWAKE_WIDESCREEN_SCISSOR_HOOK 17u
u32 bluewake_widescreen_native_hook(CPUState* cpu, u32 id);

// The draw functions that get a marker (virtual draws, so the dispatcher sees
// their entries).
#define BLUEWAKE_WS_MENU_CAPTURE 0x801DF684u     // dDlst_MENU_CAPTURE_c::draw
#define BLUEWAKE_WS_GAMEOVER_CAPTURE 0x8018E1CCu // dDlst_Gameover_CAPTURE_c::draw
#define BLUEWAKE_WS_TELESCOPE 0x80237568u        // dDlst_2DSCP_c::draw
#define BLUEWAKE_WS_PICTOBOX 0x8022B320u         // dJle_Pb_c::draw
extern bool bluewake_widescreen_active;
void bluewake_widescreen_enter(CPUState* cpu, u32 address);
static inline bool bluewake_widescreen_observes(u32 address) {
    return bluewake_widescreen_active &&
           (address == BLUEWAKE_WS_MENU_CAPTURE || address == BLUEWAKE_WS_GAMEOVER_CAPTURE ||
            address == BLUEWAKE_WS_TELESCOPE || address == BLUEWAKE_WS_PICTOBOX);
}
static inline void bluewake_widescreen_dispatch(CPUState* cpu, u32 address) {
    if (__builtin_expect(bluewake_widescreen_observes(address), 0))
        bluewake_widescreen_enter(cpu, address);
}

#endif
