// Text Speed (Settings > Enhancements > Skips & Speed-ups): 1x to 5x characters per frame in a message box, live.
//
// The game's typewriter is fopMsgM_msgDataProc_c::stringSet (f_op_msg_mng.cpp, 0x800322B4): each frame it reads
// `s8 r30 = g_msgHIO.field_0x6c` and, after each ordinary character it puts out, stops when r30 is 0, otherwise counts it down.
// So a box types out (1 + field_0x6c) characters a frame; the data is 0 in the game. Waits, sounds, choices, the name and
// every other control code are handled by the same loop and still end the frame as before (a wait returns at once).
// The field is an s16 (dMeter_msg_HIO_c, d_meter.h) that the game truncates to the s8 it reads. The host writes it (g_msgHIO is 0x803E6B44, the field at +0x6C) each retrace, so no hook site is needed.
// Instant Text keeps first place: with it on the byte is left at 0 (the messages are already whole).
#ifndef BLUEWAKE_TEXT_SPEED_H
#define BLUEWAKE_TEXT_SPEED_H

#include "core/cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

void bluewake_text_speed_set(int speed);            // 1 to 5 (the CVar gEnhancements.TextSpeed), any thread
void bluewake_text_speed_retrace(CPUState* cpu);    // game thread, every retrace

#ifdef __cplusplus
}
#endif

#endif
