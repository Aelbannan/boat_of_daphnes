// OS library replacements. Everything not here runs as recompiled game code; these are the parts
// that touch the CPU's interrupt state, the scheduler, timers, the RTC/SRAM or module linking.
#include "runtime/bod_bridge.h"
#include "runtime/modswitch.h"
#include "runtime/runtime.h"
#include "runtime/sched.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>

namespace rt {

namespace {
constexpr u32 kEE = 0x8000;
u64 arg64(const Ctx& c, int hiReg) { return u64(c.r[hiReg]) << 32 | c.r[hiReg + 1]; }
}  // namespace

// ---- console output ----
void zeldaLogNext(int n);  // src/runtime/zelda_dsp.cpp
HLE(OSReport) {
    const std::string s = guestFormat(c, c.r[3], 4, 1);
    log("%s", s.c_str());
    // (bring-up, SA2_ZELDA_LOG: JAudio losing a DSP message - the mails that follow)
    if (s.find("Send Miss") != std::string::npos && std::getenv("SA2_ZELDA_LOG")) zeldaLogNext(80);
}

// ---- interrupt enable state (MSR[EE] of the running context) ----
HLE(OSDisableInterrupts) {
    const u32 old = (c.msr & kEE) ? 1 : 0;
    c.msr &= ~kEE;
    c.r[3] = old;
}
HLE(OSEnableInterrupts) {
    const u32 old = (c.msr & kEE) ? 1 : 0;
    c.msr |= kEE;
    c.r[3] = old;
    c.poll();
}
HLE(OSRestoreInterrupts) {
    const u32 old = (c.msr & kEE) ? 1 : 0;
    if (c.r[3]) c.msr |= kEE;
    else c.msr &= ~kEE;
    c.r[3] = old;
    if (c.msr & kEE) c.poll();
}

// ---- hardware init the host does not need (called from OSInit) ----
// (EXIInit runs natively: the EXI bus is emulated in src/runtime/exi.cpp.)
HLE(SIInit) {}
HLE(__OSInitAudioSystem) {}
// Memory protection/BAT setup runs in real mode through an rfi trampoline: nothing to do natively.
HLE(__OSInitMemoryProtection) {}
HLE(RealMode) {}

// ---- threads ----
HLE(__OSThreadInit) {
    callOriginal(c, "__OSThreadInit");  // builds the default thread and sets __OSCurrentThread
    sched::adoptBootThread(rd32(sched::kCurrentThread), c);
}
HLE(OSCreateThread) {
    c.r[3] = sched::create(c.r[3], c.r[4], c.r[5], c.r[6], c.r[7], s32(c.r[8]), c.r[9]) ? 1 : 0;
}
HLE(OSResumeThread) { c.r[3] = u32(sched::resume(c.r[3])); }
HLE(OSSuspendThread) { c.r[3] = u32(sched::suspend(c.r[3])); }
HLE(OSSleepThread) { sched::sleepOn(c.r[3]); }
HLE(OSWakeupThread) { sched::wakeup(c.r[3]); }
HLE(OSExitThread) { sched::exitThread(c.r[3]); }
HLE(OSCancelThread) { sched::cancel(c.r[3]); }
HLE(OSJoinThread) {
    u32 val = 0;
    const u32 out = c.r[4];
    c.r[3] = sched::join(c.r[3], &val) ? 1 : 0;
    if (out) wr32(out, val);
}
HLE(OSSetThreadPriority) {
    if (s32(c.r[4]) < 0 || s32(c.r[4]) > 31) {
        c.r[3] = 0;
        return;
    }
    sched::setPriority(c.r[3], s32(c.r[4]));
    c.r[3] = 1;
}
HLE(OSDisableScheduler) {
    sched::disableScheduler();
    c.r[3] = 0;
}
HLE(OSEnableScheduler) {
    sched::enableScheduler();
    c.r[3] = 0;
}
HLE(__OSReschedule) {}
HLE(SelectThread) {}
HLE(UnsetRun) {}
// (thread, priority): priority inheritance, from __OSPromoteThread when a thread waits for a mutex
// another holds. The scheduler reads each thread's effective priority from its OSThread, so setting
// it is all; the result is the next thread up the chain - the owner of the mutex this one waits on
// itself - or NULL, as the OS has it (returning the thread itself made __OSPromoteThread loop).
HLE(SetEffectivePriority) {
    const u32 t = c.r[3];
    wr32(t + sched::T_PRIORITY, c.r[4]);
    const u32 mutex = rd16(t + sched::T_STATE) == sched::S_WAITING ? rd32(t + sched::T_MUTEX) : 0;
    c.r[3] = mutex ? rd32(mutex + 8) : 0;  // OSMutex: queue (8 bytes), then the owner thread
}

// ---- alarms (OSTime arguments arrive in odd/even register pairs) ----
HLE(OSSetAlarm) {  // (alarm r3, tick r5:r6, handler r7)
    sched::setAlarm(c.r[3], timebase() + arg64(c, 5), 0, c.r[7]);
}
HLE(OSSetPeriodicAlarm) {  // (alarm r3, start r5:r6, period r7:r8, handler r9)
    const u64 start = arg64(c, 5), period = arg64(c, 7);
    u64 first = start;
    const u64 now = timebase();
    if (period && first <= now) first += ((now - first) / period + 1) * period;
    sched::setAlarm(c.r[3], first, period, c.r[9]);
}
HLE(OSCancelAlarm) { sched::cancelAlarm(c.r[3]); }

// ---- SRAM (normally read over EXI from the RTC chip): a valid, English, stereo setup ----
// It is placed in the SDK's own SRAM control block (Scb: OSSram, then OSSramEx), because some SDK
// functions read it there directly instead of locking it (OSGetSoundMode: with the block left
// empty the game saw a mono console and mixed its stereo music to mono). Scb's address is taken
// from OSGetSoundMode's code: lis r3, hi / ... / addi r31, r3, lo.
namespace {
u32 s_sram = 0, s_sramEx = 0;
u32 findScb() {
    const u32 f = guestSymbol("OSGetSoundMode");
    if (!f) return 0;
    const u32 lis = rd32(f + 4), addi = rd32(f + 0x14);
    if ((lis >> 26) != 15 || (addi >> 26) != 14) return 0;
    return (lis << 16) + u32(s32(s16(addi & 0xFFFF)));
}
void sramInit() {
    if (s_sram) return;
    s_sram = findScb();
    if (!s_sram) {
        log("[os] SRAM control block not found; SRAM readers outside __OSLockSram see nothing\n");
        s_sram = hleAlloc(0x40);
    }
    s_sramEx = s_sram + 0x14;  // OSSramEx follows the 20-byte OSSram
    wr8(s_sram + 0x12, 0);     // language: English
    wr8(s_sram + 0x13, 0x04);  // flags: stereo sound
    // Memory card flash IDs (all zero) and their checksums (~sum), checked when a card mounts;
    // tools/mkcard.py formats card images for this ID.
    wr8(s_sramEx + 0x26, 0xFF);
    wr8(s_sramEx + 0x27, 0xFF);
}
}  // namespace
HLE(__OSInitSram) { sramInit(); }
HLE(__OSLockSram) {
    sramInit();
    c.r[3] = s_sram;
}
HLE(__OSLockSramEx) {
    sramInit();
    c.r[3] = s_sramEx;
}
HLE(__OSUnlockSram) { c.r[3] = 1; }
HLE(__OSUnlockSramEx) { c.r[3] = 1; }
HLE(__OSSyncSram) { c.r[3] = 1; }

// ---- REL modules: the SDK's OSLink applies the relocations; then the recompiled code of the
// module is registered at the addresses the game loaded it to. ----
// A module just linked (OSLink, OSLinkFixed): its sections' load addresses, and its functions
// registered there.
static void moduleLinked(Ctx& c, u32 module) {
    const u32 id = rd32(module);
    const u32 nsec = rd32(module + 0x0C);
    const u32 info = rd32(module + 0x10);  // absolute once linked
    if (id >= kMaxModules || nsec > 32) fatal("OSLink: bad module header at %08X", module);
    for (u32 i = 0; i < nsec; i++) g_relBase[id][i] = g_relBasePtr2[id][i] = rd32(info + i * 8) & ~1u;  // (both modules')
    registerRelModule(id);
    if (std::getenv("SA2_LINK_STACK")) {  // who loaded it: the guest back chain (LR save words)
        std::string chain;
        u32 sp = c.r[1];
        for (int i = 0; i < 12 && (sp >> 24) == 0x80 + ((sp >> 24) & 1); i++) {
            sp = rd32(sp);
            if (!sp) break;
            char b[16];
            std::snprintf(b, sizeof b, " %08X", rd32(sp + 4));
            chain += b;
        }
        log("[rel] module %u linked from lr %08X; stack%s\n", id, c.lr, chain.c_str());
    }
}
HLE(OSLink) {
    const u32 module = c.r[3];
    callOriginal(c, "OSLink");
    if (c.r[3]) moduleLinked(c, module);
}
HLE(OSLinkFixed) {  // (the game's DynamicModuleControl links this way)
    const u32 module = c.r[3];
    callOriginal(c, "OSLinkFixed");
    if (c.r[3]) moduleLinked(c, module);
}
HLE(OSUnlink) {
    const u32 id = rd32(c.r[3]);
    if (id < kMaxModules) unregisterRelModule(id);
    callOriginal(c, "OSUnlink");
}

// ---- arena (logged) ----
HLE(OSSetArenaHi) {
    log("[os] OSSetArenaHi(%08X) lr %08X\n", c.r[3], c.lr);
    callOriginal(c, "OSSetArenaHi");
}
HLE(OSInitAlloc) {
    log("[os] OSInitAlloc(%08X, %08X, %d) lr %08X\n", c.r[3], c.r[4], s32(c.r[5]), c.lr);
    callOriginal(c, "OSInitAlloc");
}
HLE(OSCreateHeap) {
    const u32 lo = c.r[3], hi = c.r[4];
    callOriginal(c, "OSCreateHeap");
    log("[os] OSCreateHeap(%08X, %08X) -> %d lr %08X\n", lo, hi, s32(c.r[3]), c.lr);
}

// ---- thread switch callback: the OS's own bookkeeping runs, and the scheduler calls the function
// at each switch (src/runtime/sched.cpp) ----
HLE(OSSetSwitchThreadCallback) {
    const u32 fn = c.r[3];
    callOriginal(c, "OSSetSwitchThreadCallback");
    sched::setSwitchCallback(fn);
    log("[os] OSSetSwitchThreadCallback(%08X) lr %08X\n", fn, c.lr);
}

// ---- reset ----
// As Boat of Daphnes' runtime_fixes hook 41: the app starts the game again (bluewake_host_reset_requested), as a console
// reboots; the game thread waits here meanwhile.
HLE(OSResetSystem) {
    log("[os] OSResetSystem(%u, %08X, %u): restarting\n", c.r[3], c.r[4], c.r[5]);
    bod::g_resetRequested = true;
    for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

}  // namespace rt
