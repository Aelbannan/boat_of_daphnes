// The randomizer as the player meets it (R6, R7): Ship of Harkinian's flow with wwrando's content.
//
//   * A new file: A on an empty file opens the quest select (left and right: Vanilla / Randomizer, each with its own title
//     logo); Randomizer opens the menu Start Randomizer / Generate New Randomizer Seed / Open Randomizer Settings, with the
//     no-seed message, "Generating..." and a chime or error sound; Start goes on to the game's own name entry, which shows
//     the seed's hash. A and B go on and back. A spoiler log or seed.json dropped on the window is the seed.
//   * A file remembers its quest and seed (Save\Randomizer.slots.json): the list shows each randomizer file's quest and hash.
//     Starting a file puts the session in its quest and seed (rando_host.c's in-place switch; a module without it restarts
//     the game into the mode and comes back to the file select).
//   * Drawn by the app over the game's file-select scene in the game's own colours and layout (fs_art.cpp for the pictures),
//     driven only by the game pad (ui_pad.h); the game's own sounds come through the module hook rando_file_select.
#pragma once

#include <string>

namespace randoflow {

// At launch, before the host starts: the session's mode, its seed pack, card and mods (environment variables the host
// reads). `data_dir` ends in a backslash.
void Launch(const char* data_dir);
// Once a frame from the settings overlay: the file-select panel and the play-time guard. `scale` is the UI scale.
void Frame(float scale);
// When the host has the disc open (the DVD overlay can take files): wwrando's title logo for a randomizer session.
void OnDiscReady();
// Whether this session is a randomizer session.
bool Active();
// The active randomizer seed's folder name ("" when none).
std::string Seed();
// The active seed's seed.json ("" when none) and the folder the randomizer's per-file records live in (Save\, beside the
// sidecar): for the Check Tracker.
std::string SeedJson();
std::string SaveFolder();
// A file dropped on the window (SDL_EVENT_DROP_FILE, UI thread): on the file select, a spoiler log or a seed's seed.json
// is loaded as the seed Start Randomizer plays.
void OnDrop(const char* path);
// A seed file chosen with the Randomizer menu's Load Seed File button (any thread): loaded as a drop is, at the file
// select (at once when the game is there, else when it gets there). The fallback where drag and drop does not reach the
// window (Proton/Wine from a Linux file manager, the Steam Deck).
void OnPick(const char* path);

// The game has just written file `slot` (0 to 2) to the card on its own (the autosave, as the pause menu's save does): in a randomizer
// session the file's seed is made sure to be in the slots list, so a reload resumes it.
void OnSaved(int slot);

} // namespace randoflow
