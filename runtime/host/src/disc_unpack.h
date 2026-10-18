// Unpacking a player's compressed GameCube disc image to a plain ISO, natively: Dolphin's WIA and RVZ (groups stored with
// none, purge, bzip2, LZMA, LZMA2 or Zstandard, RVZ's junk packing), GCZ (zlib blocks) and CISO. A GameCube image is not
// encrypted, so no keys are involved; a Wii image (the partitions are encrypted) and the Wii-only formats (WBFS, NFS) are
// not handled and the caller says to convert the image in Dolphin.
//
// Formats: docs/WiaAndRvz.md of Dolphin (GPL-2.0-or-later) describes WIA and RVZ; this is code of our own from that description.
#pragma once

#include <stddef.h>

#include "disc_import.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BW_DISC_PLAIN = 0,  // no known container (an .iso or .gcm, or something else)
    BW_DISC_WIA,
    BW_DISC_RVZ,
    BW_DISC_GCZ,
    BW_DISC_CISO,
    BW_DISC_WBFS,       // recognised, not handled
} BluewakeDiscContainer;

// Which container the file is, by its first bytes. Unreadable files are BW_DISC_PLAIN.
BluewakeDiscContainer bluewake_disc_container(const char* path);

// Writes the image's disc to `out_path` as a plain ISO. 0 on success; 1 when this image cannot be unpacked here (a Wii
// disc, WBFS, an unknown compression: `error` says why and what to do); -1 on a failure (damaged file, no space, ...;
// `error` says which). A partly written output is removed. `progress` gets the written fraction.
int bluewake_disc_unpack(const char* in_path, const char* out_path, BlueWakeImportProgress progress, void* context,
                         char* error, size_t error_size);

#ifdef __cplusplus
}
#endif
