// The mods folder (mods\ beside the exe), as Ship of Harkinian's mod list:
// each folder or archive (.zip, .rar, .7z, read in place) is one entry, new
// entries are enabled, and the list's order is the priority (an entry
// overrides those below it, per file). Entries are disc files served through
// the DVD overlay: loose files under res\ or Audiores\ at their disc paths,
// and wwrando-format player models (Link.arc beside metadata.txt) at the
// paths wwrando's installer writes. mods\textures keeps the Dolphin-format
// texture packs. See docs/fork/research/player-models-colors.md section 4.
#pragma once

#include "model_colors.h"

#include <string>
#include <vector>

namespace shipmenu {
class Menu;
}

namespace mods {

// At launch: the folder, the first scan, and the overlay set when the host
// opens the disc.
void Launch(const std::string& folder, bool read_only);
// Settings > Mod Menu: the list, its order and toggles, Rescan and Apply.
void BuildMenu(shipmenu::Menu& m, const char* header);
// The player model the enabled list gives (the top entry with one), "" for Link.
std::string ActiveModel();
// The files of the player model whose Link.arc the last Apply put in effect (Link.arc, metadata.txt,
// color_masks\*.png), for its colors; false when the game has the disc's Link.
bool ActiveModelFiles(model_colors::Files& out);
// What the active player model's pack holds, for sharing it with other players (Network > Anchor): each
// file's role ("model": Link.arc; "metadata": metadata.txt; "colors": color_masks\*.png; "voice": sound\*;
// "equipment": equipment\*), its path inside the pack ('/', as the pack spells it) and its bytes. False when
// the game has the disc's Link. Files over 20 MB are left out.
struct PackFile {
    std::string role, path;
    std::vector<unsigned char> bytes;
};
bool ActivePlayerPack(std::string& name, std::vector<PackFile>& out);
// fn is called after an Apply that changed the player model (the first one too, when it gives a model),
// from the thread that applied: it must not call back into mods. Colors follow the model through it.
void SetModelListener(void (*fn)());

} // namespace mods
