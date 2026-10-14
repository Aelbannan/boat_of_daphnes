// Runs the built-in Wind Waker HD importer from the command line (development):
//   wwhd_native_import HD-content-folder-or-image GZLE01.iso NEW-OUTPUT-FOLDER
#include "wwhd/pack.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef WWHD_IMPORT_TESTING
#include <condition_variable>
#include <mutex>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#endif

namespace {
std::atomic<bool> cancelled{false};
std::string cancel_at;
std::atomic<unsigned> checkpoint_hits{0};
unsigned cancel_after = 1;
std::mutex barrier_lock;
std::condition_variable barrier;
bool release_worker = false;
unsigned started = 0, queued = 0;

void trigger_cancel() {
    cancelled.store(true, std::memory_order_relaxed);
    std::fprintf(stderr, "Test cancellation: %s (%u)\n", cancel_at.c_str(), checkpoint_hits.load());
}
bool async_cancel() { return cancel_at == "async-drain" || cancel_at == "async-room"; }
} // namespace

void wwhd::import_test_checkpoint(const char* name) {
    const std::string point(name);
    if (async_cancel()) {
        // Hold one real worker outside the queue lock. Let the producer fill
        // the queue, then cancel after its last check, immediately before its
        // wait. Only writer shutdown releases the worker. No sleeps or races.
        std::unique_lock<std::mutex> hold(barrier_lock);
        if (point == "png-before-encode") {
            ++started;
            barrier.notify_all();
            barrier.wait(hold, [] { return release_worker; });
        } else if (point == "png-queued") {
            ++queued;
            barrier.wait(hold, [] { return started != 0; });
        } else if (point == "png-waiting") {
            trigger_cancel();
        } else if (point == "png-stopping") {
            release_worker = true;
            barrier.notify_all();
        }
    } else if (cancel_at == point && ++checkpoint_hits == cancel_after) {
        trigger_cancel();
    }
#ifndef _WIN32
    if (cancel_at == "close-failure" && point == "write-before-close" && ++checkpoint_hits == 1) {
        // This limit affects only this generated-fixture child. A tiny PNG is
        // still buffered: close must detect the failed final flush, not publish.
        struct rlimit limit;
        if (getrlimit(RLIMIT_FSIZE, &limit) != 0)
            throw std::runtime_error("test getrlimit failed");
        limit.rlim_cur = 0;
        std::signal(SIGXFSZ, SIG_IGN);
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
            throw std::runtime_error("test setrlimit failed");
    }
#endif
}
#endif

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    const UINT code_page = GetACP();
    if (code_page != CP_UTF8) {
        std::fprintf(stderr, "Native importer requires a UTF-8 process code page (GetACP=%u). "
                             "Build with BlueWake.manifest and run on Windows 10 1903 or newer.\n", code_page);
        return 2;
    }
#endif
    if (argc < 4 || argc > 7) {
        std::fprintf(stderr, "usage: %s HD-content-folder-or-image GZLE01.iso NEW-OUTPUT-FOLDER [DISC-KEY [COMMON-KEY [TITLE-KEY]]]\n", argv[0]);
        return 2;
    }
    wwhd::ImportOptions options;
    options.hd_folder = argv[1];
    options.gc_disc = argv[2];
    options.output = argv[3];
    if (argc > 4) options.disc_key_file = argv[4];
    if (argc > 5) options.common_key_file = argv[5];
    if (argc > 6) options.title_key_file = argv[6];
    // WWHD_EXACT_REPORT=1: SHA-256s in the report, to compare with the script's.
    options.exact_report = std::getenv("WWHD_EXACT_REPORT") != nullptr;
#ifdef WWHD_IMPORT_TESTING
    if (const char* mode = std::getenv("WWHD_TEST_CANCEL")) cancel_at = mode;
    if (const char* after = std::getenv("WWHD_TEST_AFTER")) cancel_after = unsigned(std::stoul(after));
    options.cancel = &cancelled;
    if (!cancel_at.empty()) options.threads = 1;
    if (cancel_at == "pre") trigger_cancel();
#endif
    options.progress = [](const wwhd::ImportProgress& p) {
        std::fprintf(stderr, "%s: %zu/%zu files, %zu replacements\n", p.stage.c_str(), p.files_read, p.files_total,
                     p.replacements);
#ifdef WWHD_IMPORT_TESTING
        if (cancel_at == "stage:" + p.stage) trigger_cancel();
#endif
    };
    try {
        const wwhd::ImportResult r = wwhd::import_pack(options);
        std::printf("Created %zu replacements; %zu conflicts skipped; %zu unsupported\n", r.replacements, r.conflicts,
                    r.errors.size());
    } catch (const wwhd::Cancelled&) {
#ifdef WWHD_IMPORT_TESTING
        // An unwound call must not leave its cancelled token on this thread.
        wwhd::check_cancel();
        if (wwhd::import_cancel != nullptr ||
            (async_cancel() && (started != 1 || !release_worker || queued == 0))) {
            std::fprintf(stderr, "Cancellation scope/worker cleanup failed\n");
            return 2;
        }
        if (async_cancel())
            std::fprintf(stderr, "Async cleanup: %u started, %u queued, joined\n", started, queued);
#endif
        std::fprintf(stderr, "Import cancelled\n");
        return 3;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Import failed: %s\n", ex.what());
        return 1;
    }
    return 0;
}
