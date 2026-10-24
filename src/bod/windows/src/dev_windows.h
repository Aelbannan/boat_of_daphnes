// Dev Tools: the Save Editor, Flag Viewer, Actor Viewer, Console and Stats
// windows, as SoH's Developer Tools have them, over Wind Waker's memory
// (runtime/host/src/dev_tools.h). Each opens from its page under the Dev Tools
// header (gOpenWindows.SaveEditor, .FlagViewer, .ActorViewer, .Console,
// .Stats).
#pragma once

#include <string>

namespace shipmenu {
class Menu;
}

namespace devtools {

// The sidebar pages (a button for each window) under `header`.
void BuildMenu(shipmenu::Menu& menu, const char* header);
// Draws the windows that are open.
void Frame(float scale, float fontScale);
// Runs one console command and returns what it printed (testing: the
// BLUEWAKE_TEST_CONSOLE variable runs its commands, `;` apart, a second in).
std::string RunCommand(const std::string& line);
void TestRun();

}  // namespace devtools
