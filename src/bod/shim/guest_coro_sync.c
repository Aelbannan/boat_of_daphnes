// SPDX-License-Identifier: GPL-3.0-or-later
// Boat of Daphnes' guest_coro.h on this runtime. Its host has no way to call a guest function from host code, so a
// hook's body runs on a fiber and each call returns into the game with the link register at the hook site
// (runtime/host/src/guest_coro.c). This runtime calls guest functions directly from a hook (src/runtime/bod_api.cpp,
// bod_guest_call_cpu), so a body simply runs to its end each step; the registers are put back as the site left them,
// as its coroutine promises.
#include "guest_coro.h"

#include <stdlib.h>
#include <string.h>

struct GuestCoro {
    GuestCoroBody body;
    CPUState* cpu;
    bool running;
};

void bod_guest_call_cpu(CPUState* cpu, u32 function);  // bod_api.cpp

GuestCoro* bluewake_coro_create(GuestCoroBody body) {
    GuestCoro* co = (GuestCoro*)calloc(1, sizeof *co);
    if (co != NULL)
        co->body = body;
    return co;
}

bool bluewake_coro_active(const GuestCoro* co) { return co != NULL && co->running; }

u32 bluewake_coro_step(GuestCoro* co, CPUState* cpu, u32 site, void* arg) {
    (void)site;
    if (co == NULL || co->running)
        return 0u;
    // Everything the body's calls change comes back as the site had it.
    CPUState saved;
    memcpy(&saved, cpu, sizeof saved);
    co->cpu = cpu;
    co->running = true;
    co->body(co, arg);
    co->running = false;
    memcpy(cpu, &saved, sizeof saved);
    return 0u;
}

u32 bluewake_guest_call(GuestCoro* co, u32 function, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    CPUState* cpu = co->cpu;
    const u32 args[6] = {a0, a1, a2, a3, a4, a5};
    for (int i = 0; i < 6; ++i)
        cpu->gpr[3 + i] = args[i];
    const u32 lr = cpu->lr;
    bod_guest_call_cpu(cpu, function);
    cpu->lr = lr;
    return cpu->gpr[3];
}

CPUState* bluewake_coro_cpu(GuestCoro* co) { return co->cpu; }
