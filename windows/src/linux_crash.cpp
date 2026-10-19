// SPDX-License-Identifier: GPL-3.0-or-later
// Boat of Daphnes for Linux: the crash reporter (the counterpart of win_crash.cpp). A fatal signal or std::terminate writes
// a [crash] report with the faulting address and a stack walk into <data_dir>/logs/crash-<pid>.log, then re-raises the
// signal so the default action (the core dump, the shell's report) still happens. The hang watchdog writes every
// thread's stack into the same file when the UI thread's heartbeat stops for 10 s.
//
// Frames are written by backtrace_symbols_fd: "module(+offset) [address]", which addr2line -e module +offset names
// against the release's debug files. There is no minidump on Linux; the stacks are the report.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>

#include "win_crash.h"
#include "crash_text.h"

static int g_crash_fd = -1;
static std::atomic<int> g_reporting{0};
static char g_stack[64 * 1024];  // the signal handler's own stack: a stack overflow can still be reported

static void write_bytes(const char* bytes, size_t size) {
    if (g_crash_fd < 0)
        return;
    while (size > 0) {
        const ssize_t n = write(g_crash_fd, bytes, size);
        if (n <= 0)
            return;
        bytes += n;
        size -= static_cast<size_t>(n);
    }
}

static void literal(const char* value) { write_bytes(value, strlen(value)); }

static void hex(uint64_t value) {
    char bytes[18];
    write_bytes(bytes, bw_crash_hex(bytes, value));
}

static void frames_to_file(const char* prefix) {
    void* frames[48];
    const int count = backtrace(frames, 48);
    for (int i = 0; i < count; ++i) {
        literal(prefix);
        hex(static_cast<uint64_t>(i));
        literal(" pc=");
        hex(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(frames[i])));
        literal(" ");
        if (g_crash_fd >= 0)
            backtrace_symbols_fd(&frames[i], 1, g_crash_fd);  // ends its line
    }
}

static void* fault_pc(ucontext_t* context) {
#if defined(__x86_64__)
    return reinterpret_cast<void*>(context->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
    return reinterpret_cast<void*>(context->uc_mcontext.pc);
#else
    (void)context;
    return nullptr;
#endif
}

static uint64_t fault_sp(ucontext_t* context) {
#if defined(__x86_64__)
    return static_cast<uint64_t>(context->uc_mcontext.gregs[REG_RSP]);
#elif defined(__aarch64__)
    return static_cast<uint64_t>(context->uc_mcontext.sp);
#else
    (void)context;
    return 0;
#endif
}

static void report(const char* reason, int sig, siginfo_t* info, ucontext_t* context) {
    if (g_reporting.exchange(1))
        return;
    literal("[crash] ");
    literal(reason);
    literal(" signal=");
    hex(static_cast<uint64_t>(sig));
    if (context != nullptr) {
        literal(" pc=");
        hex(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(fault_pc(context))));
        literal(" sp=");
        hex(fault_sp(context));
    }
    if (info != nullptr) {
        literal(" addr=");
        hex(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(info->si_addr)));
    }
    literal("\n");
    frames_to_file("[crash] frame=");
    if (g_crash_fd >= 0)
        fsync(g_crash_fd);
}

static void handle_fatal(int sig, siginfo_t* info, void* context) {
    report("signal", sig, info, static_cast<ucontext_t*>(context));
    struct sigaction dfl;
    memset(&dfl, 0, sizeof dfl);
    dfl.sa_handler = SIG_DFL;
    sigaction(sig, &dfl, nullptr);
    raise(sig);
}

static void terminate_handler() {
    report("std::terminate", 0, nullptr, nullptr);
    std::abort();
}

// --- the hang watchdog ---
// A freeze leaves no crash: the picture stops while the window stays. When the UI thread's heartbeat stops for 10 s, every
// thread is sent a signal whose handler writes its own stack into the crash report, once until the heartbeat comes back.
static std::atomic<long long> g_heartbeat{0};
static std::atomic<pid_t> g_probe_target{0};
static std::atomic<int> g_probe_done{0};
static int g_probe_signal;

extern "C" void bw_crash_heartbeat(void) { g_heartbeat.fetch_add(1, std::memory_order_relaxed); }

