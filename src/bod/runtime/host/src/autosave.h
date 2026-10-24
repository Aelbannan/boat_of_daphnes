#ifndef BLUEWAKE_AUTOSAVE_H
#define BLUEWAKE_AUTOSAVE_H

#include "core/cpu.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Autosave (Settings > Enhancements): the pause menu's save, made by the host at a quiet moment, every N minutes of play and/or
// after a new item.
//
// The sequence is dMenu_save_c's (memCardDataSave and dataWrite, d_menu_save.cpp), run on guest calls from the play scene's draw
// hook (0x80234F5C, the one the puppets and the Bunny Hood use; r3 is dMenu_flag()):
//   dComIfGs_exchangePlayerRecollectionData, dSv_info_c::putSave (the stage's slot from dStage_stagInfo_GetSaveTbl),
//   dComIfGs_setGameStartStage, dSv_info_c::memory_to_card (into mDoMemCd_Ctrl_c::mData, the game's own image of the card:
//   all three files), mDoMemCdRWm_SetCheckSumGameData, mDoMemCd_Ctrl_c::save (the card thread writes the file), then
//   SaveSync each frame until the thread is done, and the menu's finish (the card id, NewFile and NoFile cleared, the
//   recollection data exchanged back).
// A moment is quiet when: the play scene runs and has been for 3 s, no event or cutscene, no menu, no scene change under way,
// Link is on his feet or at the helm of the boat (not in the air, swimming, fighting, climbing), the card thread has no
// command, and the menu itself would let the player save here (file known, card the one it was loaded from).
void bluewake_autosave_configure(int interval_minutes, bool on_new_item); // any thread; 0 minutes: no timer
void bluewake_autosave_retrace(CPUState* cpu);                            // game thread, every retrace
bool bluewake_autosave_active(void);                                      // a guest call of the save is in flight
u32 bluewake_autosave_hook(CPUState* cpu, u32 site);                      // the draw hook; returns the guest call to make or 0
// The app's poll: true once for each save that finished (file 0 to 2 in *slot, *ok whether the card took it).
bool bluewake_autosave_take_done(int* slot, bool* ok);

#ifdef __cplusplus
}
#endif

#endif
