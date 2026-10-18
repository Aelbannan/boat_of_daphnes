// The Tingle Tuner's Game Boy Advance, on screen: a floating window (an ImGui window over the game) showing the
// emulated GBA's screen at 3:2, its sound, and the Settings > Tingle Tuner page's helpers. The GBA itself is the
// host's (runtime/host/src/gba_link.c); the buttons are controller port 2's (Settings > Controls > Controller
// Bindings, Port 2): A, B, Z (Select), Start, the D-pad, L and R.
#pragma once

namespace gba_window {

// 0 Off, 1 In-App GBA, 2 Real GBA (through a JoyBus bridge). Settings > Tingle Tuner > Mode.
enum Mode { kOff = 0, kInApp = 1, kReal = 2 };

// Once the overlay runs: starts the GBA if the setting says so (the host reads the environment at boot; this is the
// live path), draws the window, feeds the sound.
void Frame(float scale, float fontScale);
// The Mode setting changed: starts or stops the GBA.
void ApplyMode();
// Settings changed that the window reads (volume, scaling): nothing to do beyond the next frame; kept for symmetry.
void ApplySound();
// The status line for the settings page.
const char* Status();
// Port 2's keyboard: the default Game Boy Advance layout (arrows, X = A, Z = B, Right Shift = Select, Enter = Start,
// A = L, S = R).
void UseDefaultKeys();
// Restarts the GBA from its BIOS (the Tingle program is gone; call Tingle again).
void ResetGba();

}  // namespace gba_window
