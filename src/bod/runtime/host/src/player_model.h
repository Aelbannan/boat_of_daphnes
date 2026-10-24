// Player models applied without a restart (docs/fork/research/player-models-
// colors.md section 3.3, approach 1): the mods list serves a new Link.arc
// through the DVD overlay, the current area is reloaded, and at the new play
// scene's first phase the resident Link archive is unmounted and mounted
// again from the overlay, before the player is created.
#ifndef BLUEWAKE_PLAYER_MODEL_H
#define BLUEWAKE_PLAYER_MODEL_H

#include "core/cpu.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUEWAKE_PLAYER_MODEL_HOOK 13u

// Hook 13, after bl checkAllWaveLoadStatus in phase_0 (dScnPly), r3 its result.
u32 bluewake_player_model_hook(CPUState* cpu);
// Whether this module has the hook (it has run at a scene start).
bool bluewake_player_model_live(void);
// Swap Link's archive at the next play scene and reload the area now (when a
// stage is in play; else at the next one).
void bluewake_player_model_swap(void);
// 0 idle, 1 waiting for the next scene, 2 swapping; -1 the last swap failed.
int bluewake_player_model_state(void);

#ifdef __cplusplus
}
#endif

#endif
