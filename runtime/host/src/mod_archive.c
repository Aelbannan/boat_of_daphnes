/* Read-only mod archives (.zip, .rar, .rar5, .7z) over the vendored libarchive; see mod_archive.h. */
#include "mod_archive.h"

#include <archive.h>
#include <archive_entry.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef struct {
    char* name;
    unsigned long long size;
} ModArchiveEntry;

struct ModArchive {
    char* path;                  /* UTF-8, as given */
    ModArchiveEntry* entries;
    unsigned count;
    /* The read cursor: a libarchive handle positioned so that its next file header is entry
     * `cursor_next`; NULL when none is open. */
    struct archive* cursor;
    unsigned cursor_next;
};

/* A new reader for the formats mods come in. libarchive's own format detection picks among
 * them; nothing else (tar, cpio, raw, ...) is enabled so an unrelated file is refused. */
static struct archive* open_reader(const char* path_utf8) {
    struct archive* a = archive_read_new();
    if (a == NULL) {
        return NULL;
    }
    archive_read_support_format_zip(a);
    archive_read_support_format_7zip(a);
    archive_read_support_format_rar(a);
    archive_read_support_format_rar5(a);
    int r;
#ifdef _WIN32
    int wide = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, NULL, 0);
    wchar_t* wpath = wide > 0 ? (wchar_t*)malloc((size_t)wide * sizeof(wchar_t)) : NULL;
    if (wpath == NULL || MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wpath, wide) <= 0) {
        free(wpath);
        archive_read_free(a);
        return NULL;
    }
    r = archive_read_open_filename_w(a, wpath, 64 * 1024);
    free(wpath);
#else
    r = archive_read_open_filename(a, path_utf8, 64 * 1024);
#endif
    if (r != ARCHIVE_OK) {
        archive_read_free(a);
        return NULL;
    }
    return a;
}

/* The entry's name as malloc'd UTF-8 with forward slashes and no leading "./" or "/". */
static char* entry_name(struct archive_entry* e) {
    char* name = NULL;
#ifdef _WIN32
    const wchar_t* wide = archive_entry_pathname_w(e);
    if (wide != NULL) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
        if (n > 0) {
            name = (char*)malloc((size_t)n);
            if (name != NULL && WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, n, NULL, NULL) <= 0) {
                free(name);
                name = NULL;
            }
        }
    }
#else
    const char* utf8 = archive_entry_pathname_utf8(e);
    if (utf8 != NULL) {
        name = strdup(utf8);
    }
#endif
    if (name == NULL) {
        /* No conversion (a name in an unknown code page): the raw bytes. */
        const char* raw = archive_entry_pathname(e);
        if (raw == NULL) {
            return NULL;
        }
        size_t n = strlen(raw) + 1;
        name = (char*)malloc(n);
        if (name == NULL) {
            return NULL;
        }
        memcpy(name, raw, n);
    }
    for (char* p = name; *p != '\0'; ++p) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    const char* start = name;
    for (;;) {
        if (start[0] == '/') {
            ++start;
        } else if (start[0] == '.' && start[1] == '/') {
            start += 2;
        } else {
            break;
        }
    }
    if (start != name) {
        memmove(name, start, strlen(start) + 1);
    }
    return name;
}

/* Whether the entry has a usable name. archive_entry_pathname() is NULL for a name that does not
 * convert to the current locale's multibyte form (a Japanese name under an ASCII locale), so the
 * wide and UTF-8 forms are asked first. */
static int has_name(struct archive_entry* e) {
#ifdef _WIN32
    if (archive_entry_pathname_w(e) != NULL) {
        return 1;
    }
#else
    if (archive_entry_pathname_utf8(e) != NULL) {
        return 1;
    }
#endif
    return archive_entry_pathname(e) != NULL;
}

/* Advances to the next regular file; 1 with *out set, 0 at the end of the archive, -1 on error. */
static int next_file(struct archive* a, struct archive_entry** out) {
    for (;;) {
        struct archive_entry* e = NULL;
        int r = archive_read_next_header(a, &e);
        if (r == ARCHIVE_EOF) {
            return 0;
        }
        if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
            return -1;
        }
        if (archive_entry_filetype(e) == AE_IFREG && has_name(e)) {
            *out = e;
            return 1;
        }
    }
}

