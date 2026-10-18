// Guest memory for the Dev Tools windows (Save Editor, Flag Viewer, Actor
// Viewer, Console), used from the menu's thread. Reads are direct and may see
// a frame in the middle of a change, which a viewer does not mind. Writes wait
// in a queue and are made by the game thread at the next retrace, so the game
// never runs in the middle of one.
#ifndef BLUEWAKE_DEV_TOOLS_H
#define BLUEWAKE_DEV_TOOLS_H

#include "core/cpu.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Once per retrace on the game thread (game_state.c).
void bluewake_dev_retrace(CPUState* cpu);

// Copies size bytes at a guest address, in the guest's byte order. False
// before the game runs or outside main memory.
bool bluewake_dev_read(unsigned address, void* out, unsigned size);
// Queues a write of 1, 2 or 4 bytes (value in the host's order).
void bluewake_dev_write(unsigned address, unsigned value, unsigned size);
// Queues bytes (a name, say).
void bluewake_dev_write_bytes(unsigned address, const void* bytes, unsigned size);

typedef struct BluewakeDevActor {
    unsigned address;
    short proc;        // the process (actor type) number, fpcNm_*
    unsigned params;
    float pos[3];
    short angle[3];    // shape_angle
    int health;
    unsigned status;
    unsigned char group;
    unsigned short set_id;
} BluewakeDevActor;

// The live actors (g_fopAcTg_Queue), in list order, up to max. Returns how many.
unsigned bluewake_dev_actors(BluewakeDevActor* out, unsigned max);

#ifdef __cplusplus
}
#endif

#endif
