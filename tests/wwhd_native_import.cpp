// Runs the built-in Wind Waker HD importer from the command line (development):
//   wwhd_native_import HD-content-folder-or-image GZLE01.iso NEW-OUTPUT-FOLDER
#include "wwhd/pack.h"

#include <cstdio>
#include <cstdlib>

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
    options.progress = [](const wwhd::ImportProgress& p) {
        std::fprintf(stderr, "%s: %zu/%zu files, %zu replacements\n", p.stage.c_str(), p.files_read, p.files_total,
                     p.replacements);
    };
    try {
        const wwhd::ImportResult r = wwhd::import_pack(options);
        std::printf("Created %zu replacements; %zu conflicts skipped; %zu unsupported\n", r.replacements, r.conflicts,
                    r.errors.size());
    } catch (const wwhd::FormatError& ex) {
        std::fprintf(stderr, "Import failed: %s\n", ex.what());
        return 1;
    }
    return 0;
}
