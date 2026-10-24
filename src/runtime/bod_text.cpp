// Ship of Harkinian's Skip Text on The Wind Waker (gEnhancements.SkipText, beside Boat of Daphnes' Text Speed, which is
// its text_speed.c): while B is held, each box is written at once and goes on by itself.
// The message's text is written by fopMsgM_msgDataProc_c::stringSet, one step a frame: it adds what the next
// character (or control code) gives and returns, its state (+0x27C) 6 while there is more. Calling it again in the same
// frame while it is 6 writes the rest now. A box goes on by itself when its auto-send flag (+0x297) is set,
// as the message's own control code does; a choice (+0x294, select) is left to wait for the player.
#include "runtime/bod.h"
#include "runtime/runtime.h"

#include <algorithm>

namespace rt {
u16 padButtons(int port);  // frontend.cpp
}

using namespace rt;

namespace {
constexpr u32 kState = 0x27C, kString = 0x3C, kCursor = 0x118, kSelect = 0x294, kAutoSend = 0x297;
constexpr u32 kStateWriting = 6;
constexpr u16 kButtonB = 0x0200;

bool writing(u32 proc) {
    if (rd8(proc + kState) != kStateWriting) return false;
    const u32 text = rd32(proc + kString);
    return text >= 0x80000000u && text < 0x81800000u && rd8(text + rd32(proc + kCursor)) != 0;
}
}  // namespace

HLE(stringSet__21fopMsgM_msgDataProc_cFv) {
    const u32 proc = c.r[3];
    const bool skip = bod::cvarInt("gEnhancements.SkipText", 0) != 0 && (padButtons(0) & kButtonB) && !rd8(proc + kSelect);
    if (skip) wr8(proc + kAutoSend, 1);
    callOriginal(c, "stringSet__21fopMsgM_msgDataProc_cFv");
    if (!skip) return;
    // (a box is a few hundred steps at most: skipping writes it all now)
    for (int extra = 2000; extra > 0 && writing(proc); extra--) {
        c.r[3] = proc;
        callOriginal(c, "stringSet__21fopMsgM_msgDataProc_cFv");
    }
}
