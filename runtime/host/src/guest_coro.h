// Calling guest functions from a native hook, written as straight-line host
// code. The hook's body runs on a fiber; each guest_call hands the callee's
// address back to the game (the "redirect trick" of player_model.c: the link
// register is the hook site, so the hook runs again when the callee returns,
// r3 its result) and the fiber goes on from there with that result.
//
// A step is bracketed by a register snapshot: when the body finishes, every
// register is as the site left it, so the hook can sit anywhere, not only
// where the volatile registers are dead.
#ifndef BLUEWAKE_GUEST_CORO_H
#define BLUEWAKE_GUEST_CORO_H

#include "core/cpu.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GuestCoro GuestCoro;
typedef void (*GuestCoroBody)(GuestCoro* co, void* arg);

GuestCoro* bluewake_coro_create(GuestCoroBody body);
// Whether a body is mid-way (waiting for a callee): its hook site must be let through.
bool bluewake_coro_active(const GuestCoro* co);
// From the hook at `site`. Starts the body with `arg` when idle; resumes it
// when a callee has returned to the site. Returns 0 when the body is done
// (continue the game normally), or the callee's address to continue at.
u32 bluewake_coro_step(GuestCoro* co, CPUState* cpu, u32 site, void* arg);

// From inside the body: call a guest function (up to 6 integer arguments) and
// return its r3.
u32 bluewake_guest_call(GuestCoro* co, u32 function, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);
CPUState* bluewake_coro_cpu(GuestCoro* co);

#ifdef __cplusplus
}
#endif

#endif
