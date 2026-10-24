// Sampling profiler (SA2_PROFILE=<seconds>): every millisecond the host threads that run guest
// code are stopped for a moment and their instruction pointer recorded; every <seconds> the
// hottest functions are printed (names from the PDB: generated code appears as f_XXXXXXXX). Samples
// of threads that are waiting (instruction pointer outside sa2b.exe) are not counted.
//
// Linux: the threads are sampled with a signal of their own (each records its instruction pointer,
// and its stack when asked), names come from the program's and the game module's ELF symbol tables
// (generated code: f_XXXXXXXX), and samples in either count as running.
#include "runtime/runtime.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")

namespace rt {

namespace {
std::mutex s_lock;
std::vector<HANDLE> s_threads;
bool s_enabled = std::getenv("SA2_PROFILE") != nullptr;

// SA2_PROFILE_WAITS: also record where waiting threads wait (their innermost frames in sa2b.exe).
const bool s_waits = std::getenv("SA2_PROFILE_WAITS") != nullptr;

void symInit() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
}

std::string symName(u64 addr) {
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    return SymFromAddr(GetCurrentProcess(), addr, &disp, sym) ? std::string(sym->Name) : "?";
}

void reportWaits(std::map<std::vector<u64>, u32>& waits, u64 total) {
    std::map<std::string, u32> byChain;
    for (auto& [chain, n] : waits) {
        std::string key;
        for (u64 a : chain)
            if (a) key += (key.empty() ? "" : " < ") + symName(a);
        byChain[key] += n;
    }
    std::vector<std::pair<u32, std::string>> top;
    for (auto& [k, n] : byChain) top.push_back({n, k});
    std::sort(top.rbegin(), top.rend());
    log("[profile] %llu waiting samples; top wait sites:\n", (unsigned long long)total);
    for (size_t i = 0; i < top.size() && i < 15; i++)
        log("[profile] %5.1f%%  %s\n", 100.0 * top[i].first / double(total), top[i].second.c_str());
    waits.clear();
}

void report(std::unordered_map<u64, u32>& hits, u64 total) {
    symInit();
    std::map<std::string, u32> byName;
    for (auto& [rip, n] : hits) byName[symName(rip)] += n;
    std::vector<std::pair<u32, std::string>> top;
    for (auto& [name, n] : byName) top.push_back({n, name});
    std::sort(top.rbegin(), top.rend());
    log("[profile] %llu samples in guest-running threads; top functions:\n", (unsigned long long)total);
    for (size_t i = 0; i < top.size() && i < 40; i++)
        log("[profile] %5.1f%%  %s\n", 100.0 * top[i].first / double(total), top[i].second.c_str());
    // The hottest single addresses, with source lines where the PDB has them (runtime code).
    std::vector<std::pair<u32, u64>> rips;
    for (auto& [rip, n] : hits) rips.push_back({n, rip});
    std::sort(rips.rbegin(), rips.rend());
    for (size_t i = 0; i < rips.size() && i < 12; i++) {
        IMAGEHLP_LINE64 line{sizeof(IMAGEHLP_LINE64)};
        DWORD disp = 0;
        const bool have = SymGetLineFromAddr64(GetCurrentProcess(), rips[i].second, &disp, &line);
        log("[profile]   %5.1f%% at %s %s:%lu\n", 100.0 * rips[i].first / double(total), symName(rips[i].second).c_str(),
            have ? line.FileName : "", have ? line.LineNumber : 0ul);
    }
    hits.clear();
}

void profilerMain(int seconds) {
    timeBeginPeriod(1);
    HMODULE exe = GetModuleHandleA(nullptr);
    const u64 lo = reinterpret_cast<u64>(exe);
    IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<u8*>(exe) + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const u64 hi = lo + nt->OptionalHeader.SizeOfImage;
    std::unordered_map<u64, u32> hits;
    std::map<std::vector<u64>, u32> waits;
    u64 total = 0, waitTotal = 0;
    if (s_waits) symInit();
    auto last = GetTickCount64();
    for (;;) {
        Sleep(1);
        std::vector<HANDLE> threads;
        {
            std::lock_guard g(s_lock);
            threads = s_threads;
        }
        for (HANDLE h : threads) {
            CONTEXT ctx{};
            ctx.ContextFlags = s_waits ? CONTEXT_FULL : CONTEXT_CONTROL;
            if (SuspendThread(h) == DWORD(-1)) continue;
            const bool ok = GetThreadContext(h, &ctx);
            u64 chain[6] = {};
            if (ok && s_waits && (ctx.Rip < lo || ctx.Rip >= hi)) {
                // Waiting: walk the stack (while the thread is stopped) to the first frames in sa2b.exe.
                STACKFRAME64 f{};
                f.AddrPC = {ctx.Rip, 0, AddrModeFlat};
                f.AddrFrame = {ctx.Rbp, 0, AddrModeFlat};
                f.AddrStack = {ctx.Rsp, 0, AddrModeFlat};
                int n = 0;
                for (int depth = 0; depth < 48 && n < 6; depth++) {
                    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), h, &f, &ctx, nullptr,
                                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !f.AddrPC.Offset)
                        break;
                    if (f.AddrPC.Offset >= lo && f.AddrPC.Offset < hi) chain[n++] = f.AddrPC.Offset;
                }
            }
            ResumeThread(h);
            if (!ok) continue;
            if (ctx.Rip < lo || ctx.Rip >= hi) {  // waiting, or in a system DLL
                if (chain[0]) {
                    waits[std::vector<u64>(chain, chain + 6)]++;
                    waitTotal++;
                }
                continue;
            }
            hits[ctx.Rip]++;
            total++;
        }
        if (GetTickCount64() - last >= u64(seconds) * 1000) {
            report(hits, total);
            if (s_waits) reportWaits(waits, waitTotal);
            total = waitTotal = 0;
            last = GetTickCount64();
        }
    }
}
}  // namespace

