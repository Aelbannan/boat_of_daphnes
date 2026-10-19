#include "guest_coro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A body runs on its own stack and hands guest calls back to the loop that stepped it: Win32 fibers on Windows, ucontext on
 * Linux x86-64. Elsewhere there are none (the stubs below). */
#if defined(_WIN32)
#include <windows.h>
#define GUEST_CORO_FIBERS 1
#elif defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <ucontext.h>
#define GUEST_CORO_FIBERS 1
#endif

#ifdef GUEST_CORO_FIBERS

struct GuestCoro {
    GuestCoroBody body;
    void* arg;
#ifdef _WIN32
    LPVOID fiber;
    LPVOID caller;
#else
    ucontext_t fiber;
    ucontext_t caller;
    void* stack; /* the fiber's stack, mapped when first stepped */
#endif
    CPUState* cpu;
    u32 site;
    u32 sp;
    bool active;  // a body is running or waiting for a callee
    bool waiting; // handed a call to the game, not yet returned
    bool done;
    u32 pending_function;
    u32 args[6];
    u32 result;
    // The site's registers.
    u32 gpr[32];
    f64 fpr[32];
    f64 ps1[32];
    u32 lr, ctr, cr, xer, fpscr;
};

#define CORO_STACK_SIZE (512u * 1024u)

#ifdef _WIN32
static void CALLBACK fiber_main(LPVOID param) {
    GuestCoro* co = (GuestCoro*)param;
    for (;;) {
        co->body(co, co->arg);
        co->done = true;
        SwitchToFiber(co->caller);
    }
}
#else
/* makecontext passes ints: the pointer comes in as two halves. */
static void fiber_main(unsigned low, unsigned high) {
    GuestCoro* co = (GuestCoro*)(((uintptr_t)high << 32) | (uintptr_t)low);
    for (;;) {
        co->body(co, co->arg);
        co->done = true;
        swapcontext(&co->fiber, &co->caller);
    }
}
#endif

GuestCoro* bluewake_coro_create(GuestCoroBody body) {
    GuestCoro* co = (GuestCoro*)calloc(1, sizeof *co);
    if (co != NULL)
        co->body = body;
    return co;
}

bool bluewake_coro_active(const GuestCoro* co) { return co != NULL && co->active; }

CPUState* bluewake_coro_cpu(GuestCoro* co) { return co->cpu; }

static void save_regs(GuestCoro* co, const CPUState* cpu) {
    memcpy(co->gpr, cpu->gpr, sizeof co->gpr);
    memcpy(co->fpr, cpu->fpr, sizeof co->fpr);
    memcpy(co->ps1, cpu->ps1, sizeof co->ps1);
    co->lr = cpu->lr;
    co->ctr = cpu->ctr;
    co->cr = cpu->cr;
    co->xer = cpu->xer;
    co->fpscr = cpu->fpscr;
}

static void restore_regs(const GuestCoro* co, CPUState* cpu) {
    memcpy(cpu->gpr, co->gpr, sizeof co->gpr);
    memcpy(cpu->fpr, co->fpr, sizeof co->fpr);
    memcpy(cpu->ps1, co->ps1, sizeof co->ps1);
    cpu->lr = co->lr;
    cpu->ctr = co->ctr;
    cpu->cr = co->cr;
    cpu->xer = co->xer;
    cpu->fpscr = co->fpscr;
}

u32 bluewake_coro_step(GuestCoro* co, CPUState* cpu, u32 site, void* arg) {
    if (co == NULL)
        return 0u;
    if (!co->active) {
#ifdef _WIN32
        if (co->caller == NULL) {
            co->caller = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(NULL);
            if (co->caller == NULL)
                return 0u;
        }
        if (co->fiber == NULL) {
            co->fiber = CreateFiber(CORO_STACK_SIZE, fiber_main, co);
            if (co->fiber == NULL)
                return 0u;
        }
#else
        if (co->stack == NULL) {
            void* stack = mmap(NULL, CORO_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
            if (stack == MAP_FAILED || getcontext(&co->fiber) != 0)
                return 0u;
            co->stack = stack;
            co->fiber.uc_stack.ss_sp = stack;
            co->fiber.uc_stack.ss_size = CORO_STACK_SIZE;
            co->fiber.uc_link = NULL;
            makecontext(&co->fiber, (void (*)(void))fiber_main, 2, (unsigned)((uintptr_t)co & 0xFFFFFFFFu),
                        (unsigned)((uintptr_t)co >> 32));
        }
#endif
        save_regs(co, cpu);
        co->cpu = cpu;
        co->site = site;
        co->sp = cpu->gpr[1];
        co->arg = arg;
        co->active = true;
        co->waiting = false;
        co->done = false;
    } else {
        // A callee returned to the site, or the site was reached some other
        // way (a callee that runs through it): only the first is ours.
        if (!co->waiting || cpu->pc != co->site || cpu->gpr[1] != co->sp)
            return 0u;
        co->result = cpu->gpr[3];
        co->waiting = false;
    }
    co->cpu = cpu;
#ifdef _WIN32
    SwitchToFiber(co->fiber);
#else
    swapcontext(&co->caller, &co->fiber);
#endif
    if (co->done) {
        restore_regs(co, cpu);
        co->active = false;
        return 0u;
    }
    for (int i = 0; i < 6; ++i)
        cpu->gpr[3 + i] = co->args[i];
    cpu->lr = co->site;
    co->waiting = true;
    return co->pending_function;
}

u32 bluewake_guest_call(GuestCoro* co, u32 function, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    co->pending_function = function;
    co->args[0] = a0;
    co->args[1] = a1;
    co->args[2] = a2;
    co->args[3] = a3;
    co->args[4] = a4;
    co->args[5] = a5;
#ifdef _WIN32
    SwitchToFiber(co->caller);
#else
    swapcontext(&co->fiber, &co->caller);
#endif
    return co->result;
}

#else

struct GuestCoro { int unused; };
GuestCoro* bluewake_coro_create(GuestCoroBody body) { (void)body; return NULL; }
bool bluewake_coro_active(const GuestCoro* co) { (void)co; return false; }
CPUState* bluewake_coro_cpu(GuestCoro* co) { (void)co; return NULL; }
u32 bluewake_coro_step(GuestCoro* co, CPUState* cpu, u32 site, void* arg) {
    (void)co; (void)cpu; (void)site; (void)arg;
    return 0u;
}
u32 bluewake_guest_call(GuestCoro* co, u32 function, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) {
    (void)co; (void)function; (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return 0u;
}
#endif
