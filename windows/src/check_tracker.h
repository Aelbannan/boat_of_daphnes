// The Check Tracker: a window listing every location of the randomizer seed in play, grouped by area (the Sea Chart's
// islands, then the dungeons, then the sea's own), each marked collected, reachable now, not reachable yet or skipped.
// Which checks are collected is read from the live save (scripts/rando/check_flags.json says which bit each location
// sets); which are reachable is answered by wwrando's own logic, run in a helper process (scripts/rando/check_logic_server.py)
// that is asked again whenever what the player has changes. Plan: docs/fork/plan-check-tracker.md.
//
// Laid out as Ship of Harkinian's and 2 Ship 2 Harkinian's check trackers (areas, collapsible, counts, hide collected,
// only reachable, skipped checks); the code is this fork's own (SoH publishes no license, 2Ship is CC0).
#pragma once

namespace shipmenu {
class Menu;
}

namespace checktracker {

// The Randomizer header's "Check Tracker" page.
void BuildMenu(shipmenu::Menu& m);
// Once a frame, in the ImGui frame: draws the window while it is open (gOpenWindows.CheckTracker) and a game is in play.
// `scale` is the UI scale (sizes), `fontScale` the font's.
void Frame(float scale, float fontScale);
// The team's save came in (Anchor, any thread): checks that turn collected in the next few seconds are a teammate's.
void NoteTeamSave();
// Opens or closes the window (hotkey F4).
void Toggle();

} // namespace checktracker