static void hang_probe(int, siginfo_t*, void* context) {
    const pid_t self = static_cast<pid_t>(syscall(SYS_gettid));
    if (self != g_probe_target.load())
        return;
    literal("[hang] thread=");
    hex(static_cast<uint64_t>(self));
    if (context != nullptr) {
        literal(" pc=");
        hex(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(fault_pc(static_cast<ucontext_t*>(context)))));
    }
    literal("\n");
    frames_to_file("[hang]   frame=");
    g_probe_done.store(1);
}

static void sleep_ms(long ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

static void report_hang() {
    literal("[hang] the UI thread has not finished a frame for 10 s: every thread's stack\n");
    const pid_t self = static_cast<pid_t>(syscall(SYS_gettid));
    const pid_t pid = getpid();
    DIR* tasks = opendir("/proc/self/task");
    if (tasks == nullptr)
        return;
    while (dirent* entry = readdir(tasks)) {
        const pid_t tid = static_cast<pid_t>(atoi(entry->d_name));
        if (tid <= 0 || tid == self)
            continue;
        g_probe_done.store(0);
        g_probe_target.store(tid);
        if (syscall(SYS_tgkill, pid, tid, g_probe_signal) != 0)
            continue;
        for (int waited = 0; waited < 500 && !g_probe_done.load(); waited += 5)
            sleep_ms(5);
        if (!g_probe_done.load()) {
            literal("[hang] thread=");
            hex(static_cast<uint64_t>(tid));
            literal(" (no answer)\n");
        }
    }
    closedir(tasks);
    g_probe_target.store(0);
    if (g_crash_fd >= 0)
        fsync(g_crash_fd);
}

static void* hang_watchdog(void*) {
    long long seen = g_heartbeat.load();
    unsigned still = 0;
    bool reported = false;
    for (;;) {
        sleep_ms(1000);
        const long long now = g_heartbeat.load();
        if (now != seen) {
            seen = now;
            still = 0;
            reported = false;
            continue;
        }
        // Before the first frame, and while the window is minimized (no frames then), nothing to say.
        if (now == 0 || reported || ++still < 10)
            continue;
        reported = true;
        report_hang();
        std::fprintf(stderr, "[hang] the UI thread stopped for 10 s: stacks in the crash report\n");
    }
    return nullptr;
}

extern "C" void bw_crash_install(const char* data_dir) {
    char path[4096];
    std::snprintf(path, sizeof path, "%slogs/crash-%ld.log", data_dir, static_cast<long>(getpid()));
    g_crash_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (g_crash_fd < 0)
        std::fprintf(stderr, "[crash] report file unavailable: %s\n", std::strerror(errno));
    else
        std::fprintf(stderr, "[crash] reports=%s\n", path);
    // backtrace loads libgcc on its first call, which allocates: do it now, not in a handler.
    void* warm[2];
    backtrace(warm, 2);

    stack_t alt;
    alt.ss_sp = g_stack;
    alt.ss_size = sizeof g_stack;
    alt.ss_flags = 0;
    sigaltstack(&alt, nullptr);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sa.sa_sigaction = handle_fatal;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE})
        sigaction(sig, &sa, nullptr);
    std::set_terminate(terminate_handler);

    g_probe_signal = SIGRTMIN + 3;
    struct sigaction probe;
    memset(&probe, 0, sizeof probe);
    probe.sa_flags = SA_SIGINFO | SA_RESTART;
    probe.sa_sigaction = hang_probe;
    sigemptyset(&probe.sa_mask);
    sigaction(g_probe_signal, &probe, nullptr);
    pthread_t watchdog;
    if (pthread_create(&watchdog, nullptr, hang_watchdog, nullptr) == 0)
        pthread_detach(watchdog);
}

extern "C" void bw_crash_test(void) {
#if BLUEWAKE_ENABLE_DEVELOPER_TRACING
    const char* test = std::getenv("BLUEWAKE_CRASH_TEST");
    if (!test) return;
    if (std::strcmp(test, "null") == 0) {
        // Volatile keeps this an actual indirect call, not UB folded by the compiler.
        void (*volatile call)() = nullptr; call();
    } else if (std::strcmp(test, "abort") == 0) std::abort();
    else if (std::strcmp(test, "terminate") == 0) std::terminate();
#endif
}
