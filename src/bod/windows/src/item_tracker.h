// The Item Tracker: a window of the game's own item icons laid out as the item screen is (three rows
// of seven), with rows for the equipment, the capacities (wallet, bomb bag, quiver, magic, hearts), the
// Triforce shards and pearls, the songs and each dungeon's keys, map and compass. Everything is read from
// the live save (g_dComIfG_gameInfo), and what the player does not have yet is drawn dim and grey.
//
// Laid out and set as Ship of Harkinian's Item Tracker (Randomizer > Item Tracker, the gTrackers.ItemTracker.*
// CVars) for the Wind Waker's inventory; the code is this fork's own. 2Ship2Harkinian's tracker (CC0) was
// read for its slot drawing: an invisible cell, the icon fitted into it, the count in its corner.
#pragma once

namespace shipmenu {
class Menu;
}

namespace itemtracker {

// The Randomizer header's "Item Tracker" page.
void BuildMenu(shipmenu::Menu& m);
// Once a frame, in the ImGui frame: draws the tracker while it is open (gOpenWindows.ItemTracker) and a
// game is in play. `scale` is the UI scale (sizes), `fontScale` the font's.
void Frame(float scale, float fontScale);
// The tracker's hotkey (F7): opens or closes it.
void Toggle();

} // namespace itemtracker
