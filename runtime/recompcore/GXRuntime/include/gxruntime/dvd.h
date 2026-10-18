// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GXRUNTIME_DVD_H
#define GXRUNTIME_DVD_H

#include "core/cpu.h"

#include <stdbool.h>

// Host-backed GameCube DVD (disc) layer.
//
// Recompiled games read assets through the DVD SDK. On real hardware the
// apploader loads the disc File System Table (FST) into memory and the DVD
// driver streams sectors over the DI hardware; we run only the game's main.dol,
// so neither exists. Instead we serve the DVD API from the original ISO on the
// host: parse its FST, resolve paths, and copy file bytes into guest RAM.
//
// This mirrors the SDK semantics the recompiled game depends on
// (DVDConvertPathToEntrynum / DVDFastOpen / DVDReadAsyncPrio); see dvd.c.

// Open an explicit GameCube disc image path. Returns false for NULL, unreadable,
// or invalid images. CLI/environment policy belongs to the game executable.
bool dvd_open_image(const char* path);
void dvd_close_image(void);
bool dvd_image_ready(void);

// Resolve a disc-root-relative path to an FST entry number, or -1 if absent.
// Case-insensitive, matching DVDConvertPathToEntrynum.
s32 dvd_path_to_entrynum(const char* path);

// File entry -> absolute disc byte offset (*start) and byte length (*length).
// Returns false for an out-of-range entry or a directory.
bool dvd_entry_info(s32 entrynum, u32* start, u32* length);
// The file whose bytes start at `disc_off` (its FST name and disc start), or NULL; for logs only.
const char* dvd_name_for_start(u32 disc_off);
// The file containing a disc offset and where it starts, or NULL; for logs only.
const char* dvd_name_for_offset(u32 disc_off, u32* file_start);

// Copy `length` bytes at absolute disc offset `disc_off` into guest memory at
// `guest_addr`. Bytes past the image end are zero-filled (a short read on real
// hardware would surface as a DVD error; the game's transfers stay in bounds).
void dvd_read_to_guest(CPUState* cpu, u32 guest_addr, u32 disc_off, u32 length);

// File overlay (mods, player models, randomizer data): a disc file served from
// memory instead of the image. The file keeps its FST entry; dvd_entry_info
// then reports a start in an overlay range above any real disc offset
// (DVD_OVERLAY_BASE) and its new length, and reads in that range come from the
// overlay. A file the game already has open or loaded keeps what it read; a
// change applies to the next open (a scene change, for archives a scene loads).
// Thread-safe against the game's reads. The data is copied.
#define DVD_OVERLAY_BASE 0x80000000u
bool dvd_overlay_set(const char* path, const u8* data, u32 size);
// Whether the file is served from an overlay now (the randomizer's in-place switch reopens the image and drops them).
bool dvd_overlay_has(const char* path);
// Back to the disc's own file (false if it had no overlay).
bool dvd_overlay_clear(const char* path);
void dvd_overlay_clear_all(void);
// A disc file's bytes from the image (not the overlay), for code that edits
// one: *size set, NULL if absent. The caller frees it.
u8* dvd_read_image_file(const char* path, u32* size);

#endif
