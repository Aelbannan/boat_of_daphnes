// Crash reporting: an unhandled exception (access violation, ...) prints the exception, the host
// address (with the module it belongs to) and what every guest thread was doing. (Linux: the
// signals - SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT - the same way.)
#include "runtime/runtime.h"
#include "runtime/sched.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#endif

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rt {

void dumpAllThreads();  // src/runtime/cpu.cpp

#if defined(_WIN32)
namespace {
LONG WINAPI onCrash(EXCEPTION_POINTERS* ep) {
    const auto* r = ep->ExceptionRecord;
    const auto pc = reinterpret_cast<uintptr_t>(r->ExceptionAddress);
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(pc), &mod);
    char name[MAX_PATH] = "?";
    if (mod) GetModuleFileNameA(mod, name, MAX_PATH);
    // (through log(): into sa2b.log too - a game started by the launcher has no console)
    char what[160] = "";
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        const auto addr = r->ExceptionInformation[1];
        const auto mem = reinterpret_cast<uintptr_t>(g_mem);
        std::snprintf(what, sizeof what, ", %s %llX%s", r->ExceptionInformation[0] ? "writing" : "reading", (unsigned long long)addr,
                      addr >= mem && addr < mem + kHostMemSize ? " (guest memory)" : "");
    }
    log("\nCRASH: exception %08lX at %p (%s+%llX)%s\n", r->ExceptionCode, r->ExceptionAddress, name,
        (unsigned long long)(pc - reinterpret_cast<uintptr_t>(mod)), what);
    // the host call stack (through the exception dispatch into the faulting code), as module+offset
    void* frames[48];
    const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT i = 0; i < n; i++) {
        HMODULE m = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(frames[i]), &m);
        char mn[MAX_PATH] = "?";
        if (m) GetModuleFileNameA(m, mn, MAX_PATH);
        const char* base = std::strrchr(mn, '\\');
        log("  %s+%llX\n", base ? base + 1 : mn, (unsigned long long)(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(m)));
    }
    dumpAllThreads();
    sched::debugDump();
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace

// abort() (also reached from std::terminate, e.g. an uncaught C++ exception) fast-fails past the
// exception filter; report it the same way, with the host call stack as module+offset.
static void onAbort(int) {
    log("\nABORT on host thread %lu; host stack:\n", GetCurrentThreadId());
    void* frames[48];
    const USHORT n = CaptureStackBackTrace(1, 48, frames, nullptr);
    for (USHORT i = 0; i < n; i++) {
        HMODULE mod = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(frames[i]), &mod);
        char name[MAX_PATH] = "?";
        if (mod) GetModuleFileNameA(mod, name, MAX_PATH);
        const char* base = std::strrchr(name, '\\');
        log("  %s+%llX\n", base ? base + 1 : name, (unsigned long long)(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(mod)));
    }
    std::fflush(stdout);
    dumpAllThreads();
    sched::debugDump();
    std::fflush(stdout);
    std::_Exit(3);
}

void installCrashHandler() {
    SetUnhandledExceptionFilter(onCrash);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, onAbort);
}

// Debug watchpoint (SA2_WATCH=<guest address>): the host page holding it is made read-only and
// the first write to it is reported with what the guest threads were doing, then allowed.
namespace {
uintptr_t s_watchPage = 0;
u32 s_watchGuest = 0, s_watchLen = 0;
int s_watchHits = 0, s_watchMax = 3;
thread_local bool t_rearm = false;  // this thread is single-stepping past a write to the page
LONG WINAPI onWatch(EXCEPTION_POINTERS* ep) {
    const auto* r = ep->ExceptionRecord;
    DWORD old;
    if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && t_rearm) {
        t_rearm = false;
        VirtualProtect(reinterpret_cast<void*>(s_watchPage), 0x1000, PAGE_READONLY, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 || !s_watchPage) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t addr = r->ExceptionInformation[1];
    if ((addr & ~uintptr_t(0xFFF)) != s_watchPage) return EXCEPTION_CONTINUE_SEARCH;
    const u32 guest = 0x80000000u | u32(addr - reinterpret_cast<uintptr_t>(g_mem));
    if (guest >= s_watchGuest && guest < s_watchGuest + s_watchLen && s_watchHits++ < s_watchMax) {
        std::fprintf(stdout, "\n[watch] write to %08X (watching %08X+%u) from host %p\n", guest, s_watchGuest, s_watchLen,
                     r->ExceptionAddress);
        if (const Ctx* c = sched::currentCtx()) {
            for (int i = 0; i < 32; i++) std::fprintf(stdout, "r%-2d %08X%s", i, c->r[i], i % 8 == 7 ? "\n" : "  ");
            std::fprintf(stdout, "lr %08X  ctr %08X\nguest stack:", c->lr, c->ctr);
            // Back chain: each frame's first word links to the caller's, whose LR save word is at +4.
            u32 sp = c->r[1];
            for (int i = 0; i < 16 && (sp >> 24) == 0x80 + ((sp >> 24) & 1); i++) {
                sp = rd32(sp);
                if (!sp) break;
                const u32 ret = rd32(sp + 4);
                const char* nm = guestName(ret);
                std::fprintf(stdout, " %08X%s%s", ret, nm ? ":" : "", nm ? nm : "");
            }
            std::fprintf(stdout, "\n");
        }
        dumpAllThreads();
        std::fflush(stdout);
    }
    // Let this one write through: unprotect, single-step it, protect again.
    VirtualProtect(reinterpret_cast<void*>(s_watchPage), 0x1000, PAGE_READWRITE, &old);
    ep->ContextRecord->EFlags |= 0x100;
    t_rearm = true;
    return EXCEPTION_CONTINUE_EXECUTION;
}
}  // namespace

