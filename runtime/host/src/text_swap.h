// Two live rewrites of the game's message file (BMG), by one mechanism:
//  - Crenando (Enhancements > Crenando): every message that names Ganondorf or Ganon shows "Crenando" instead.
//  - Player Gender (Cosmetics Editor > Link > Player Gender = Female): the messages that speak of the player (an explicit
//    list of ids and phrases, player_gender_rules.inc) say she, her, big sister, Ms. and so on.
// The rewritten texts live in a host-owned guest buffer (the archive heap) and the message file's INF1 entry is pointed at
// them, as the Bunny Hood does for its own texts (bunny_hood.c); off, every entry gets its own offset back. A randomizer
// seed's message file, mounted again, is read again.
#ifndef BLUEWAKE_TEXT_SWAP_H
#define BLUEWAKE_TEXT_SWAP_H

#include "core/cpu.h"
#include "guest_coro.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The app's switch (CVar gEnhancements.Crenando), live.
void bluewake_text_swap_enable(bool on);
// The app's switch (CVar gCosmetics.PlayerGender = 1), live.
void bluewake_text_swap_set_female(bool on);
bool bluewake_text_swap_enabled(void); // either switch
// Once per retrace on the game thread (game_options.c).
void bluewake_text_swap_retrace(CPUState* cpu);
// The draw hook's fiber (bunny_hood.c): true when it has a guest call to make (the buffer to get, a test message to show).
bool bluewake_text_swap_wants_fiber(void);
void bluewake_text_swap_fiber(GuestCoro* co, CPUState* cpu);

#ifdef __cplusplus
}
#endif

#endif