// Where every thread that runs guest code is right now: each is sampled `samples` times over
// `ms` milliseconds, and its most frequent functions (from the PDB: guest code shows as f_XXXXXXXX)
// are logged - for hangs, when the game stops producing frames.
void profileSnapshot(int samples, int ms) {
    symInit();
    std::vector<HANDLE> threads;
    {
        std::lock_guard g(s_lock);
        threads = s_threads;
    }
    std::vector<std::unordered_map<u64, u32>> hits(threads.size());
    std::vector<u32> outside(threads.size());
    HMODULE exe = GetModuleHandleA(nullptr);
    const u64 lo = reinterpret_cast<u64>(exe);
    IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<u8*>(exe) + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const u64 hi = lo + nt->OptionalHeader.SizeOfImage;
    for (int s = 0; s < samples; s++) {
        for (size_t t = 0; t < threads.size(); t++) {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_CONTROL;
            DWORD code = 0;
            if (!GetExitCodeThread(threads[t], &code) || code != STILL_ACTIVE) continue;
            if (SuspendThread(threads[t]) == DWORD(-1)) continue;
            const bool ok = GetThreadContext(threads[t], &ctx);
            ResumeThread(threads[t]);
            if (!ok) continue;
            if (ctx.Rip >= lo && ctx.Rip < hi) hits[t][ctx.Rip]++;
            else outside[t]++;  // waiting (in the OS) or in a system library
        }
        Sleep(DWORD(std::max(1, ms / samples)));
    }
    for (size_t t = 0; t < threads.size(); t++) {
        if (hits[t].empty()) continue;
        std::map<std::string, u32> byName;
        for (auto& [rip, n] : hits[t]) byName[symName(rip)] += n;
        std::vector<std::pair<u32, std::string>> top;
        for (auto& [name, n] : byName) top.push_back({n, name});
        std::sort(top.rbegin(), top.rend());
        std::string line;
        for (size_t i = 0; i < top.size() && i < 6; i++) line += " " + top[i].second + " x" + std::to_string(top[i].first);
        log("[stall]   host thread %lu: %u samples waiting,%s\n", GetThreadId(threads[t]), outside[t], line.c_str());
    }
    // Then each thread's stack as it is now, innermost first (generated functions are the game's,
    // f_<address>): what a stuck thread is inside of and who called it.
    for (HANDLE h : threads) {
        DWORD code = 0;
        if (!GetExitCodeThread(h, &code) || code != STILL_ACTIVE) continue;
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_FULL;
        if (SuspendThread(h) == DWORD(-1)) continue;
        u64 frames[16] = {};  // (names looked up after it runs again: nothing that allocates while it is stopped)
        int n = 0;
        if (GetThreadContext(h, &ctx)) {
            STACKFRAME64 f{};
            f.AddrPC = {ctx.Rip, 0, AddrModeFlat};
            f.AddrFrame = {ctx.Rbp, 0, AddrModeFlat};
            f.AddrStack = {ctx.Rsp, 0, AddrModeFlat};
            for (int depth = 0; depth < 64 && n < 16; depth++) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), h, &f, &ctx, nullptr, SymFunctionTableAccess64,
                                 SymGetModuleBase64, nullptr) ||
                    !f.AddrPC.Offset)
                    break;
                if (f.AddrPC.Offset >= lo && f.AddrPC.Offset < hi) frames[n++] = f.AddrPC.Offset;
            }
        }
        ResumeThread(h);
        std::string chain;
        for (int i = 0; i < n; i++) chain += (i ? " < " : " ") + symName(frames[i]);
        log("[stall]   host thread %lu stack:%s\n", GetThreadId(h), chain.empty() ? " (none in sa2b.exe)" : chain.c_str());
    }
}

