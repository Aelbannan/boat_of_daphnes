// Settings > Controls > GameCube Adapter > Install Adapter Driver: binds the
// WinUSB driver to the official adapter (USB 057E:0337) with libwdi (the
// library behind Zadig), so the game can read it. The install happens only
// when the player asks for it: one UAC prompt, a self-signed driver package.
#pragma once

#include <string>

namespace adapter_driver {

// Starts the install on its own thread (the UAC prompt and the install wait
// there). Does nothing while one runs.
void Install();
bool Busy();
// One line for the menu: what the last install did, or "".
std::string Status();
// Testing, never installs: builds the driver package for a made-up adapter
// without signing it, in a temporary folder, to show the INF is made. Returns
// the first lines of the INF, or why not.
std::string DryRun();

}  // namespace adapter_driver
