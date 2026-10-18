// A file the disc's table lacks gets an entry through the overlay (runtime/recompcore/GXRuntime/src/dvd.c, fst_add_file):
// the randomizer's res/Object/Vscroll.arc. Builds a small GameCube image with a made-up table:
//   0 /  1 res/  2 Msg/  3 a.arc  | Object/ 4  5 b.arc  6 c.arc | 7 Stage/ 8  9 s.arc |  10 root.bin
#include "gxruntime/dvd.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static void entry(unsigned char* fst, int i, int dir, unsigned name, unsigned a, unsigned b) {
    put32(fst + 12 * i, ((unsigned)dir << 24) | name);
    put32(fst + 12 * i + 4, a);
    put32(fst + 12 * i + 8, b);
}

int main(void) {
    const char* names[] = {"", "res", "Msg", "a.arc", "Object", "b.arc", "c.arc", "Stage", "s.arc", "root.bin"};
    // entries: 0 root, 1 res, 2 Msg, 3 a.arc, 4 Object, 5 b.arc, 6 c.arc, 7 Stage, 8 s.arc, 9 root.bin
    unsigned char fst[10 * 12 + 64];
    memset(fst, 0, sizeof fst);
    unsigned off[10], at = 0;
    char strings[64];
    for (int i = 0; i < 10; ++i) {
        off[i] = at;
        strcpy(strings + at, names[i]);
        at += (unsigned)strlen(names[i]) + 1u;
    }
    entry(fst, 0, 1, 0, 0, 10);
    entry(fst, 1, 1, off[1], 0, 9);
    entry(fst, 2, 1, off[2], 1, 4);
    entry(fst, 3, 0, off[3], 0x1000, 16);
    entry(fst, 4, 1, off[4], 1, 7);
    entry(fst, 5, 0, off[5], 0x2000, 16);
    entry(fst, 6, 0, off[6], 0x3000, 16);
    entry(fst, 7, 1, off[7], 1, 9);
    entry(fst, 8, 0, off[8], 0x4000, 16);
    // (entry 9 is root.bin at the root: next index 10 is the total)
    entry(fst, 9, 0, off[9], 0x5000, 16);
    memcpy(fst + 10 * 12, strings, at);
    const unsigned fst_size = 10 * 12 + at;

    unsigned char image[0x600];
    memset(image, 0, sizeof image);
    put32(image + 0x1C, 0xC2339F3Du);
    put32(image + 0x424, 0x440);
    put32(image + 0x428, fst_size);
    memcpy(image + 0x440, fst, fst_size);
    const char* path = "dvd_new_file_test.iso";
    FILE* f = fopen(path, "wb");
    assert(f != NULL && fwrite(image, 1, sizeof image, f) == sizeof image);
    fclose(f);

    assert(dvd_open_image(path));
    assert(dvd_path_to_entrynum("res/Object/b.arc") == 5 && dvd_path_to_entrynum("res/Stage/s.arc") == 8 &&
           dvd_path_to_entrynum("root.bin") == 9);
    assert(dvd_path_to_entrynum("res/Object/Vscroll.arc") < 0);

    // an existing file is overlaid in place; a missing one gets an entry at the end of its directory
    const unsigned char bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    assert(dvd_overlay_set("res/Object/c.arc", bytes, 8));
    assert(dvd_path_to_entrynum("res/Object/c.arc") == 6);
    assert(dvd_overlay_set("res/Object/Vscroll.arc", bytes, 8));
    const int added = dvd_path_to_entrynum("res/Object/Vscroll.arc");
    if (added != 7) fprintf(stderr, "added=%d\n", added);
    assert(added == 7);
    // what moved up one still resolves, by the same names
    assert(dvd_path_to_entrynum("res/Object/b.arc") == 5 && dvd_path_to_entrynum("res/Object/c.arc") == 6);
    assert(dvd_path_to_entrynum("res/Stage/s.arc") == 9 && dvd_path_to_entrynum("res/Stage") == 8);
    assert(dvd_path_to_entrynum("root.bin") == 10 && dvd_path_to_entrynum("res/Msg/a.arc") == 3);
    assert(dvd_path_to_entrynum("res/Object") == 4 && dvd_path_to_entrynum("res") == 1);
    // the overlay table followed c.arc's entry (it is before the insertion: unchanged) and serves the new file
    unsigned start = 0, length = 0;
    assert(dvd_entry_info(added, &start, &length) && start >= DVD_OVERLAY_BASE && length == 8);
    assert(dvd_entry_info(6, &start, &length) && start >= DVD_OVERLAY_BASE && length == 8);
    // a file after the insertion, overlaid later, lands on its moved entry
    assert(dvd_overlay_set("res/Stage/s.arc", bytes, 4));
    assert(dvd_entry_info(9, &start, &length) && start >= DVD_OVERLAY_BASE && length == 4);
    // a second new file, in another directory and one at the root
    assert(dvd_overlay_set("res/Msg/new.bmg", bytes, 8));
    assert(dvd_path_to_entrynum("res/Msg/new.bmg") == 4);
    assert(dvd_path_to_entrynum("res/Object/Vscroll.arc") == 8 && dvd_path_to_entrynum("res/Stage/s.arc") == 10);
    assert(dvd_overlay_set("top.bin", bytes, 8));
    assert(dvd_path_to_entrynum("top.bin") >= 0 && dvd_path_to_entrynum("root.bin") >= 0);
    // a directory that is missing is refused, and so is an empty file
    assert(!dvd_overlay_set("nodir/x.bin", bytes, 8));
    assert(!dvd_overlay_set("res/Msg/empty.bin", bytes, 0));
    dvd_close_image();
    remove(path);
    puts("dvd_new_file_test: ok");
    return 0;
}
