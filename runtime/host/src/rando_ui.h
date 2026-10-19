// The randomizer's file-select menu needs the game's own sounds (the cursor, the decide and cancel tones, a chime):
// the app queues a sound number, and this hook, at the top of dFile_select_c::_move, calls mDoAud_seStart for it from
// the guest's own thread of execution. Hook 50 in mods/betterww/options.txt (option rando_file_select).
#ifndef BLUEWAKE_RANDO_UI_H
#define BLUEWAKE_RANDO_UI_H

#include "core/cpu.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUEWAKE_RANDO_UI_HOOK 50u

// The game's sound numbers (JAZelAudio_SE.h) the file select uses.
enum {
    BLUEWAKE_SE_CURSOR = 0x0830,  // JA_SE_MSEL_CURSOR
    BLUEWAKE_SE_DECIDE = 0x0831,  // JA_SE_MSEL_OK_1
    BLUEWAKE_SE_CANCEL = 0x0832,  // JA_SE_MSEL_CANCEL_1
    BLUEWAKE_SE_START = 0x0833,   // JA_SE_MSEL_ENTER_GAME
    // The fishman's chart fanfare is NOT confirmed (JA_SE_SO_MAP_ADD_FIN 0x08FF is a name-based guess with no caller in the decomp, and the
    // capture was blocked: docs/fork/rando-inhouse.md). Until it is, success sounds the decide tone.
    BLUEWAKE_SE_CHIME = 0x0831,
    BLUEWAKE_SE_SOLVED = 0x0806,  // JA_SE_READ_RIDDLE_1: the puzzle-solved / secret-found jingle (d_a_kui, d_a_obj_eskban...)
    BLUEWAKE_SE_ERROR = 0x085E,   // JA_SE_MAN_CAMERA_NG: it could not be
};
#define BLUEWAKE_SCENE_PSHIP 83  // "PShip" in JAIZelBasic::spot_dir_name (its table row: JA_BGM_PIRATE, wave set 0x0E)
// BGM sequences (the low 16 bits of JAZelAudio_BGM.h's 0x8000xxxx).
enum {
    BLUEWAKE_BGM_SELECT = 0x001E,  // JA_BGM_SELECT: the file select's music
    BLUEWAKE_BGM_PIRATE = 0x002A,  // JA_BGM_PIRATE: the pirate ship's theme
    // JA_BGM_BGN_GET_BOX: the sub-BGM d_menu_fmap's fmMapWrite (0x801BAA94) starts when the fishman's square is written into the chart. About 1.6 s
    // of notes (peaks at 934 and 1479 Hz in the disk capture, ear_5d), as the chest jingle; started with bluewake_rando_ui_subbgm.
    BLUEWAKE_BGM_CHART_FILLED = 0x005D,
};

// Any thread: starts a main BGM sequence (JAZelAudio_BGM.h's 0x8000001E is the file select's) the way mDoAud_bgmStart does.
void bluewake_rando_ui_bgm(unsigned sequence);
void bluewake_rando_ui_bgm_after_subbgm(unsigned sequence); // the same, once the sub-BGM (a jingle) has ended
// Any thread: starts a sub-BGM (a jingle) the way d_menu_fmap's fmMapWrite starts 0x8000005D.
void bluewake_rando_ui_subbgm(unsigned sequence);
// Any thread: sets the audio scene to spot_dir_name[index] (0xFFFF: none), which loads that scene's wave banks and starts its
// music: the pirate ship's (83) is the pirates' theme with its instruments, which the file select has not loaded.
void bluewake_rando_ui_scene(unsigned index);
// Any thread: plays the sound the next time the file select runs (a few are kept, the rest dropped).
void bluewake_rando_ui_sfx(unsigned sound);
// Game thread (the retrace): after an in-place switch of the randomizer, asks the hook to run the game's cCc_Init again, which builds the
// dynamic module controls (DMC) from the DynamicNameTable and the DMC array of the code now in force: the randomizer's cDyl code keeps
// its own, larger table and array, empty until this runs, and with them empty no REL is loaded again. Returns false when the hook has
// never run (a module without it): the caller then finishes the switch without. The hook calls bluewake_rando_inplace_done() after.
bool bluewake_rando_ui_rebuild_dyl(void);
// A switch from one seed to another with the randomizer on: mounts the new seed's resident archives (bmgres.arc, itemicon.arc) again. False
// when the hook has not run.
bool bluewake_rando_ui_reload_resident(void);
// Whether the hook has run in this process (the file select's frame: a module without hook 50 never does, and has no way to rebuild the
// dynamic module controls, so it cannot be switched in place).
bool bluewake_rando_ui_hook_ran(void);
// main.c: the in-place switch is complete.
void bluewake_rando_inplace_done(void);
// Whether the hook is installed in this module (the settings menu and the log say so).
u32 bluewake_rando_ui_hook(CPUState* cpu, u32 id);

#ifdef __cplusplus
}
#endif

#endif
