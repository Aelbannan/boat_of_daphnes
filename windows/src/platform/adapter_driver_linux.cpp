// The GameCube adapter's driver install (adapter_driver.h) on Linux: there is none to make. The adapter is read through
// hidraw by SDL (gc_adapter.c), which needs a udev rule that lets the player's user open the device
// (docs/LINUX.md: SUBSYSTEM=="hidraw", ATTRS{idVendor}=="057e", ATTRS{idProduct}=="0337", TAG+="uaccess"), so the menu's
// button does nothing and says so.
#include "adapter_driver.h"

namespace adapter_driver {

void Install() {}

bool Busy() { return false; }

std::string Status() { return "Linux needs no driver: allow your user to open the adapter with a udev rule."; }

std::string DryRun() { return "no driver package on Linux"; }

}  // namespace adapter_driver
