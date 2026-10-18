// SPDX-License-Identifier: GPL-3.0-or-later
// Opens the archives named on the command line (.zip, .rar, .7z) through the mod archive wrapper
// (runtime/host/src/mod_archive.c): lists every file with its size and CRC-32, reads them all in
// order, then again in reverse (which re-opens the sequential formats), and checks that case-
// insensitive lookup finds each name. The archives are the caller's own files; none is shipped.
//
//   bluewake_mod_archive_test ARCHIVE...                 list and verify
//   bluewake_mod_archive_test ARCHIVE --out NAME FILE    also write entry NAME to FILE
//   bluewake_mod_archive_test ARCHIVE --expect-count N   fail unless the archive holds N files
//
// Exit status 0 when every archive passes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_archive.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

static unsigned crc32_of(const unsigned char* data, unsigned long long size) {
    static unsigned table[256];
    if (table[1] == 0) {
        for (unsigned i = 0; i < 256; ++i) {
            unsigned c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
    }
    unsigned crc = 0xFFFFFFFFu;
    for (unsigned long long i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

static int failures;
#define CHECK(cond, ...)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);        \
            printf(__VA_ARGS__);                               \
            printf("\n");                                      \
            ++failures;                                        \
        }                                                      \
    } while (0)

static int write_file(const char* path, const unsigned char* data, unsigned long long size) {
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    int ok = fwrite(data, 1, (size_t)size, f) == (size_t)size;
    return fclose(f) == 0 && ok;
}

static void upper_copy(char* out, const char* in, size_t cap) {
    size_t i = 0;
    for (; in[i] != '\0' && i + 1 < cap; ++i) {
        out[i] = (in[i] >= 'a' && in[i] <= 'z') ? (char)(in[i] - 'a' + 'A') : in[i];  // upper-case
    }
    out[i] = '\0';
}

static void test_archive(const char* path, const char* out_name, const char* out_file, long expect_count) {
    printf("== %s\n", path);
    ModArchive* a = mod_archive_open(path);
    CHECK(a != NULL, "mod_archive_open failed");
    if (a == NULL) {
        return;
    }
    unsigned n = mod_archive_count(a);
    printf("%u files\n", n);
    CHECK(expect_count < 0 || (long)n == expect_count, "expected %ld files, found %u", expect_count, n);
    CHECK(mod_archive_name(a, n) == NULL && mod_archive_size(a, n) == 0, "out-of-range index not refused");
    unsigned* crcs = (unsigned*)calloc(n != 0 ? n : 1, sizeof(unsigned));
    for (unsigned i = 0; i < n; ++i) {
        const char* name = mod_archive_name(a, i);
        unsigned long long size = mod_archive_size(a, i), got = 0;
        unsigned char* data = mod_archive_read(a, i, &got);
        CHECK(data != NULL, "read of %s failed", name);
        if (data == NULL) {
            continue;
        }
        CHECK(got == size, "%s: read %llu bytes, listed %llu", name, got, size);
        CHECK(data[got] == 0, "%s: no terminating zero", name);
        crcs[i] = crc32_of(data, got);
        printf("  [%u] %-50s %10llu bytes  crc32 %08x  first4 %02x %02x %02x %02x\n", i, name, got, crcs[i],
               got > 0 ? data[0] : 0, got > 1 ? data[1] : 0, got > 2 ? data[2] : 0, got > 3 ? data[3] : 0);
        if (out_name != NULL && out_file != NULL && strcmp(out_name, name) == 0) {
            CHECK(write_file(out_file, data, got), "could not write %s", out_file);
            printf("  wrote %s to %s\n", name, out_file);
        }
        free(data);
    }
    // Backwards: every read before the last one re-opens the archive.
    for (unsigned i = n; i-- > 0;) {
        unsigned long long got = 0;
        unsigned char* data = mod_archive_read(a, i, &got);
        CHECK(data != NULL, "second read of %s failed", mod_archive_name(a, i));
        if (data != NULL) {
            CHECK(crc32_of(data, got) == crcs[i], "%s: bytes differ on re-read", mod_archive_name(a, i));
            free(data);
        }
    }
    // The same entry twice in a row.
    if (n != 0) {
        unsigned long long s1 = 0, s2 = 0;
        unsigned char* d1 = mod_archive_read(a, 0, &s1);
        unsigned char* d2 = mod_archive_read(a, 0, &s2);
        CHECK(d1 != NULL && d2 != NULL && s1 == s2 && memcmp(d1, d2, (size_t)s1) == 0, "repeated read differs");
        free(d1);
        free(d2);
    }
    // Case-insensitive lookup.
    for (unsigned i = 0; i < n; ++i) {
        char upper[1024];
        upper_copy(upper, mod_archive_name(a, i), sizeof upper);
        int found = mod_archive_find(a, upper);
        CHECK(found >= 0 && strcmp(mod_archive_name(a, (unsigned)found), mod_archive_name(a, i)) == 0,
              "find(%s) = %d", upper, found);
    }
    CHECK(mod_archive_find(a, "no/such/file.bin") == -1, "find of a missing name");
    CHECK(mod_archive_read(a, n, NULL) == NULL, "read of an out-of-range index");
    free(crcs);
    mod_archive_close(a);
    mod_archive_close(NULL);
}

typedef struct {
    const char* path;
    const char* out_name;
    const char* out_file;
    long expect;
} Job;

static int run(int argc, char** argv) {
    Job jobs[64];
    int njobs = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--out") == 0 && njobs > 0 && i + 2 < argc) {
            jobs[njobs - 1].out_name = argv[i + 1];
            jobs[njobs - 1].out_file = argv[i + 2];
            i += 2;
        } else if (strcmp(argv[i], "--expect-count") == 0 && njobs > 0 && i + 1 < argc) {
            jobs[njobs - 1].expect = atol(argv[++i]);
        } else if (strncmp(argv[i], "--", 2) == 0 || njobs == 64) {
            printf("bad argument %s\n", argv[i]);
            return 2;
        } else {
            Job job = {argv[i], NULL, NULL, -1};
            jobs[njobs++] = job;
        }
    }
    if (njobs == 0) {
        printf("usage: %s ARCHIVE [--out NAME FILE] [--expect-count N] [ARCHIVE ...]\n", argv[0]);
        return 2;
    }
    CHECK(mod_archive_open("this-file-does-not-exist.zip") == NULL, "missing file opened");
    CHECK(mod_archive_open(argv[0]) == NULL, "an executable opened as an archive");
    for (int i = 0; i < njobs; ++i) {
        test_archive(jobs[i].path, jobs[i].out_name, jobs[i].out_file, jobs[i].expect);
    }
    printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

#ifdef _WIN32
// The command line in UTF-8 whatever the console's code page, as the app (UTF-8 manifest) sees paths.
int main(void) {
    int wide_argc = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide == NULL) {
        return 2;
    }
    char** argv = (char**)calloc((size_t)wide_argc + 1, sizeof(char*));
    for (int i = 0; i < wide_argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, NULL, 0, NULL, NULL);
        argv[i] = (char*)malloc((size_t)n);
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, argv[i], n, NULL, NULL);
    }
    return run(wide_argc, argv);
}
#else
int main(int argc, char** argv) {
    return run(argc, argv);
}
#endif
