// SPDX-License-Identifier: GPL-3.0-or-later
// Host-backed GameCube DVD layer. See dvd.h.
#include "gxruntime/dvd.h"
#include "gxruntime/guest_memory_dirty.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GameCube disc header fields (big-endian, in the first sector).
#define DISC_MAGIC_OFFSET 0x1Cu
#define DISC_MAGIC        0xC2339F3Du
#define DISC_FST_OFFSET   0x424u  // byte offset of the FST on disc
#define DISC_FST_SIZE     0x428u  // byte size of the FST

// One FST entry is 12 bytes: a flags/string-offset word, then two words whose
// meaning depends on whether the entry is a file or a directory.
#define FST_ENTRY_SIZE 12u

static FILE* g_iso;
static u8*   g_fst;          // raw FST blob (entries followed by the string table)
static u32   g_fst_size;
static u32   g_num_entries;  // total entries; equals root entry's "next" field
static const char* g_strings;  // string table, inside g_fst
static bool  g_ready;
static bool  g_materialization_trace;
static unsigned g_materialization_reports;

static u32 be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

// ---------------------------------------------------------------------------
// FST entry accessors (mirror dvdfs.c macros)
// ---------------------------------------------------------------------------
static const u8* fst_entry(u32 i) { return g_fst + (size_t)i * FST_ENTRY_SIZE; }
static bool entry_is_dir(u32 i)   { return (be32(fst_entry(i)) & 0xFF000000u) != 0; }
static u32  entry_stroff(u32 i)   { return be32(fst_entry(i)) & 0x00FFFFFFu; }
static u32  entry_parent(u32 i)   { return be32(fst_entry(i) + 4); }  // dir: parent index
static u32  entry_next(u32 i)     { return be32(fst_entry(i) + 8); }  // dir: first index past it
static u32  entry_filepos(u32 i)  { return be32(fst_entry(i) + 4); }  // file: disc offset
static u32  entry_filelen(u32 i)  { return be32(fst_entry(i) + 8); }  // file: byte length

// Case-insensitive prefix match: true when all of `string` matches `path` and
// the path component ends there ('/' or NUL). Mirrors dvdfs.c isSame().
static bool name_matches(const char* path, const char* string) {
    while (*string != '\0') {
        if (tolower((unsigned char)*path) != tolower((unsigned char)*string))
            return false;
        path++;
        string++;
    }
    return *path == '/' || *path == '\0';
}

// ---------------------------------------------------------------------------
// Image loading
// ---------------------------------------------------------------------------
static bool load_from(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;

    u8 header[0x440];
    if (fread(header, 1, sizeof header, f) != sizeof header) {
        fclose(f);
        return false;
    }
    if (be32(header + DISC_MAGIC_OFFSET) != DISC_MAGIC) {
        fclose(f);  // not a GameCube disc image
        return false;
    }

    u32 fst_off = be32(header + DISC_FST_OFFSET);
    u32 fst_size = be32(header + DISC_FST_SIZE);
    if (fst_size < FST_ENTRY_SIZE || fst_size > 0x4000000u) {
        fclose(f);
        return false;
    }

    u8* fst = (u8*)malloc(fst_size);
    if (!fst) {
        fclose(f);
        return false;
    }
    if (fseeko(f, (off_t)fst_off, SEEK_SET) != 0 ||
        fread(fst, 1, fst_size, f) != fst_size) {
        free(fst);
        fclose(f);
        return false;
    }

    u32 num_entries = be32(fst + 8);  // root entry's "next" field = total count
    if ((u64)num_entries * FST_ENTRY_SIZE > fst_size) {
        free(fst);
        fclose(f);
        return false;
    }

    g_iso = f;
    g_fst = fst;
    g_fst_size = fst_size;
    g_num_entries = num_entries;
    g_strings = (const char*)(fst + (size_t)num_entries * FST_ENTRY_SIZE);
    g_ready = true;
    fprintf(stderr, "[dvd] disc image: %s (%u FST entries)\n", path, num_entries);
    return true;
}

bool dvd_open_image(const char* path) {
    if (g_ready)
        return true;
    if (path != NULL && path[0] != '\0' && load_from(path)) {
        g_materialization_trace = getenv("BLUEWAKE_TRACE_DVD_MATERIALIZATION") != NULL;
        g_materialization_reports = 0u;
        return true;
    }
    fprintf(stderr, "[dvd] no readable GameCube disc image at '%s'\n",
            path != NULL ? path : "(null)");
    return false;
}

