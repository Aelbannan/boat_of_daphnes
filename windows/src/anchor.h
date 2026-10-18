// Online play (Network > Anchor), as Ship of Harkinian's Anchor: a room on the
// server (server/anchor), players with a name, color and team, and the Anchor
// Room window listing them with where they are. Item and flag sync and other
// Links in the world come next (docs/fork/PLAN.md N3).
#pragma once

namespace shipmenu {
class Menu;
}

namespace online {

// The Network header's pages.
void BuildMenu(shipmenu::Menu& menu);
// Connects at launch when it was connected last time.
void Launch();
// Once a frame: messages in and out, and the Anchor Room window when open.
void Frame(float scale, float fontScale);
// The chat hotkey's input line over the game: while it is open win_settings blocks the game's pad and keyboard, and
// Esc closes it.
bool ChatOverlayOpen();
void CloseChatOverlay();
// At exit.
void Shutdown();

} // namespace online
