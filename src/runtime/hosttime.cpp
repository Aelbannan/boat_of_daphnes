// Waiting for a moment in host time, precisely. The game counts video retraces to pace itself
// (f_80117DF8 spins until the count passes last frame's); retraces that come late and then two
// together read as a frame the game missed. A plain sleep wakes at the system timer's next tick
// (up to 15.6 ms late on Windows); a high-resolution waitable timer wakes within about half a
// millisecond, and a short spin after it lands on the moment. (Linux: an absolute clock_nanosleep
// with the thread's timer slack at 1 us does the same.)
#include "runtime/runtime.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <immintrin.h>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")
#else
#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

#include <chrono>

namespace rt {

namespace {
inline void cpuRelax() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}
}  // namespace

#if defined(_WIN32)
u64 hostCycles() { return __rdtsc(); }

// At startup: a 1 ms system timer for everything else that sleeps, and no power throttling -
// Windows slows processes it considers in the background (no visible window, or occluded) and
// ignores their timer requests, and the game must keep its 60 Hz either way.
void hostTimingInit() {
    timeBeginPeriod(1);
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    s.StateMask = 0;  // controlled, and off
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof s);
}

// For the thread that keeps the retrace beat (it runs for microseconds a frame).
void hostThreadTimeCritical() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL); }

void sleepUntil(std::chrono::steady_clock::time_point t, bool spin) {
    using namespace std::chrono;
    thread_local HANDLE s_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const auto margin = spin ? microseconds(400) : microseconds(0);
    for (;;) {
        const auto left = t - steady_clock::now();
        if (left <= nanoseconds(0)) return;
        if (left > margin) {
            if (!s_timer) {
                Sleep(DWORD(duration_cast<milliseconds>(left - margin).count()));
                if (!spin) return;
                continue;
            }
            LARGE_INTEGER due;
            due.QuadPart = -std::max<long long>(1, duration_cast<nanoseconds>(left - margin).count() / 100);  // relative, 100 ns
            if (SetWaitableTimerEx(s_timer, &due, 0, nullptr, nullptr, nullptr, 0)) WaitForSingleObject(s_timer, INFINITE);
            if (!spin) return;
            continue;
        }
        cpuRelax();
    }
}
#else
u64 hostCycles() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return u64(ts.tv_sec) * 1000000000ull + u64(ts.tv_nsec);
}

// At startup: sleeps as precise as the kernel allows (the default timer slack lets a sleep end up to
// 50 us late; threads inherit their creator's).
void hostTimingInit() { prctl(PR_SET_TIMERSLACK, 1UL, 0, 0, 0); }

// For the thread that keeps the retrace beat: real-time when allowed (rtkit or a raised rtprio
// limit), else as high a nice level as allowed; else as it is.
void hostThreadTimeCritical() {
    prctl(PR_SET_TIMERSLACK, 1UL, 0, 0, 0);
    sched_param p{};
    p.sched_priority = 1;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &p) == 0) return;
    const pid_t tid = pid_t(syscall(SYS_gettid));
    for (int nice = -15; nice < 0; nice += 5)
        if (setpriority(PRIO_PROCESS, id_t(tid), nice) == 0) return;
}

void sleepUntil(std::chrono::steady_clock::time_point t, bool spin) {
    using namespace std::chrono;
    // (steady_clock is CLOCK_MONOTONIC)
    const auto margin = spin ? microseconds(150) : microseconds(0);
    for (;;) {
        const auto now = steady_clock::now();
        const auto left = t - now;
        if (left <= nanoseconds(0)) return;
        if (left > margin) {
            const auto wake = duration_cast<nanoseconds>((t - margin).time_since_epoch()).count();
            timespec ts{time_t(wake / 1000000000), long(wake % 1000000000)};
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
            }
            if (!spin) return;
            continue;
        }
        cpuRelax();
    }
}
#endif

}  // namespace rt