// CPU time (time stamp counter cycles) the threads that run guest code have used so far.
u64 profileGuestCycles() {
    std::lock_guard g(s_lock);
    u64 sum = 0;
    for (HANDLE h : s_threads) {
        ULONG64 c = 0;
        if (QueryThreadCycleTime(h, &c)) sum += c;
    }
    return sum;
}

// Each such thread's CPU time since the last call, in milliseconds of `hz` cycles, busiest first.
void profileLogThreads(double hz) {
    static std::unordered_map<HANDLE, u64> s_last;
    std::lock_guard g(s_lock);
    std::vector<std::pair<double, DWORD>> rows;
    for (HANDLE h : s_threads) {
        ULONG64 c = 0;
        if (!QueryThreadCycleTime(h, &c)) continue;
        const u64 d = c - s_last[h];
        s_last[h] = c;
        rows.emplace_back(double(d) / hz * 1000, GetThreadId(h));
    }
    std::sort(rows.rbegin(), rows.rend());
    std::string s;
    for (size_t i = 0; i < rows.size() && i < 6; i++) s += " " + std::to_string(rows[i].second) + ":" + std::to_string(int(rows[i].first));
    log("[profile] thread CPU ms (host id:ms):%s\n", s.c_str());
}

// Called by every host thread that may run guest code.
void profileRegisterThread() {
    // Threads that run the game come first: the game must keep its 60 Hz (its cutscenes time some
    // things by frames, some by the music) while the renderer draws in-between frames on the other
    // cores; the renderer only loses in-between frames when it waits.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
    if (!h) return;
    std::lock_guard g(s_lock);
    s_threads.push_back(h);
    if (!s_enabled) return;
    static bool s_started = false;
    if (!s_started) {
        s_started = true;
        const int seconds = std::max(1, std::atoi(std::getenv("SA2_PROFILE")));
        std::thread(profilerMain, seconds).detach();
    }
}

}  // namespace rt

#else  // Linux

#include <cxxabi.h>
#include <dlfcn.h>
#include <elf.h>
#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>

