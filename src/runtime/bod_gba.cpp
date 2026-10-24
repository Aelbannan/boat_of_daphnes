// The Tingle Tuner's Game Boy Advance inside the app: Boat of Daphnes' gba_link.c (mGBA running the free emibios,
// GPL-3.0-or-later; src/bod/runtime/host/src) joined to the game's serial interface as its host does (its main.c
// host_si_gba_transfer, host_gba_retrace). This runtime has no SI register model - the pads are read through PADRead -
// so the two SI calls the game's GBA library makes (SIGetTypeAsync and SITransfer, dolphin/gba/GBAXfer.c) are answered
// here for the GBA's channel: a GBA is there, and each transfer goes to the emulated GBA on the guest's clock, its
// callback run from the interrupt path as the SI's transfer-complete interrupt would.
#include "runtime/bod.h"
#include "runtime/runtime.h"
#include "runtime/sched.h"

#include "gba_link.h"  // src/bod/runtime/host/src

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace rt {
u16 padButtons(int port);  // frontend.cpp
}

namespace rt::bod {
namespace {
std::atomic<int> s_channel{-1};  // the SI channel the GBA is on (-1: none)
constexpr u32 kTypeGba = 0x00040000u;      // SI_GBA
constexpr u32 kErrorNoResponse = 0x0008u;  // SI_ERROR_NO_RESPONSE

u64 guestCycles() { return timebase() * 12; }  // (the time base is the CPU clock / 12)

// The GBA's buttons from its channel's pad, KEYINPUT order, as Dolphin maps a GameCube pad to the GBA:
// A B Select(Z) Start Right Left Up Down R L.
u16 gbaKeys(u16 b) {
    u16 keys = 0;
    if (b & 0x0100) keys |= 1u << 0;
    if (b & 0x0200) keys |= 1u << 1;
    if (b & 0x0010) keys |= 1u << 2;
    if (b & 0x1000) keys |= 1u << 3;
    if (b & 0x0002) keys |= 1u << 4;
    if (b & 0x0001) keys |= 1u << 5;
    if (b & 0x0008) keys |= 1u << 6;
    if (b & 0x0004) keys |= 1u << 7;
    if (b & 0x0020) keys |= 1u << 8;
    if (b & 0x0040) keys |= 1u << 9;
    return keys;
}
}  // namespace

// Each retrace, on the game thread: the GBA's keys, and its clock caught up to the guest's.
void gbaRetrace() {
    const int ch = s_channel;
    if (ch < 0 || !gba_link_running()) return;
    gba_link_set_keys(gbaKeys(padButtons(ch)));
    gba_link_sync(guestCycles());
}

}  // namespace rt::bod

using namespace rt;

// SIGetTypeAsync(chan, callback): the GBA's channel answers that a GBA is there.
HLE(SIGetTypeAsync) {
    const s32 chan = s32(c.r[3]);
    const u32 callback = c.r[4];
    if (chan != bod::s_channel || !gba_link_running()) return callOriginal(c, "SIGetTypeAsync");
    if (callback)
        sched::postIrq([chan, callback](Ctx& ic) {
            ic.r[3] = u32(chan);
            ic.r[4] = bod::kTypeGba;
            ic.call(callback);
        });
    c.r[3] = bod::kTypeGba;
}

// SITransfer(chan, output, outputBytes, input, inputBytes, callback, delay): to the emulated GBA.
HLE(SITransfer) {
    const s32 chan = s32(c.r[3]);
    if (chan != bod::s_channel || !gba_link_running()) return callOriginal(c, "SITransfer");
    const u32 output = c.r[4], outBytes = c.r[5], input = c.r[6], inBytes = c.r[7], callback = c.r[8];
    u8 out[8] = {}, in[8] = {};
    const u32 n = std::min<u32>(outBytes, sizeof out);
    for (u32 i = 0; i < n; i++) out[i] = u8(rd8(output + i));
    const int got = gba_link_transfer(bod::guestCycles(), out, n, in);
    for (u32 i = 0; got > 0 && i < std::min<u32>(inBytes, u32(got)); i++) wr8(input + i, in[i]);
    const u32 sr = got > 0 ? 0u : bod::kErrorNoResponse;
    if (callback)
        sched::postIrq([chan, sr, callback](Ctx& ic) {
            ic.r[3] = u32(chan);
            ic.r[4] = sr;
            ic.r[5] = 0;  // (the interrupted context: the GBA library does not read it)
            ic.call(callback);
        });
    c.r[3] = 1;
}

// Its settings menu's Tingle Tuner mode: 1 the in-app GBA (on channel 1, the second controller port), else off. (Its
// "Real GBA" mode, a JoyBus bridge to a GBA cable, has no host here.)
extern "C" int bluewake_gba_set_mode(int mode) {
    if (mode == 1) {
        if (!gba_link_running() && !gba_link_start(std::getenv("BLUEWAKE_GBA_BIOS"))) {
            log("[gba] the in-app GBA did not start\n");
            return 0;
        }
        bod::s_channel = 1;
        log("[gba] the in-app GBA is on channel 1\n");
        return 1;
    }
    bod::s_channel = -1;
    gba_link_stop();
    return 0;
}
extern "C" int bluewake_gba_channel(void) { return bod::s_channel; }