ModArchive* mod_archive_open(const char* path_utf8) {
    if (path_utf8 == NULL || path_utf8[0] == '\0') {
        return NULL;
    }
    struct archive* a = open_reader(path_utf8);
    if (a == NULL) {
        return NULL;
    }
    ModArchive* m = (ModArchive*)calloc(1, sizeof(*m));
    if (m == NULL) {
        archive_read_free(a);
        return NULL;
    }
    m->path = strdup(path_utf8);
    unsigned capacity = 0;
    struct archive_entry* e;
    int r;
    while ((r = next_file(a, &e)) == 1) {
        if (m->count == capacity) {
            unsigned grown = capacity != 0 ? capacity * 2 : 64;
            ModArchiveEntry* entries = (ModArchiveEntry*)realloc(m->entries, grown * sizeof(*entries));
            if (entries == NULL) {
                r = -1;
                break;
            }
            m->entries = entries;
            capacity = grown;
        }
        char* name = entry_name(e);
        if (name == NULL) {
            r = -1;
            break;
        }
        m->entries[m->count].name = name;
        m->entries[m->count].size = archive_entry_size_is_set(e) ? (unsigned long long)archive_entry_size(e) : 0;
        ++m->count;
    }
    if (r < 0 || m->path == NULL) {
        archive_read_free(a);
        mod_archive_close(m);
        return NULL;
    }
    /* The listing consumed the archive; the first read starts over. */
    archive_read_free(a);
    return m;
}

unsigned mod_archive_count(const ModArchive* archive) {
    return archive != NULL ? archive->count : 0;
}

const char* mod_archive_name(const ModArchive* archive, unsigned i) {
    return archive != NULL && i < archive->count ? archive->entries[i].name : NULL;
}

unsigned long long mod_archive_size(const ModArchive* archive, unsigned i) {
    return archive != NULL && i < archive->count ? archive->entries[i].size : 0;
}

static char fold(char c) {
    if (c == '\\') {
        return '/';
    }
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int mod_archive_find(const ModArchive* archive, const char* name_ci) {
    if (archive == NULL || name_ci == NULL) {
        return -1;
    }
    for (unsigned i = 0; i < archive->count; ++i) {
        const char* a = archive->entries[i].name;
        const char* b = name_ci;
        while (*a != '\0' && fold(*a) == fold(*b)) {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') {
            return (int)i;
        }
    }
    return -1;
}

static void drop_cursor(ModArchive* m) {
    if (m->cursor != NULL) {
        archive_read_free(m->cursor);
        m->cursor = NULL;
    }
}

unsigned char* mod_archive_read(ModArchive* archive, unsigned i, unsigned long long* size) {
    if (size != NULL) {
        *size = 0;
    }
    if (archive == NULL || i >= archive->count) {
        return NULL;
    }
    /* Continue from the cursor when the entry is ahead of it; otherwise start over. */
    if (archive->cursor == NULL || archive->cursor_next > i) {
        drop_cursor(archive);
        archive->cursor = open_reader(archive->path);
        archive->cursor_next = 0;
        if (archive->cursor == NULL) {
            return NULL;
        }
    }
    struct archive_entry* e = NULL;
    while (archive->cursor_next <= i) {
        if (next_file(archive->cursor, &e) != 1) {
            drop_cursor(archive);
            return NULL;
        }
        ++archive->cursor_next;
    }
    /* `e` is entry i, and the cursor sits on it: its data is what libarchive reads next. A failed
     * read leaves the handle unusable for the entries after it, so it is dropped. */
    unsigned long long expect = archive->entries[i].size;
    if (expect > (unsigned long long)(SIZE_MAX / 2) - 1) {
        drop_cursor(archive);
        return NULL;
    }
    size_t capacity = (size_t)expect + 1;
    unsigned char* data = (unsigned char*)malloc(capacity);
    if (data == NULL) {
        drop_cursor(archive);
        return NULL;
    }
    size_t used = 0;
    for (;;) {
        if (used + 1 == capacity) {
            /* More data than the header said (or none was recorded): grow. */
            size_t grown = capacity * 2;
            unsigned char* bigger = grown > capacity ? (unsigned char*)realloc(data, grown) : NULL;
            if (bigger == NULL) {
                free(data);
                drop_cursor(archive);
                return NULL;
            }
            data = bigger;
            capacity = grown;
        }
        la_ssize_t got = archive_read_data(archive->cursor, data + used, capacity - 1 - used);
        if (got == 0) {
            break;
        }
        if (got < 0) {
            free(data);
            drop_cursor(archive);
            return NULL;
        }
        used += (size_t)got;
    }
    data[used] = 0;
    if (size != NULL) {
        *size = used;
    }
    return data;
}

void mod_archive_close(ModArchive* archive) {
    if (archive == NULL) {
        return;
    }
    drop_cursor(archive);
    for (unsigned i = 0; i < archive->count; ++i) {
        free(archive->entries[i].name);
    }
    free(archive->entries);
    free(archive->path);
    free(archive);
}
