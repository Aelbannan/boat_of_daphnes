// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <exception>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <tlhelp32.h>
#include "win_crash.h"
#include "crash_text.h"

static HANDLE crash_file = INVALID_HANDLE_VALUE;
static volatile LONG reporting;
static void write_bytes(const char* bytes, unsigned size) {
    if (crash_file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(crash_file, bytes, size, &written, nullptr);
    }
}
static void literal(const char* value) {
    unsigned size = 0; while (value[size]) ++size;
    write_bytes(value, size);
}
static void hex(ULONG64 value) {
    char bytes[18]; unsigned size = bw_crash_hex(bytes, value);
    write_bytes(bytes, size);
}
static void report_context(const char* reason, DWORD code, CONTEXT context) {
    if (InterlockedExchange(&reporting, 1)) return;
    literal("[crash] "); literal(reason); literal(" code="); hex(code);
    literal(" rip="); hex(context.Rip); literal(" rsp="); hex(context.Rsp); literal("\r\n");
    ULONG_PTR lo, hi; GetCurrentThreadStackLimits(&lo, &hi);
    __try {
        // A call through null leaves its return address on the stack.
        if (context.Rip == 0 && context.Rsp >= lo && context.Rsp <= hi - 8) {
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += 8;
            literal("[crash] null-call caller="); hex(context.Rip); literal("\r\n");
        }
        for (unsigned frame = 0; frame < 16 && context.Rip; ++frame) {
            literal("[crash] frame="); hex(frame); literal(" pc="); hex(context.Rip);
            DWORD64 image = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
            literal(" image="); hex(image); literal(" offset="); hex(context.Rip - image); literal("\r\n");
            DWORD64 previous = context.Rsp;
            if (context.Rsp < lo || context.Rsp > hi - 8) break;
            if (!function) {
                context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp); context.Rsp += 8;
            } else {
                PVOID handler_data; DWORD64 establisher;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context,
                    &handler_data, &establisher, nullptr);
            }
            if (context.Rsp <= previous || context.Rsp > hi) break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        literal("[crash] stack unavailable\r\n");
    }
    if (crash_file != INVALID_HANDLE_VALUE) FlushFileBuffers(crash_file);
}
static LONG WINAPI unhandled(EXCEPTION_POINTERS* info) {
    report_context("exception", info->ExceptionRecord->ExceptionCode, *info->ContextRecord);
    TerminateProcess(GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void fatal(const char* reason, DWORD code) {
    CONTEXT context{}; RtlCaptureContext(&context);
    report_context(reason, code, context);
    TerminateProcess(GetCurrentProcess(), code);
}
// --- the hang watchdog ---
// A freeze leaves no crash: the picture stops while the window stays (two after Ballad of Gales warps, 2026-10-17,
// whose logs could only say the present side had stopped). When the UI thread's heartbeat stops for 10 s, every
// thread's stack is written into the crash report (frames as image + offset, symbolized against the release's PDBs)
// and a minidump beside it, once until the heartbeat comes back.
static volatile LONG64 heartbeat;
static char hang_dump_path[MAX_PATH * 4];

extern "C" void bw_crash_heartbeat(void) { InterlockedIncrement64(&heartbeat); }

static void hang_walk(HANDLE thread, DWORD id) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (SuspendThread(thread) == (DWORD)-1)
        return;
    if (GetThreadContext(thread, &context)) {
        literal("[hang] thread="); hex(id); literal(" rip="); hex(context.Rip); literal(" rsp="); hex(context.Rsp); literal("\r\n");
        __try {
            for (unsigned frame = 0; frame < 24 && context.Rip; ++frame) {
                DWORD64 image = 0;
                PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
                literal("[hang]   frame="); hex(frame); literal(" pc="); hex(context.Rip);
                literal(" image="); hex(image); literal(" offset="); hex(context.Rip - image); literal("\r\n");
                const DWORD64 previous = context.Rsp;
                if (!function) {
                    context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
                    context.Rsp += 8;
                } else {
                    PVOID handler_data; DWORD64 establisher;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler_data,
                                     &establisher, nullptr);
                }
                if (context.Rsp <= previous)
                    break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            literal("[hang]   (stack unreadable)\r\n");
        }
    }
    ResumeThread(thread);
}

static DWORD WINAPI hang_watchdog(LPVOID) {
    LONG64 seen = heartbeat;
    unsigned still = 0;
    bool reported = false;
    for (;;) {
        Sleep(1000);
        const LONG64 now = heartbeat;
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
        literal("[hang] the UI thread has not finished a frame for 10 s: every thread's stack\r\n");
        const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 entry{};
            entry.dwSize = sizeof entry;
            for (BOOL ok = Thread32First(snap, &entry); ok; ok = Thread32Next(snap, &entry)) {
                if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self)
                    continue;
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                           FALSE, entry.th32ThreadID);
                if (thread != nullptr) {
                    hang_walk(thread, entry.th32ThreadID);
                    CloseHandle(thread);
                }
            }
            CloseHandle(snap);
        }
        // The minidump: threads, stacks and the modules (dbghelp, loaded only now).
        typedef BOOL(WINAPI * WriteDump)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
        if (HMODULE dbghelp = LoadLibraryA("dbghelp.dll")) {
            if (auto write = reinterpret_cast<WriteDump>(GetProcAddress(dbghelp, "MiniDumpWriteDump"))) {
                HANDLE file = CreateFileA(hang_dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file != INVALID_HANDLE_VALUE) {
                    // MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules
                    const BOOL ok = write(GetCurrentProcess(), pid, file, 0x1000 | 0x20, nullptr, nullptr, nullptr);
                    CloseHandle(file);
                    literal(ok ? "[hang] minidump written\r\n" : "[hang] minidump failed\r\n");
                }
            }
        }
        if (crash_file != INVALID_HANDLE_VALUE)
            FlushFileBuffers(crash_file);
        std::fprintf(stderr, "[hang] the UI thread stopped for 10 s: stacks in the crash report, minidump %s\n", hang_dump_path);
    }
    return 0;
}

extern "C" void bw_crash_install(const char* data_dir) {
    char path[MAX_PATH * 4];
    std::snprintf(path, sizeof path, "%slogs\\crash-%lu.log", data_dir, GetCurrentProcessId());
    crash_file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    ULONG guarantee = 32768;
    if (!SetThreadStackGuarantee(&guarantee)) std::fprintf(stderr, "[crash] stack guarantee unavailable: %lu\n", GetLastError());
    if (crash_file == INVALID_HANDLE_VALUE) std::fprintf(stderr, "[crash] report file unavailable: %lu\n", GetLastError());
    else std::fprintf(stderr, "[crash] reports=%s\n", path);
    SetUnhandledExceptionFilter(unhandled);
    std::snprintf(hang_dump_path, sizeof hang_dump_path, "%slogs\\hang-%lu.dmp", data_dir, GetCurrentProcessId());
    if (HANDLE watchdog = CreateThread(nullptr, 64 * 1024, hang_watchdog, nullptr, 0, nullptr))
        CloseHandle(watchdog);
    std::set_terminate([] { fatal("std::terminate", 0xE0420001); });
    std::signal(SIGABRT, [](int) { fatal("SIGABRT", 0xE0420002); });
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
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
