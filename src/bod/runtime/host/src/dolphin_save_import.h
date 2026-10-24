// Import Wind Waker (GZLE01) saves made by Dolphin into BlueWake's memory card.
//
// Dolphin exports a save as a .gci (a 64-byte GameCube directory entry, then
// the file's blocks) and keeps whole cards as .raw images. The game's save is
// the 'gczelda' file: 12 blocks of 0x2000, where blocks 1 and 2 are two copies
// of three 0x770-byte quest logs, each with its own checksum, followed by a
// checksum of the block (zeldaret/tww m_Do_MemCardRWmng.h). BlueWake's card is
// the GXRuntime container (magic DOLCARD1, ref/recompcore memory_card.c).
// build/device-setup/ww_save.py is the reference this ports.
#ifndef BLUEWAKE_DOLPHIN_SAVE_IMPORT_H
#define BLUEWAKE_DOLPHIN_SAVE_IMPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BW_QUEST_LOGS 3

typedef struct BWQuestLog {
    bool empty;        // no player name: the game shows this quest log as new
    bool checksum_ok;  // the quest log's own checksum matches
    char name[17];     // player name, printable ASCII
    unsigned max_life; // quarter hearts
    unsigned rupees;
} BWQuestLog;

typedef struct BWDolphinSave {
    uint8_t entry[64]; // GameCube directory entry (the .gci header)
    uint8_t* data;     // the gczelda file, 98,304 bytes
    size_t length;
} BWDolphinSave;

// Each call returns NULL on success, or a short plain-English reason.

// Reads a .gci, or a raw memory card image holding a gczelda file.
const char* bw_dolphin_save_parse(const uint8_t* bytes, size_t size, BWDolphinSave* out);
void bw_dolphin_save_free(BWDolphinSave* save);

// The quest logs of a gczelda file (either copy, preferring the first).
void bw_gczelda_quest_logs(const uint8_t* gczelda, BWQuestLog out[BW_QUEST_LOGS]);

// The quest logs on a BlueWake card. *has_saves is false when the card has no
// gczelda file yet; out is then left zeroed.
const char* bw_card_quest_logs(const uint8_t* card, size_t size, bool* has_saves,
                               BWQuestLog out[BW_QUEST_LOGS]);

// Puts quest log src_slot (1-3) of the Dolphin save into slot dst_slot (1-3)
// of both copies on the card, with every checksum and container hash
// recomputed. If the card has no gczelda file yet, the whole Dolphin file is
// added instead and the slots are ignored. *out is malloc'd; the caller frees.
const char* bw_card_import(const uint8_t* card, size_t size, const BWDolphinSave* save,
                           int src_slot, int dst_slot, uint8_t** out, size_t* out_size);

#ifdef __cplusplus
}
#endif

#endif