namespace rt {

void hostThreadCrashStack();  // src/runtime/crash.cpp

namespace {
struct Thread {
    pthread_t self;
    pid_t tid;
    clockid_t cpu;
};
std::mutex s_lock;
std::vector<Thread> s_threads;
bool s_enabled = std::getenv("SA2_PROFILE") != nullptr;
const bool s_waits = std::getenv("SA2_PROFILE_WAITS") != nullptr;

// ---- names: each module's ELF symbol table (.symtab, else .dynsym), functions sorted by address ----
struct Symbols {
    std::vector<std::pair<u64, std::pair<u64, std::string>>> funcs;  // address (in the file's terms), size, name
    u64 firstLoad = 0;                                               // the file address of its first segment
    u64 bias = 0;                                                    // load address minus file address
};
std::mutex s_symLock;
std::map<std::string, Symbols> s_modules;

void readSymbols(const std::string& path, Symbols& out) {
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return;
    struct stat st{};
    fstat(fd, &st);
    void* map = st.st_size > 0 ? mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    close(fd);
    if (map == MAP_FAILED) return;
    const auto* base = static_cast<const u8*>(map);
    const auto* eh = reinterpret_cast<const Elf64_Ehdr*>(base);
    if (size_t(st.st_size) >= sizeof(Elf64_Ehdr) && std::memcmp(eh->e_ident, ELFMAG, SELFMAG) == 0 && eh->e_ident[EI_CLASS] == ELFCLASS64) {
        const auto* ph = reinterpret_cast<const Elf64_Phdr*>(base + eh->e_phoff);
        for (int i = 0; i < eh->e_phnum; i++)
            if (ph[i].p_type == PT_LOAD) {
                out.firstLoad = ph[i].p_vaddr & ~u64(0xFFF);
                break;
            }
        const auto* sh = reinterpret_cast<const Elf64_Shdr*>(base + eh->e_shoff);
        for (int pass = 0; pass < 2 && out.funcs.empty(); pass++) {
            const u32 want = pass == 0 ? SHT_SYMTAB : SHT_DYNSYM;
            for (int i = 0; i < eh->e_shnum; i++) {
                if (sh[i].sh_type != want) continue;
                const auto* syms = reinterpret_cast<const Elf64_Sym*>(base + sh[i].sh_offset);
                const char* strs = reinterpret_cast<const char*>(base + sh[sh[i].sh_link].sh_offset);
                for (size_t k = 0; k < sh[i].sh_size / sizeof(Elf64_Sym); k++) {
                    if (ELF64_ST_TYPE(syms[k].st_info) != STT_FUNC || !syms[k].st_value) continue;
                    std::string name = strs + syms[k].st_name;
                    int status = 0;
                    if (char* d = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status); d && status == 0) {
                        name = d;
                        std::free(d);
                        // f_XXXXXXXX(rt::Ctx&) -> f_XXXXXXXX: generated code reads as the game's addresses
                        if (const size_t paren = name.find('('); paren != std::string::npos) name.resize(paren);
                        if (name.rfind("rt::", 0) == 0) name.erase(0, 4);
                    }
                    out.funcs.push_back({syms[k].st_value, {syms[k].st_size, name}});
                }
            }
        }
        std::sort(out.funcs.begin(), out.funcs.end(), [](auto& a, auto& b) { return a.first < b.first; });
    }
    munmap(map, size_t(st.st_size));
}

// The program's and the game module's loaded ranges: samples there are of a thread running.
struct Range {
    u64 lo, hi;
};
std::vector<Range> s_ours;
void findOurs() {
    static std::once_flag s_once;
    std::call_once(s_once, [] {
        dl_iterate_phdr(
            [](dl_phdr_info* info, size_t, void*) {
                const char* n = info->dlpi_name;
                if (n && *n && !std::strstr(n, "sa2b_game")) return 0;  // (the program has no name here)
                for (int i = 0; i < info->dlpi_phnum; i++)
                    if (info->dlpi_phdr[i].p_type == PT_LOAD && (info->dlpi_phdr[i].p_flags & PF_X))
                        s_ours.push_back({info->dlpi_addr + info->dlpi_phdr[i].p_vaddr, info->dlpi_addr + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz});
                return 0;
            },
            nullptr);
    });
}
bool ours(u64 pc) {
    for (const Range& r : s_ours)
        if (pc >= r.lo && pc < r.hi) return true;
    return false;
}

std::string symName(u64 addr) {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(addr), &info) || !info.dli_fname) return "?";
    std::string path = info.dli_fname;
    const auto fbase = reinterpret_cast<u64>(info.dli_fbase);
    std::lock_guard g(s_symLock);
    auto it = s_modules.find(path);
    if (it == s_modules.end()) {
        Symbols s;
        readSymbols(path.find('/') == std::string::npos ? "/proc/self/exe" : path, s);
        // (dladdr's base is where the first segment was loaded)
        s.bias = fbase - s.firstLoad;
        it = s_modules.emplace(path, std::move(s)).first;
    }
    const Symbols& s = it->second;
    const u64 a = addr - s.bias;
    auto f = std::upper_bound(s.funcs.begin(), s.funcs.end(), a, [](u64 v, auto& e) { return v < e.first; });
    if (f != s.funcs.begin()) {
        --f;
        if (a < f->first + std::max<u64>(f->second.first, 1) || f->second.first == 0) return f->second.second;
    }
    if (info.dli_sname) return info.dli_sname;
    const char* b = std::strrchr(info.dli_fname, '/');
    char buf[64];
    std::snprintf(buf, sizeof buf, "+%llX", (unsigned long long)a);
    return std::string(b ? b + 1 : info.dli_fname) + buf;
}

