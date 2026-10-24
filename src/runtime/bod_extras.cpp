// Boat of Daphnes' player models without a restart (its runtime/host/src/player_model.c, GPL-3.0-or-later) on this
// runtime. Its hook 13 sits in the play scene's phase_0 (after JAInter::BankWave::checkAllWaveLoadStatus): when a swap is
// asked for (the Cosmetics Editor's player model, its new Link.arc already in the disc overlay), Link's archive is
// deleted and mounted again there, before any actor of the scene uses it. Its host calls the game by returning into
// each function with the link register at the hook (it has no other way); here the calls are made directly, and the
// steps that wait (Link's archive still held, still loading) let phase_0 wait a frame the way it does: r3 = 1.
#include "runtime/bod.h"
#include "runtime/bod_bridge.h"
#include "runtime/runtime.h"

#include <atomic>

namespace rt::bod {

namespace {
enum : u32 {
    SET_RES = 0x8006EF34u,     // dRes_control_c::setRes(name, info, count, path, direction, heap)
    SYNC_RES = 0x8006F01Cu,    // dRes_control_c::syncRes(name, info, count): 1 loading, 0 done, -1 failed
    DELETE_RES = 0x8006F074u,  // dRes_control_c::deleteRes(name, info, count)
    OBJECT_INFO = 0x803E0BC8u, // g_dComIfG_gameInfo.mResControl.mObjectInfo[64]
    INFO_SIZE = 0x24u,         // dRes_info_c: mArchiveName[14], mCount (u16) at +0x0E
    INFO_COUNT = 64u,
    STR_LINK = 0x8035A0D8u,    // "Link" in main.dol's string data
    STR_OBJECT = 0x8034F5A7u,  // "/res/Object/"
    WAIT_LIMIT = 300,          // scene starts' frames to wait for Link's count to drop to 1
};
enum Step { STEP_IDLE, STEP_PENDING, STEP_SYNC };

std::atomic<int> s_request{0};
std::atomic<bool> s_live{false};
std::atomic<int> s_state{0};  // 1 asked for, 0 swapped, -1 failed
Step s_step = STEP_IDLE;
int s_wait = 0;

int linkCount() {
    for (u32 i = 0; i < INFO_COUNT; i++) {
        const u32 info = OBJECT_INFO + i * INFO_SIZE;
        const u16 count = u16(rd16(info + 0x0E));
        if (count && rd32(info) == 0x4C696E6Bu && rd8(info + 4) == 0) return count;  // "Link\0"
    }
    return 0;
}

// A call of the game's, the hook's registers as they were after it (r3 its result, given back).
u32 call(Ctx& c, u32 fn, std::initializer_list<u32> args) {
    const Ctx saved = c;
    const u32 r = guestCall(c, fn, args);
    c = saved;
    return r;
}
}  // namespace

bool playerModelLive() { return s_live; }
void playerModelRequest() {
    s_request = 1;
    s_state = 1;
}
int playerModelState() {
    const int s = s_state;
    return s == 1 && s_step != STEP_IDLE ? 2 : s;
}

u32 playerModelHook(Ctx& c) {
    s_live = true;
    if (s_step == STEP_IDLE) {
        if (s_request.exchange(0) == 0) return 0;
        s_step = STEP_PENDING;
        s_wait = 0;
    }
    if (s_step == STEP_PENDING) {
        const int count = linkCount();
        if (count != 1) {
            // Some actor of the last scene still holds it (or it is not loaded): again next frame, then give up.
            if (++s_wait > WAIT_LIMIT || count == 0) {
                log("[model] Link's archive is held (count %d): not swapped; the model changes at the next start\n", count);
                s_step = STEP_IDLE;
                s_state = -1;
                return 0;
            }
            c.r[3] = 1;  // phase_0 waits (cPhs_INIT_e)
            return 0;
        }
        call(c, DELETE_RES, {STR_LINK, OBJECT_INFO, INFO_COUNT});
        // (direction 0, as the logo scene mounts it; no heap: the archive heap)
        if (call(c, SET_RES, {STR_LINK, OBJECT_INFO, INFO_COUNT, STR_OBJECT, 0u, 0u}) != 1) {
            log("[model] mounting the new Link.arc failed\n");
            s_step = STEP_IDLE;
            s_state = -1;
            c.r[3] = 1;
            return 0;
        }
        s_step = STEP_SYNC;
    }
    const s32 result = s32(call(c, SYNC_RES, {STR_LINK, OBJECT_INFO, INFO_COUNT}));
    if (result > 0) {  // still loading: phase_0 waits a frame, then syncs again
        c.r[3] = 1;
        return 0;
    }
    s_step = STEP_IDLE;
    s_state = result == 0 ? 0 : -1;
    log("[model] Link's archive %s\n", result == 0 ? "swapped" : "failed to load");
    c.r[3] = 1;  // one more frame before the scene goes on
    return 0;
}

}  // namespace rt::bod