void dvd_close_image(void) {
    dvd_overlay_clear_all();
    if (g_iso)
        fclose(g_iso);
    free(g_fst);
    g_iso = NULL;
    g_fst = NULL;
    g_strings = NULL;
    g_ready = false;
    g_materialization_trace = false;
    g_materialization_reports = 0u;
}

bool dvd_image_ready(void) {
    return g_ready;
}

// ---------------------------------------------------------------------------
// Path resolution (mirrors DVDConvertPathToEntrynum; current dir is always the
// root, since the game never issues DVDChangeDir).
// ---------------------------------------------------------------------------
s32 dvd_path_to_entrynum(const char* path) {
    if (!g_ready || !path)
        return -1;

    u32 dir = 0;  // currentDirectory == root
    while (1) {
        if (*path == '\0')
            return (s32)dir;
        if (*path == '/') {
            dir = 0;
            path++;
            continue;
        }
        if (*path == '.') {
            if (path[1] == '.') {
                if (path[2] == '/') {
                    dir = entry_parent(dir);
                    path += 3;
                    continue;
                }
                if (path[2] == '\0')
                    return (s32)entry_parent(dir);
            } else if (path[1] == '/') {
                path += 2;
                continue;
            } else if (path[1] == '\0') {
                return (s32)dir;
            }
        }

        const char* end = path;
        while (*end != '\0' && *end != '/')
            end++;
        bool want_dir = (*end != '\0');
        u32 length = (u32)(end - path);

        u32 i;
        bool found = false;
        for (i = dir + 1; i < entry_next(dir);
             i = entry_is_dir(i) ? entry_next(i) : (i + 1)) {
            if (!entry_is_dir(i) && want_dir)
                continue;
            if (name_matches(path, g_strings + entry_stroff(i))) {
                found = true;
                break;
            }
        }
        if (!found)
            return -1;
        if (!want_dir)
            return (s32)i;

        dir = i;
        path += length + 1;
    }
}

// ---------------------------------------------------------------------------
// File overlay (see dvd.h). Overlay files live in a range of virtual disc
// offsets from DVD_OVERLAY_BASE, each at its own 32 KB-aligned start, assigned
// once per entry and kept while the entry has an overlay (a larger replacement
// moves to a new start). A spinlock guards the table: the menu sets entries,
// the game thread reads.
// ---------------------------------------------------------------------------
typedef struct OverlayFile {
    s32 entry;
    u32 start; // virtual disc offset
    u32 size;
    u32 room;  // bytes reserved at start
    u8* data;
} OverlayFile;

static OverlayFile* g_overlay;
static u32 g_overlay_count, g_overlay_capacity;
static u32 g_overlay_next = DVD_OVERLAY_BASE;
static atomic_flag g_overlay_lock = ATOMIC_FLAG_INIT;

static void overlay_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_overlay_lock, memory_order_acquire)) {
    }
}
static void overlay_unlock(void) { atomic_flag_clear_explicit(&g_overlay_lock, memory_order_release); }

static OverlayFile* overlay_find_entry(s32 entry) {
    for (u32 i = 0; i < g_overlay_count; ++i)
        if (g_overlay[i].entry == entry)
            return &g_overlay[i];
    return NULL;
}