// ---- sampling: a signal to one thread, which records where it is ----
struct Sample {
    std::atomic<int> done{0};
    bool wantStack = false;
    u64 pc = 0;
    int n = 0;
    void* frames[40];
};
std::atomic<Sample*> s_req{nullptr};
std::mutex s_sampleLock;  // one sample at a time
int sampleSignal() { return SIGRTMIN + 4; }

void onSample(int, siginfo_t*, void* uc) {
    Sample* r = s_req.load(std::memory_order_acquire);
    if (!r) return;
    const auto* u = static_cast<const ucontext_t*>(uc);
#if defined(__x86_64__)
    r->pc = u64(u->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
    r->pc = u64(u->uc_mcontext.pc);
#endif
    if (r->wantStack) r->n = backtrace(r->frames, 40);
    r->done.store(1, std::memory_order_release);
}

void installSampler() {
    static std::once_flag s_once;
    std::call_once(s_once, [] {
        void* prime[2];
        backtrace(prime, 2);  // (loads the unwinder outside the signal handler)
        struct sigaction sa{};
        sa.sa_sigaction = onSample;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(sampleSignal(), &sa, nullptr);
    });
}

// Where thread t is: false when it is gone. With `chain`: its innermost frames in our code.
bool sample(const Thread& t, u64& pc, std::vector<u64>* chain = nullptr, size_t maxChain = 6) {
    std::lock_guard g(s_sampleLock);
    Sample r;
    r.wantStack = chain != nullptr;
    s_req.store(&r, std::memory_order_release);
    if (syscall(SYS_tgkill, getpid(), t.tid, sampleSignal()) != 0) return s_req.store(nullptr), false;
    const auto t0 = std::chrono::steady_clock::now();
    while (!r.done.load(std::memory_order_acquire))
        if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(50)) {
            s_req.store(nullptr);
            // (it may still be on its way: wait for it, the record lives here)
            while (!r.done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(500)) {
            }
            return false;
        }
    s_req.store(nullptr);
    pc = r.pc;
    if (chain) {
        // past the handler and the signal frame: from the interrupted instruction on
        int from = 0;
        for (int i = 0; i < r.n; i++)
            if (u64(r.frames[i]) == r.pc) from = i;
        for (int i = from; i < r.n && chain->size() < maxChain; i++)
            if (ours(u64(r.frames[i]))) chain->push_back(u64(r.frames[i]));
    }
    return true;
}

std::vector<Thread> threadsNow() {
    std::lock_guard g(s_lock);
    return s_threads;
}