void installWatch(u32 guest, u32 len) {
    s_watchGuest = guest;
    s_watchLen = len;
    if (const char* m = std::getenv("SA2_WATCH_HITS")) s_watchMax = std::atoi(m);
    s_watchPage = reinterpret_cast<uintptr_t>(hostp(guest)) & ~uintptr_t(0xFFF);
    AddVectoredExceptionHandler(1, onWatch);
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(s_watchPage), 0x1000, PAGE_READONLY, &old);
    log("[watch] guarding %08X\n", guest);
}

#else  // Linux

namespace {
// Where a host address is: module file name (without its folder) and offset, and the symbol when the
// module exports one (the program's are: it is linked with -rdynamic).
void where(const void* pc, char* out, size_t size) {
    Dl_info info{};
    if (!dladdr(pc, &info) || !info.dli_fname) {
        std::snprintf(out, size, "?+%llX", (unsigned long long)reinterpret_cast<uintptr_t>(pc));
        return;
    }
    const char* base = std::strrchr(info.dli_fname, '/');
    base = base ? base + 1 : info.dli_fname;
    const auto off = (unsigned long long)(reinterpret_cast<uintptr_t>(pc) - reinterpret_cast<uintptr_t>(info.dli_fbase));
    if (info.dli_sname)
        std::snprintf(out, size, "%s+%llX (%s+%llX)", base, off, info.dli_sname,
                      (unsigned long long)(reinterpret_cast<uintptr_t>(pc) - reinterpret_cast<uintptr_t>(info.dli_saddr)));
    else
        std::snprintf(out, size, "%s+%llX", base, off);
}

void logStack(int skip) {
    void* frames[48];
    const int n = backtrace(frames, 48);
    for (int i = skip; i < n; i++) {
        char w[512];
        where(frames[i], w, sizeof w);
        log("  %s\n", w);
    }
}

uintptr_t pcOf(void* uc) {
    const auto* u = static_cast<const ucontext_t*>(uc);
#if defined(__x86_64__)
    return uintptr_t(u->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
    return uintptr_t(u->uc_mcontext.pc);
#else
    (void)u;
    return 0;
#endif
}

// Debug watchpoint state (installWatch below): the first writes to a guarded guest word are reported.
uintptr_t s_watchPage = 0;
u32 s_watchGuest = 0, s_watchLen = 0;
int s_watchHits = 0, s_watchMax = 3;
thread_local bool t_rearm = false;  // this thread is single-stepping past a write to the page

bool onWatch(siginfo_t* si, void* uc) {
    if (!s_watchPage) return false;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(si->si_addr);
    if ((addr & ~uintptr_t(0xFFF)) != s_watchPage) return false;
    const u32 guest = 0x80000000u | u32(addr - reinterpret_cast<uintptr_t>(g_mem));
    if (guest >= s_watchGuest && guest < s_watchGuest + s_watchLen && s_watchHits++ < s_watchMax) {
        std::fprintf(stdout, "\n[watch] write to %08X (watching %08X+%u) from host %p\n", guest, s_watchGuest, s_watchLen,
                     reinterpret_cast<void*>(pcOf(uc)));
        if (const Ctx* c = sched::currentCtx()) {
            for (int i = 0; i < 32; i++) std::fprintf(stdout, "r%-2d %08X%s", i, c->r[i], i % 8 == 7 ? "\n" : "  ");
            std::fprintf(stdout, "lr %08X  ctr %08X\nguest stack:", c->lr, c->ctr);
            // Back chain: each frame's first word links to the caller's, whose LR save word is at +4.
            u32 sp = c->r[1];
            for (int i = 0; i < 16 && (sp >> 24) == 0x80 + ((sp >> 24) & 1); i++) {
                sp = rd32(sp);
                if (!sp) break;
                const u32 ret = rd32(sp + 4);
                const char* nm = guestName(ret);
                std::fprintf(stdout, " %08X%s%s", ret, nm ? ":" : "", nm ? nm : "");
            }
            std::fprintf(stdout, "\n");
        }
        dumpAllThreads();
        std::fflush(stdout);
    }
    // Let this one write through: unprotect, single-step it (the trap flag), protect again (SIGTRAP).
    mprotect(reinterpret_cast<void*>(s_watchPage), 0x1000, PROT_READ | PROT_WRITE);
#if defined(__x86_64__)
    static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_EFL] |= 0x100;
    t_rearm = true;
#endif
    return true;
}

void onTrap(int sig, siginfo_t*, void* uc) {
    if (!t_rearm) {
        std::signal(sig, SIG_DFL);
        std::raise(sig);
        return;
    }
    t_rearm = false;
#if defined(__x86_64__)
    static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_EFL] &= ~0x100;
#endif
    mprotect(reinterpret_cast<void*>(s_watchPage), 0x1000, PROT_READ);
}

