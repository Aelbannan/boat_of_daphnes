// The Randomizer header of the menu, laid out as Ship of Harkinian's (docs/fork/research/soh-randomizer.md
// 1.4) with the Wind Waker Randomizer's options (wwrando, MIT): a General page with the seed, Generate
// and the spoiler log, then one page per wwrando option group. The options come from wwrando itself
// (assets\randomizer\options.json, scripts/rando/describe_options.py); a seed is made by wwrando's own
// generator run headless (rando\ beside the exe, scripts/rando/generate_seed.py) into Randomizer\<seed>\.
#pragma once

#include <string>

namespace shipmenu {
class Menu;
}

namespace randomizer {

void BuildMenu(shipmenu::Menu& m);

// The generator, for the file select's menu (windows/src/rando_flow.cpp) as well as the General page's button. The
// options are the Randomizer header's settings.
bool EnsureLoaded();
void StartGeneration();
void RandomizeAllSettings();
bool Generating();
// A seed's game files for the DVD overlay (patch_game.py from the player's own disc, ~15 s, blocking: call it off the UI thread)
// into <dir>\overlay. "" when made, else why not.
std::string RebuildOverlay(const std::string& dir, const std::string& permalink);
// The seed's one-file form, Randomizer\<seed>.json (generation writes it; a friend drops it on the file select).
std::string SeedFileOf(const std::string& seed);
// The last generation's message ("" before the first): the seed made, or why it failed.
std::string LastResult();
// Whether the last generation succeeded since the file select was last shown (for its done or failed line).
int LastOutcome();  // 0 none yet, 1 made a seed, 2 failed
// At launch (a test hook only, for now).
void Launch();

} // namespace randomizer