// A file the disc's table lacks (a randomizer adds some, e.g. res/Object/Vscroll.arc): a new entry at the end of its
// directory's range, so the name resolves like any other. Every entry from there on moves up one (so do the overlay table's
// entry numbers and the directories' parent and end indexes); the name is appended to the string table. Meant for the
// moment the disc opens, before the game holds an entry number. Returns the new entry, or -1 (a directory is missing).
static s32 fst_add_file(const char* path, u32 size) {
    if (!g_ready || path == NULL)
        return -1;
    const char* last = strrchr(path, '/');
    const char* name = last != NULL ? last + 1 : path;
    if (*name == '\0')
        return -1;
    u32 parent = 0;
    if (last != NULL) {
        char dir[256];
        const size_t n = (size_t)(last - path);
        if (n == 0 || n >= sizeof dir)
            return -1;
        memcpy(dir, path, n);
        dir[n] = '\0';
        const s32 d = dvd_path_to_entrynum(dir);
        if (d < 0 || !entry_is_dir((u32)d))
            return -1;
        parent = (u32)d;
    }
    const u32 at = entry_next(parent);  // insertion index: the end of the parent's range
    const u32 old_count = g_num_entries;
    const size_t name_len = strlen(name) + 1u;
    const u32 old_strings = g_fst_size - old_count * FST_ENTRY_SIZE;
    const u32 new_size = (old_count + 1u) * FST_ENTRY_SIZE + old_strings + (u32)name_len;
    u8* blob = (u8*)calloc(1, new_size);
    if (blob == NULL)
        return -1;
    memcpy(blob, g_fst, (size_t)at * FST_ENTRY_SIZE);
    memcpy(blob + (size_t)(at + 1u) * FST_ENTRY_SIZE, g_fst + (size_t)at * FST_ENTRY_SIZE,
           (size_t)(old_count - at) * FST_ENTRY_SIZE);
    memcpy(blob + (size_t)(old_count + 1u) * FST_ENTRY_SIZE, g_fst + (size_t)old_count * FST_ENTRY_SIZE, old_strings);
    const u32 name_off = old_strings;
    u8* e = blob + (size_t)at * FST_ENTRY_SIZE;  // the new file: name offset, disc position (the overlay's), length
    e[0] = 0; e[1] = (u8)(name_off >> 16); e[2] = (u8)(name_off >> 8); e[3] = (u8)name_off;  // a file: flag 0, name offset
    const u32 pos = DVD_OVERLAY_BASE;
    e[4] = (u8)(pos >> 24); e[5] = (u8)(pos >> 16); e[6] = (u8)(pos >> 8); e[7] = (u8)pos;
    e[8] = (u8)(size >> 24); e[9] = (u8)(size >> 16); e[10] = (u8)(size >> 8); e[11] = (u8)size;
    memcpy(blob + (size_t)(old_count + 1u) * FST_ENTRY_SIZE + old_strings, name, name_len);
    // directories: an ancestor of the new file (or its parent) gets one more entry in its range; one that moved up has its
    // parent index and end index shifted
    for (u32 j = 0; j < old_count + 1u; ++j) {
        if (j == at)
            continue;
        u8* d = blob + (size_t)j * FST_ENTRY_SIZE;
        if ((d[0] & 0xFFu) == 0u)
            continue;  // a file (the root has flag 1)
        u32 par = be32(d + 4), nxt = be32(d + 8);
        const bool ancestor = j <= parent && nxt >= at;
        if (j > at) {
            if (par >= at)
                par++;
            nxt++;
        } else if (ancestor) {
            nxt++;
        }
        d[4] = (u8)(par >> 24); d[5] = (u8)(par >> 16); d[6] = (u8)(par >> 8); d[7] = (u8)par;
        d[8] = (u8)(nxt >> 24); d[9] = (u8)(nxt >> 16); d[10] = (u8)(nxt >> 8); d[11] = (u8)nxt;
    }
    for (u32 i = 0; i < g_overlay_count; ++i)
        if (g_overlay[i].entry >= (s32)at)
            g_overlay[i].entry++;
    free(g_fst);
    g_fst = blob;
    g_fst_size = new_size;
    g_num_entries = old_count + 1u;
    g_strings = (const char*)(blob + (size_t)g_num_entries * FST_ENTRY_SIZE);
    fprintf(stderr, "[dvd] new file %s (entry %u): the disc's table did not have it\n", path, at);
    return (s32)at;
}

bool dvd_overlay_set(const char* path, const u8* data, u32 size) {
    s32 entry = dvd_path_to_entrynum(path);
    if (entry < 0 && data != NULL && size != 0u) {
        overlay_lock();
        entry = dvd_path_to_entrynum(path);
        if (entry < 0)
            entry = fst_add_file(path, size);
        overlay_unlock();
    }
    if (entry < 0 || entry_is_dir((u32)entry) || data == NULL || size == 0u)
        return false;
    u8* copy = (u8*)malloc(size);
    if (copy == NULL)
        return false;
    memcpy(copy, data, size);
    overlay_lock();
    OverlayFile* f = overlay_find_entry(entry);
    if (f == NULL) {
        if (g_overlay_count == g_overlay_capacity) {
            const u32 capacity = g_overlay_capacity ? g_overlay_capacity * 2u : 64u;
            OverlayFile* grown = (OverlayFile*)realloc(g_overlay, capacity * sizeof *grown);
            if (grown == NULL) {
                overlay_unlock();
                free(copy);
                return false;
            }
            g_overlay = grown;
            g_overlay_capacity = capacity;
        }
        f = &g_overlay[g_overlay_count++];
        memset(f, 0, sizeof *f);
        f->entry = entry;
    }
    if (f->room < size) {
        const u32 room = (size + 0x7FFFu) & ~0x7FFFu;
        if ((u64)g_overlay_next + room > 0xFFFF0000u) {
            overlay_unlock();
            free(copy);
            return false;
        }
        f->start = g_overlay_next;
        f->room = room;
        g_overlay_next += room;
    }
    u8* old = f->data;
    f->data = copy;
    f->size = size;
    overlay_unlock();
    free(old);
    fprintf(stderr, "[dvd] overlay %s: %u bytes (disc file %u bytes)\n", path, size, entry_filelen((u32)entry));
    return true;
}