void onCrash(int sig, siginfo_t* si, void* uc) {
    if (sig == SIGSEGV && onWatch(si, uc)) return;
    const auto pc = pcOf(uc);
    char at[512];
    where(reinterpret_cast<void*>(pc), at, sizeof at);
    // (through log(): into sa2b.log too - a game started by the launcher has no console)
    char what[160] = "";
    if (sig == SIGSEGV || sig == SIGBUS) {
        const auto addr = reinterpret_cast<uintptr_t>(si->si_addr);
        const auto mem = reinterpret_cast<uintptr_t>(g_mem);
        const char* rw = "accessing";
#if defined(__x86_64__)
        rw = (static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_ERR] & 2) ? "writing" : "reading";
#endif
        std::snprintf(what, sizeof what, ", %s %llX%s", rw, (unsigned long long)addr,
                      addr >= mem && addr < mem + kHostMemSize ? " (guest memory)" : "");
    }
    log("\nCRASH: signal %d (%s) at %p (%s)%s\n", sig, strsignal(sig), reinterpret_cast<void*>(pc), at, what);
    // the host call stack (through the signal frame into the faulting code), as module+offset
    logStack(0);
    dumpAllThreads();
    sched::debugDump();
    std::fflush(stdout);
    // and on to the default action (the exit status, a core dump where they are kept)
    signal(sig, SIG_DFL);
    raise(sig);
}

// abort() (also reached from std::terminate, e.g. an uncaught C++ exception): reported the same way,
// with the host call stack as module+offset.
void onAbort(int) {
    log("\nABORT on host thread %ld; host stack:\n", long(syscall(SYS_gettid)));
    logStack(1);
    std::fflush(stdout);
    dumpAllThreads();
    sched::debugDump();
    std::fflush(stdout);
    std::_Exit(3);
}
}  // namespace

// A stack for the crash handler on the calling thread, so a stack overflow is reported too.
void hostThreadCrashStack() {
    static thread_local bool t_done = false;
    if (t_done) return;
    t_done = true;
    stack_t ss{};
    ss.ss_size = 256 * 1024;
    ss.ss_sp = std::malloc(ss.ss_size);
    if (ss.ss_sp) sigaltstack(&ss, nullptr);
}

void installCrashHandler() {
    void* prime[2];
    backtrace(prime, 2);  // (loads the unwinder now: not first in a signal handler)
    hostThreadCrashStack();
    struct sigaction sa{};
    sa.sa_sigaction = onCrash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE}) sigaction(sig, &sa, nullptr);
    std::signal(SIGABRT, onAbort);
}

// Debug watchpoint (SA2_WATCH=<guest address>): the host page holding it is made read-only and
// the first write to it is reported with what the guest threads were doing, then allowed.
void installWatch(u32 guest, u32 len) {
    s_watchGuest = guest;
    s_watchLen = len;
    if (const char* m = std::getenv("SA2_WATCH_HITS")) s_watchMax = std::atoi(m);
    s_watchPage = reinterpret_cast<uintptr_t>(hostp(guest)) & ~uintptr_t(0xFFF);
    struct sigaction sa{};
    sa.sa_sigaction = onCrash;  // (which hands the guarded page's faults to the watch first)
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sa.sa_sigaction = onTrap;
    sigaction(SIGTRAP, &sa, nullptr);
    mprotect(reinterpret_cast<void*>(s_watchPage), 0x1000, PROT_READ);
    log("[watch] guarding %08X\n", guest);
}

#endif

}  // namespace rt
