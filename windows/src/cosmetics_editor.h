// The Cosmetics Editor (Enhancements > Cosmetics Editor > Popout Cosmetics
// Editor Window), laid out as Ship of Harkinian's: a top block of global
// controls, tabs of color groups, and per color a swatch, Random, Rainbow,
// Locked and Reset. Colors are CVars gCosmetics.<id>.Value / .Changed /
// .Rainbow / .Locked; runtime/host/src/cosmetics.c holds them for the game.
#pragma once

#include <string>

namespace cosmetics {

// Every color to the game (at launch, and after a CVar file is loaded).
void ApplyAll();
// Testing: a stock Link color preset by "hero:<name>" or "casual:<name>".
void TestApplyPreset(const std::string& spec);
// Once a frame, in the ImGui frame: the window when open, and rainbow colors.
void Frame(float scale, float fontScale);
// The editor's body, for the page in the settings menu (the window is its Popout).
void DrawEmbedded(float scale);

} // namespace cosmetics