bool dvd_overlay_has(const char* path) {
    const s32 entry = dvd_path_to_entrynum(path);
    if (entry < 0)
        return false;
    overlay_lock();
    const bool has = overlay_find_entry(entry) != NULL;
    overlay_unlock();
    return has;
}

bool dvd_overlay_clear(const char* path) {
    const s32 entry = dvd_path_to_entrynum(path);
    if (entry < 0)
        return false;
    overlay_lock();
    OverlayFile* f = overlay_find_entry(entry);
    u8* old = NULL;
    if (f != NULL) {
        old = f->data;
        *f = g_overlay[--g_overlay_count];
    }
    overlay_unlock();
    free(old);
    return f != NULL;
}

void dvd_overlay_clear_all(void) {
    overlay_lock();
    for (u32 i = 0; i < g_overlay_count; ++i)
        free(g_overlay[i].data);
    g_overlay_count = 0;
    overlay_unlock();
}

u8* dvd_read_image_file(const char* path, u32* size) {
    const s32 entry = dvd_path_to_entrynum(path);
    if (entry < 0 || entry_is_dir((u32)entry) || g_iso == NULL)
        return NULL;
    const u32 length = entry_filelen((u32)entry);
    u8* data = (u8*)malloc(length ? length : 1u);
    if (data == NULL)
        return NULL;
    if (fseeko(g_iso, (off_t)entry_filepos((u32)entry), SEEK_SET) != 0 || fread(data, 1, length, g_iso) != length) {
        free(data);
        return NULL;
    }
    if (size)
        *size = length;
    return data;
}

// Reads in the overlay range: false if `disc_off` is a real disc offset.
static bool overlay_read(CPUState* cpu, u32 guest_addr, u32 disc_off, u32 length) {
    if (disc_off < DVD_OVERLAY_BASE)
        return false;
    overlay_lock();
    const OverlayFile* f = NULL;
    for (u32 i = 0; i < g_overlay_count; ++i)
        if (disc_off >= g_overlay[i].start && disc_off < g_overlay[i].start + g_overlay[i].room) {
            f = &g_overlay[i];
            break;
        }
    for (u32 k = 0; k < length; ++k) {
        const u32 at = disc_off + k;
        const u8 b = f != NULL && at - f->start < f->size ? f->data[at - f->start] : 0u;
        if (guest_addr + k >= GC_RAM_BASE && guest_addr + k < GC_RAM_BASE + cpu->ram_size)
            cpu->ram[guest_addr + k - GC_RAM_BASE] = b;
        else if (guest_addr + k >= GC_RAM_UNCACHED && guest_addr + k < GC_RAM_UNCACHED + cpu->ram_size)
            cpu->ram[guest_addr + k - GC_RAM_UNCACHED] = b;
        else
            mem_write8(cpu, guest_addr + k, b);
    }
    overlay_unlock();
    dol_guest_memory_dirty_mark(guest_addr, length);
    return true;
}

const char* dvd_name_for_offset(u32 disc_off, u32* file_start) {
    if (!g_ready)
        return NULL;
    for (u32 i = 1u; i < g_num_entries; ++i)
        if (!entry_is_dir(i) && entry_filepos(i) <= disc_off && disc_off - entry_filepos(i) < entry_filelen(i)) {
            if (file_start)
                *file_start = entry_filepos(i);
            return g_strings + entry_stroff(i);
        }
    return NULL;
}

const char* dvd_name_for_start(u32 disc_off) {
    if (!g_ready)
        return NULL;
    for (u32 i = 1u; i < g_num_entries; ++i)
        if (!entry_is_dir(i) && entry_filepos(i) <= disc_off && disc_off - entry_filepos(i) < entry_filelen(i))
            return g_strings + entry_stroff(i);
    return NULL;
}

