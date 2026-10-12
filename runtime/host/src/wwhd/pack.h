// The built-in Wind Waker HD importer: makes a Dolphin-format texture pack from
// a decrypted Wind Waker HD content folder and the GameCube disc, as
// scripts/import_wwhd_textures.py does (its build()), for every BlueWake build.
#pragma once

#include "wwhd.h"

#include <atomic>
#include <functional>

namespace wwhd {

struct ImportProgress {
    size_t files_read = 0;   // HD resources read so far
    size_t files_total = 0;  // HD resources to read
    size_t replacements = 0; // replacement keys so far
    std::string stage;       // what is happening, for the menu
};

struct ImportOptions {
    std::string hd_folder; // decrypted Wind Waker HD content folder
    std::string gc_disc;   // the GameCube GZLE01 rev 0 .iso/.gcm
    std::string output;    // a new folder for the pack
    std::function<void(const ImportProgress&)> progress;
    const std::atomic<bool>* cancel = nullptr;
    // SHA-256 of every image and file in import-report.json, as the script
    // writes them, to compare packs (slower; the app leaves it off).
    bool exact_report = false;
    // Threads writing PNGs: 0 for the machine's cores less one (at most 8).
    unsigned threads = 0;
};

struct ImportResult {
    size_t replacements = 0;
    size_t conflicts = 0;
    std::vector<ImportError> errors;
};

// Throws FormatError when the import cannot be made; a cancelled import throws
// and leaves nothing behind.
ImportResult import_pack(const ImportOptions& options);

// A PNG of an RGBA image (8 bits per channel), deflated with zlib.
Bytes encode_png(const struct Image& image);

} // namespace wwhd
