#ifndef BLUEWAKE_VOICE_PITCH_H
#define BLUEWAKE_VOICE_PITCH_H

#include "core/cpu.h"

// Link Voice Pitch (Cosmetics Editor): the pitch of Link's voice as a ratio of the frequency (1.0 normal, 1.1 ten percent
// higher, 0.85 fifteen percent deeper).
//
// JAIZelBasic::linkVoiceStart (0x802A8748) ends in a call of JAIZelBasic::seStart (0x802A6720: id, pos, mapinfo, reverb,
// pitch f1, volume f2, fxmix f3, pan f4) with the pitch and volume both 1.0. seStart sends Link's voice ids 0x1800..0x1859
// down a path of their own (0x802A6AFC) that never reads f1, so scaling the argument does nothing. The option hook
// "link_voice_pitch" (mods/betterww/options.txt, id 34, 0x802A88FC, before the f3/f4 loads) therefore runs the seStart call
// itself as a guest coroutine, calls JAISound::setPitch on the new sound (this+0x2064) and turns the game's own call into an
// early exit (id 0xFFFFFFFF). Other ids get f1 scaled. The puppets' voices go through the same function.
#define BLUEWAKE_VOICE_PITCH_HOOK 34u

// The player's own pitch, as a ratio.
void bluewake_voice_pitch_set_local(float ratio);
// While a puppet's voice is being started (the game thread), the pitch of its owner (0: the local one).
void bluewake_voice_pitch_set_scope(float ratio);
float bluewake_voice_pitch_local(void);
// The hook: returns the guest call to make (0: go on).
u32 bluewake_voice_pitch_apply(CPUState* cpu);

#endif
