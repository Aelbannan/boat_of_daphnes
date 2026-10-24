#ifndef BLUEWAKE_PROF_H
#define BLUEWAKE_PROF_H

#include "core/cpu.h"

// The sampling profiler (docs/fork/performance.md). A thread of its own reads the guest CPU's pc, lr and stack
// pointer the translated code keeps in CPUState (plain loads, no suspension, nothing on the game thread's path),
// walks the guest stack in MEM1 and counts per guest function: self, inclusive, by phase of the frame (actor
// execute, actor draw, the painter, audio, game logic, the rest), by category (J3D, collision, audio, ...) and
// by actor (the REL whose code is on the stack). Every BLUEWAKE_PROF_SECS (default 10) seconds and when the stage
// changes it prints [prof] lines. Names come from guest_symbols.txt (scripts/windows/gen_guest_symbols.py).
//
//   BLUEWAKE_PROF=1            on (default: off unless bluewake_prof_default_on is set at build time)
//   BLUEWAKE_PROF=0            off
//   BLUEWAKE_PROF_HZ=N         samples a second (default 500)
//   BLUEWAKE_PROF_SECS=N       seconds between summaries (default 10)
//   BLUEWAKE_PROF_SYMBOLS=path guest_symbols.txt (default: beside the executable, then the data folder)
//   BLUEWAKE_PROF_TOP=N        rows per list (default 12)
//
// The game thread's own phase timestamps (host side: audio, present, GX submit) are bluewake_prof_phase_*.
void bluewake_prof_start(CPUState* cpu);
void bluewake_prof_stop(void);

// Cheap host-side time marks on the game thread: two QueryPerformanceCounter reads around a host phase, added to
// a per-second counter that the [prof] summary prints. Nothing happens when the profiler is off.
enum {
    BW_PROF_HOST_AUDIO = 0,   // the DSP / audio mix the host runs between guest turns
    BW_PROF_HOST_GX,          // GX FIFO submit to the renderer
    BW_PROF_HOST_PRESENT,     // end of frame / present
    BW_PROF_HOST_OTHER,
    BW_PROF_HOST_COUNT
};
extern int g_bw_prof_on;
unsigned long long bluewake_prof_now(void);
void bluewake_prof_add(int phase, unsigned long long t0);
#define BW_PROF_BEGIN() (g_bw_prof_on ? bluewake_prof_now() : 0ull)
#define BW_PROF_END(phase, t0) do { if (g_bw_prof_on) bluewake_prof_add((phase), (t0)); } while (0)

#endif
