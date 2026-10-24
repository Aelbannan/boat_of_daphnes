// Live switch sync for Anchor (as Ship of Harkinian's scene-flag sync): a switch the game sets or clears here
// (dSv_info_c::onSwitch / offSwitch, module hooks 37 and 38) goes to the teammates, and theirs are set here, so doors,
// gates and puzzles that watch a switch react while both players are in the same place.
#ifndef BLUEWAKE_SWITCH_SYNC_H
#define BLUEWAKE_SWITCH_SYNC_H

#include "core/cpu.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BluewakeSwitchEvent {
    char stage[9];
    int8_t room;
    uint8_t no;
    bool on;
} BluewakeSwitchEvent;

// On while connected to a room that syncs, with a save in play (the app decides, as for team sync).
void bluewake_switch_sync_enable(bool on);
// The switches this game set or cleared since the last call (the app sends them).
unsigned bluewake_switch_sync_take(BluewakeSwitchEvent* out, unsigned max);
// A teammate's switch: set here at the next retrace if this game is on that stage (the app's thread).
void bluewake_switch_sync_give(const BluewakeSwitchEvent* event);

// The module hooks at the entries of onSwitch (r4 the switch, r5 the room) and offSwitch.
uint32_t bluewake_switch_sync_hook(CPUState* cpu, bool on);
// The game thread, once a retrace: the teammates' switches.
void bluewake_switch_sync_retrace(CPUState* cpu);

#ifdef __cplusplus
}
#endif

#endif