bool dvd_entry_info(s32 entrynum, u32* start, u32* length) {
    if (!g_ready || entrynum < 0 || (u32)entrynum >= g_num_entries)
        return false;
    if (entry_is_dir((u32)entrynum))
        return false;
    u32 s = entry_filepos((u32)entrynum), n = entry_filelen((u32)entrynum);
    overlay_lock();
    const OverlayFile* f = overlay_find_entry(entrynum);
    if (f != NULL) {
        s = f->start;
        n = f->size;
    }
    overlay_unlock();
    if (start)
        *start = s;
    if (length)
        *length = n;
    return true;
}

// ---------------------------------------------------------------------------
// Disc read -> guest RAM
// ---------------------------------------------------------------------------
void dvd_read_to_guest(CPUState* cpu, u32 guest_addr, u32 disc_off, u32 length) {
    if (!g_ready || length == 0)
        return;
    if (overlay_read(cpu, guest_addr, disc_off, length))
        return;

    // MEM1 reads use the authentic bulk-DMA fast path below, which bypasses
    // mem_write8 and its journal. Observe only the file-select resource range
    // so source ownership is visible without changing emulation behavior.
    const u32 target_start = 0x81512AC0u;
    const u32 target_end = target_start + 0x4360u;
    const u64 read_end = (u64)guest_addr + length;
    if (g_materialization_trace && g_materialization_reports < 64u &&
        guest_addr < target_end && read_end > target_start) {
        const char* name = "<unknown>";
        u32 entry = 0xFFFFFFFFu;
        for (u32 i = 1u; i < g_num_entries; ++i) {
            if (entry_is_dir(i) || entry_filepos(i) != disc_off)
                continue;
            name = g_strings + entry_stroff(i);
            entry = i;
            break;
        }
        u8 prefix[16] = {0};
        const u32 prefix_length = length < sizeof(prefix) ? length : sizeof(prefix);
        if (fseeko(g_iso, (off_t)disc_off, SEEK_SET) == 0)
            (void)fread(prefix, 1, prefix_length, g_iso);
        fprintf(stderr,
                "[dvd-materialize] guest=0x%08X length=0x%X disc=0x%08X "
                "entry=%u name=\"%s\" prefix=%02X%02X%02X%02X%02X%02X%02X%02X"
                "%02X%02X%02X%02X%02X%02X%02X%02X\n",
                guest_addr, length, disc_off, entry, name,
                prefix[0], prefix[1], prefix[2], prefix[3],
                prefix[4], prefix[5], prefix[6], prefix[7],
                prefix[8], prefix[9], prefix[10], prefix[11],
                prefix[12], prefix[13], prefix[14], prefix[15]);
        g_materialization_reports++;
    }

    // Fast path: the destination lives entirely in cached or uncached MEM1, so
    // copy disc bytes straight into the flat guest RAM (both are big-endian raw
    // bytes -- no swizzling, exactly as a real DI DMA would land them).
    u8* dst = NULL;
    if (guest_addr >= GC_RAM_BASE && guest_addr + length <= GC_RAM_BASE + cpu->ram_size)
        dst = cpu->ram + (guest_addr - GC_RAM_BASE);
    else if (guest_addr >= GC_RAM_UNCACHED &&
             guest_addr + length <= GC_RAM_UNCACHED + cpu->ram_size)
        dst = cpu->ram + (guest_addr - GC_RAM_UNCACHED);

    if (dst) {
        size_t got = 0;
        if (fseeko(g_iso, (off_t)disc_off, SEEK_SET) == 0)
            got = fread(dst, 1, length, g_iso);
        if (got < length)
            memset(dst + got, 0, length - got);  // zero-fill past image end
        dol_guest_memory_dirty_mark(guest_addr, length);
        if (g_materialization_trace && guest_addr == 0x814DE360u &&
            length == 0x52060u) {
            const u32 target_offset = 0x81512AC0u - guest_addr;
            const u8* target = dst + target_offset;
            fprintf(stderr,
                    "[dvd-materialize-readback] guest=0x81512AC0 "
                    "word0=%02X%02X%02X%02X d84=%02X%02X%02X%02X got=0x%zX\n",
                    target[0], target[1], target[2], target[3],
                    target[0xD84], target[0xD85], target[0xD86], target[0xD87],
                    got);
        }
        return;
    }

    // Fallback for a non-MEM1 target: route each byte through the CPU so any
    // external region (VM window, etc.) is honored. Not expected in practice.
    for (u32 k = 0; k < length; k++) {
        u8 b = 0;
        if (fseeko(g_iso, (off_t)(disc_off + k), SEEK_SET) == 0)
            (void)(fread(&b, 1, 1, g_iso) == 1);
        mem_write8(cpu, guest_addr + k, b);
    }
}