void reportWaits(std::map<std::vector<u64>, u32>& waits, u64 total) {
    std::map<std::string, u32> byChain;
    for (auto& [chain, n] : waits) {
        std::string key;
        for (u64 a : chain)
            if (a) key += (key.empty() ? "" : " < ") + symName(a);
        byChain[key] += n;
    }
    std::vector<std::pair<u32, std::string>> top;
    for (auto& [k, n] : byChain) top.push_back({n, k});
    std::sort(top.rbegin(), top.rend());
    log("[profile] %llu waiting samples; top wait sites:\n", (unsigned long long)total);
    for (size_t i = 0; i < top.size() && i < 15; i++)
        log("[profile] %5.1f%%  %s\n", 100.0 * top[i].first / double(total), top[i].second.c_str());
    waits.clear();
}

void report(std::unordered_map<u64, u32>& hits, u64 total) {
    std::map<std::string, u32> byName;
    for (auto& [pc, n] : hits) byName[symName(pc)] += n;
    std::vector<std::pair<u32, std::string>> top;
    for (auto& [name, n] : byName) top.push_back({n, name});
    std::sort(top.rbegin(), top.rend());
    log("[profile] %llu samples in guest-running threads; top functions:\n", (unsigned long long)total);
    for (size_t i = 0; i < top.size() && i < 40; i++)
        log("[profile] %5.1f%%  %s\n", 100.0 * top[i].first / double(total), top[i].second.c_str());
    // The hottest single addresses (as module+offset for addr2line: no line table read here).
    std::vector<std::pair<u32, u64>> pcs;
    for (auto& [pc, n] : hits) pcs.push_back({n, pc});
    std::sort(pcs.rbegin(), pcs.rend());
    for (size_t i = 0; i < pcs.size() && i < 12; i++) {
        Dl_info info{};
        dladdr(reinterpret_cast<void*>(pcs[i].second), &info);
        const char* b = info.dli_fname ? std::strrchr(info.dli_fname, '/') : nullptr;
        log("[profile]   %5.1f%% at %s %s+%llX\n", 100.0 * pcs[i].first / double(total), symName(pcs[i].second).c_str(),
            b ? b + 1 : (info.dli_fname ? info.dli_fname : "?"), (unsigned long long)(pcs[i].second - u64(info.dli_fbase)));
    }
    hits.clear();
}

void profilerMain(int seconds) {
    installSampler();
    findOurs();
    std::unordered_map<u64, u32> hits;
    std::map<std::vector<u64>, u32> waits;
    u64 total = 0, waitTotal = 0;
    auto last = std::chrono::steady_clock::now();
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for (const Thread& t : threadsNow()) {
            u64 pc = 0;
            std::vector<u64> chain;
            if (!sample(t, pc, s_waits ? &chain : nullptr)) continue;
            if (!ours(pc)) {  // waiting, or in a system library
                if (!chain.empty()) {
                    chain.resize(6);
                    waits[chain]++;
                    waitTotal++;
                }
                continue;
            }
            hits[pc]++;
            total++;
        }
        if (std::chrono::steady_clock::now() - last >= std::chrono::seconds(seconds)) {
            report(hits, total);
            if (s_waits) reportWaits(waits, waitTotal);
            total = waitTotal = 0;
            last = std::chrono::steady_clock::now();
        }
    }
}

u64 cpuNs(const Thread& t) {
    timespec ts{};
    if (clock_gettime(t.cpu, &ts) != 0) return 0;
    return u64(ts.tv_sec) * 1000000000ull + u64(ts.tv_nsec);
}
}  // namespace

