// BR-0018: the cutscene after Jabun's cave door (Outset, sea room 44) breaks never ending, in randomizer sessions.
//
// daObjAjav (d_a_obj_ajav.cpp) breaks in three stages. After the last one its action 3 waits for check_end():
// check_all_wait() asks whether each of its six rock parts' step is &Part_c::no_proc, a pointer to member
// function compared by value, then 60 frames, then it ends its cut of "ajav_uzu", sets its switch (the whirlpool
// shrinks), and the event goes on to the whirlpool and the boat entering the cave. A user's randomizer game stayed
// on the "the wall is gone" shot for good. The randomizer's copy of this REL is laid out differently (wwrando puts
// code at its end), so the "done" value a rock stores when it finishes falling (fall_1) and the one the check
// compares with can differ there, and the check then never passes.
//
// This guard acts only in that state: the door in its final wait (M_status 3, action 3) for 5 seconds. It sets
// every rock's step to each of the two values the REL itself uses for "nothing to do" in turn (the constructor's,
// and fall_1's as left in a finished rock's draw step) until the door moves on. Either one stops a rock; the
// game's own code then ends the cut. It logs what it saw, so a player's log says which value the check wanted.
#include "jabun_guard.h"

#include "dev_tools.h"

#include <stdio.h>
#include <string.h>

enum {
    OBJ_AJAV_PROC = 0x0088,  // fpcNm Obj_Ajav (the console's actors list)
    AJAV_PARTS = 0x890u,     // Part_c mRockParts[6]
    PART_SIZE = 0x98u,
    PART_EXE = 0x80u,        // daObjAjavPartCallback mExeProc (12 bytes)
    PART_DRAW = 0x8Cu,       // mDrawProc
    AJAV_STATUS = 0xC22u,    // u8 M_status (STATUS_MAX 4: the final wait is 3)
    AJAV_ACTION = 0xC24u,    // u8 mActionIdx (3: waiting for the rocks)
    STUCK_CHECKS = 10,       // checks a half second apart: 5 s in the final wait
};

typedef struct {
    u32 w[3];
} Ptmf;

static u32 g_door;          // the door actor last seen, 0 when none
static bool g_have_initial; // the constructor's "nothing to do", read before any rock moved
static Ptmf g_initial;
static int g_stuck, g_forced;

static Ptmf read_ptmf(CPUState* cpu, u32 a) {
    Ptmf p = {{mem_read32(cpu, a), mem_read32(cpu, a + 4u), mem_read32(cpu, a + 8u)}};
    return p;
}

static void write_ptmf(CPUState* cpu, u32 a, const Ptmf* p) {
    for (u32 i = 0; i < 3u; ++i)
        mem_write32(cpu, a + 4u * i, p->w[i]);
}

static u32 find_door(void) {
    static BluewakeDevActor list[512];
    const unsigned n = bluewake_dev_actors(list, 512u);
    for (unsigned i = 0; i < n; ++i)
        if ((unsigned short)list[i].proc == OBJ_AJAV_PROC)
            return list[i].address;
    return 0u;
}

void bluewake_jabun_guard_retrace(CPUState* cpu, const char* stage, int room, unsigned long long retrace) {
    if (strcmp(stage, "sea") != 0 || room != 44) {
        g_door = 0u;
        g_have_initial = false;
        g_stuck = g_forced = 0;
        return;
    }
    if (retrace % 30u != 0u)
        return;
    const u32 door = find_door();
    if (door != g_door) {
        g_door = door;
        g_have_initial = false;
        g_stuck = g_forced = 0;
    }
    if (door == 0u)
        return;
    const u8 status = mem_read8(cpu, door + AJAV_STATUS), action = mem_read8(cpu, door + AJAV_ACTION);
    if (!g_have_initial && status == 0u) {
        g_initial = read_ptmf(cpu, door + AJAV_PARTS + PART_EXE);
        g_have_initial = true;
    }
    if (status != 3u || action != 3u) {
        if (g_forced != 0)
            fprintf(stderr, "[jabun] the door moved on (status %u, action %u) after %d forced checks\n", status, action,
                    g_forced);
        g_stuck = g_forced = 0;
        return;
    }
    if (++g_stuck < STUCK_CHECKS)
        return;
    // A finished rock leaves fall_1's value in its draw step as well (setDrawProc(&Part_c::no_proc)).
    Ptmf finished = {{0u, 0u, 0u}};
    bool have_finished = false;
    for (u32 k = 0; k < 6u; ++k) {
        const u32 part = door + AJAV_PARTS + PART_SIZE * k;
        const Ptmf exe = read_ptmf(cpu, part + PART_EXE), draw = read_ptmf(cpu, part + PART_DRAW);
        if (g_forced == 0)
            fprintf(stderr, "[jabun] door %08X stuck in its final wait: rock %u step %08X %08X %08X, draw %08X %08X %08X\n",
                    door, k, exe.w[0], exe.w[1], exe.w[2], draw.w[0], draw.w[1], draw.w[2]);
        if (!have_finished && memcmp(&exe, &draw, sizeof exe) == 0) {
            finished = exe;
            have_finished = true;
        }
    }
    const bool use_initial = (g_forced % 2 == 0 && g_have_initial) || !have_finished;
    if (!use_initial || g_have_initial) {
        const Ptmf* value = use_initial ? &g_initial : &finished;
        if (g_forced == 0)
            fprintf(stderr, "[jabun] constructor's value %08X %08X %08X%s; finished rock's %08X %08X %08X%s\n",
                    g_initial.w[0], g_initial.w[1], g_initial.w[2], g_have_initial ? "" : " (not seen)",
                    finished.w[0], finished.w[1], finished.w[2], have_finished ? "" : " (none)");
        for (u32 k = 0; k < 6u; ++k)
            write_ptmf(cpu, door + AJAV_PARTS + PART_SIZE * k + PART_EXE, value);
        fprintf(stderr, "[jabun] forcing the rocks' step to the %s value\n", use_initial ? "constructor's" : "finished rock's");
    }
    ++g_forced;
}
