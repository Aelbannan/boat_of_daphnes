// Naming what a teammate got, for online play's "found" toasts: the sender
// reads its own save changes (the team sync's diff, runtime/host/src/team_sync.h)
// for items, and the receiver shows the item's name and icon (the game's own
// icon from res/Msg/itemicon.arc, decoded from the disc at run time).
#pragma once

#include <imgui.h>

#include <vector>

extern "C" {
#include "team_sync.h"
}

namespace item_found {

// The save's synced bytes now, once, when sync starts (a baseline for the
// changes that need the old value, such as max life).
void SetBaseline(const BluewakeSyncChange* snapshot, unsigned count);
// The items this player's own save changes gained (item numbers, tww d_item_data.h),
// none of them junk. A burst of many (a save loaded) is not reported.
std::vector<int> FromChanges(const BluewakeSyncChange* changes, unsigned count);
// English name; "Item 0xNN" when the number has none.
const char* Name(int item);
// Rupees, hearts, ammo and other pick-ups that get no toast.
bool IsJunk(int item);
// The icon as an ImGui texture, 0 when the disc is not open yet or the item has no icon.
ImTextureID Icon(int item);
// The same, dim and grey (what the item tracker draws for an item the player does not have yet).
ImTextureID IconFaded(int item);
// Any member of res/Msg/itemicon.arc by file name ("get_rupy.bti"), normal or dim and grey. One cache for
// the toasts and the item tracker.
ImTextureID IconFile(const char* file, bool faded);

} // namespace item_found
