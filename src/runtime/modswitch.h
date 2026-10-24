#pragma once
// Two game modules in one session: the game's (tww_game.dll) and the randomizer's (tww_game_rando.dll, wwrando's patched
// program recompiled), switched while the game runs (src/runtime/modswitch.cpp).
#include "runtime/modules.h"

#include <string>
#include <vector>

namespace rt {

extern const GameModule* g_mods[2];  // [0] the game's, [1] the randomizer's (null when it is not there)
extern int g_modCurrent;             // the one in force (g_game)
extern volatile u32* g_irqPendingPtr2;  // the other module's interrupt flag (written with the one in force's)
extern u32 (*g_relBasePtr2)[32];        // ... and its REL load bases

// Boot, once both are loaded and main.dol's functions registered: the functions that differ (tools/rando/module_diff.py's
// table beside the program) and the two programs' images (main.dol of each disc).
bool modswitchInit(const std::string& diffPath, const std::vector<u8>& dol0, const std::vector<u8>& dol1);
bool modswitchAvailable();
// The game thread, at a safe point (the file select's frame): the program in force becomes `target` - its main.dol bytes,
// its code (dispatch, overrides), the disc's files. The game's own state (heaps, the module controls) is the caller's.
bool modswitchTo(int target);

}  // namespace rt
