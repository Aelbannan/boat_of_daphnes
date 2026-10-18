// Settings > Controls > Controller Bindings: SoH's Input Editor for this port.
// A window with a tab for each of the four ports: which controller drives the
// port, a binding for each GameCube button and analog direction (click it,
// then press the control), dead zones, and the keyboard's keys. The mappings
// are Aurora's own and are saved by it (PADSerializeMappings). Also here: the
// camera inversion on top of those mappings, "First Connected Port Is Player
// 1", and which ports the keyboard drives.
#pragma once

union SDL_Event;

namespace input_editor {

// Draws the window when gOpenWindows.ControllerBindings is set.
void Frame(float scale, float fontScale);
// The bindings' body, for the page in the settings menu (the window is its Popout).
void DrawEmbedded(float scale);
// Settings > Tingle Tuner: the GBA's controller (Keyboard, None or a connected pad; the Nintendo Switch Online pads are
// marked) and its buttons as the GBA names them. The GBA is controller port 2's.
void DrawGbaController(float scale);
// Every few frames: a controller that arrived or left starts from the saved
// mapping, gets the camera inversion, and the port rule is applied.
void Poll();
// Settings > Controls > Invert Camera X/Y Axis changed.
void ApplyCamera();
// Settings > Controls > First Connected Port Is Player 1 changed.
void ApplyPorts();
// Which ports the keyboard drives; the menu being open takes port 1's away.
void ApplyKeyboard(bool menuOpen);
// An SDL event: true when the editor takes it (a key being bound).
bool OnEvent(const SDL_Event* event);
// Testing: BLUEWAKE_TEST_VIRTUAL_PAD="<port>[,<SDL button to hold>]" attaches a virtual
// pad and puts it on that port (0-3); BLUEWAKE_TEST_BIND="<GameCube button bit>" then
// starts binding that button, which the held button answers.
void TestSetup();
// The text for Settings > Controls: a line on the ports and their controllers.
const char* Summary();

}  // namespace input_editor
