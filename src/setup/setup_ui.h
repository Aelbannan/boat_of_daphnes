#pragma once
// The first-run window (src/setup/setup_ui.cpp): before the game starts, when its code is not built yet.
#include <filesystem>

namespace setup {

// Shows the window until the game can start: true with `disc` set (chosen there, or the remembered one), false when the
// player quit. `rememberedDisc`: `disc` holds the disc used before (the build starts at once).
bool runFirstRun(const std::filesystem::path& exeDir, std::filesystem::path& disc, bool rememberedDisc);

}  // namespace setup