// Where every thread that runs guest code is right now: each is sampled `samples` times over
// `ms` milliseconds, and its most frequent functions (guest code shows as f_XXXXXXXX) are logged -
// for hangs, when the game stops producing frames.
void profileSnapshot(int samples, int ms) {
    installSampler();
    findOurs();
    const std::vector<Thread> threads = threadsNow();
    std::vector<std::unordered_map<u64, u32>> hits(threads.size());
    std::vector<u32> outside(threads.size());
    for (int s = 0; s < samples; s++) {
        for (size_t t = 0; t < threads.size(); t++) {
            u64 pc = 0;
            if (!sample(threads[t], pc)) continue;
            if (ours(pc)) hits[t][pc]++;
            else outside[t]++;  // waiting (in the OS) or in a system library
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, ms / samples)));
    }
    for (size_t t = 0; t < threads.size(); t++) {
        if (hits[t].empty()) continue;
        std::map<std::string, u32> byName;
        for (auto& [pc, n] : hits[t]) byName[symName(pc)] += n;
        std::vector<std::pair<u32, std::string>> top;
        for (auto& [name, n] : byName) top.push_back({n, name});
        std::sort(top.rbegin(), top.rend());
        std::string line;
        for (size_t i = 0; i < top.size() && i < 6; i++) line += " " + top[i].second + " x" + std::to_string(top[i].first);
        log("[stall]   host thread %d: %u samples waiting,%s\n", int(threads[t].tid), outside[t], line.c_str());
    }
    // Then each thread's stack as it is now, innermost first (generated functions are the game's,
    // f_<address>): what a stuck thread is inside of and who called it.
    for (const Thread& t : threads) {
        u64 pc = 0;
        std::vector<u64> frames;
        if (!sample(t, pc, &frames, 16)) continue;
        std::string chain;
        for (size_t i = 0; i < frames.size(); i++) chain += (i ? " < " : " ") + symName(frames[i]);
        log("[stall]   host thread %d stack:%s\n", int(t.tid), chain.empty() ? " (none in our code)" : chain.c_str());
    }
}

// CPU time the threads that run guest code have used so far (nanoseconds: the unit of hostCycles()
// here, src/runtime/hosttime.cpp).
u64 profileGuestCycles() {
    std::lock_guard g(s_lock);
    u64 sum = 0;
    for (const Thread& t : s_threads) sum += cpuNs(t);
    return sum;
}

// Each such thread's CPU time since the last call, in milliseconds of `hz` units, busiest first.
void profileLogThreads(double hz) {
    static std::unordered_map<pid_t, u64> s_last;
    std::lock_guard g(s_lock);
    std::vector<std::pair<double, pid_t>> rows;
    for (const Thread& t : s_threads) {
        const u64 c = cpuNs(t);
        if (!c) continue;
        const u64 d = c - s_last[t.tid];
        s_last[t.tid] = c;
        rows.emplace_back(double(d) / hz * 1000, t.tid);
    }
    std::sort(rows.rbegin(), rows.rend());
    std::string s;
    for (size_t i = 0; i < rows.size() && i < 6; i++) s += " " + std::to_string(rows[i].second) + ":" + std::to_string(int(rows[i].first));
    log("[profile] thread CPU ms (host id:ms):%s\n", s.c_str());
}

// Called by every host thread that may run guest code.
void profileRegisterThread() {
    // Threads that run the game come first: the game must keep its 60 Hz (its cutscenes time some
    // things by frames, some by the music) while the renderer draws in-between frames on the other
    // cores; the renderer only loses in-between frames when it waits. (A higher priority than
    // normal needs CAP_SYS_NICE or a raised nice limit: tried, else as it is.)
    const pid_t tid = pid_t(syscall(SYS_gettid));
    setpriority(PRIO_PROCESS, id_t(tid), -5);
    hostThreadCrashStack();
    Thread t{pthread_self(), tid, CLOCK_THREAD_CPUTIME_ID};
    if (pthread_getcpuclockid(t.self, &t.cpu) != 0) return;
    std::lock_guard g(s_lock);
    s_threads.push_back(t);
    if (!s_enabled) return;
    static bool s_started = false;
    if (!s_started) {
        s_started = true;
        const int seconds = std::max(1, std::atoi(std::getenv("SA2_PROFILE")));
        std::thread(profilerMain, seconds).detach();
    }
}

}  // namespace rt

#endif
