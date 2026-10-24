// The Input Viewer: an overlay of GameCube buttons in outline (A, B, X, Y, Z, Start, the L and R triggers as
// analog fills, the control stick and the C-stick inside their gates, the D-pad) that lights what the
// game is being handed on port 1: the post-mapping state PADRead returns, whether it came from the keyboard,
// a controller or the GameCube adapter.
//
// Set out as Ship of Harkinian's Input Viewer (buttons in the pad's positions over the game, scale/opacity
// options, analog values), placed like the item tracker (no frame, click-through unless dragging is on); the
// code and the drawing are this fork's own, all of it ImGui draw lists, no art.
#pragma once

namespace shipmenu {
class Menu;
}

namespace inputviewer {

// Settings > Input Viewer: show it, dragging, and its options.
void BuildMenu(shipmenu::Menu& m);
// Once a frame, in the ImGui frame: the overlay while gOpenWindows.InputViewer is 1.
void Frame(float scale, float fontScale);

} // namespace inputviewer
