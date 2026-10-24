#include "text_speed.h"

#include "game_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MSG_HIO_CHARS_EXTRA = 0x803E6B44u + 0x6Cu, // g_msgHIO.field_0x6c (an s16; stringSet reads it as an s8, the low byte)
    PROFILE_MSG = 0x80393798u,                 // g_profile_MSG
    MSG_PROFILE_AT = 0x10u,                    // base_process_class::mpProf
    MSG_PROCNAME_AT = 0x08u,
    MSG_DATAPROC_AT = 0x0E04u,                 // sub_msg_class::mMsgDataProc
    DP_BMGDATA = 0x3Cu,
    DP_COUNT = 0x118u,
    DP_WAITTIMER = 0x158u,
    DP_STATUS = 0x27Cu,
};

static volatile int g_speed = 1;
static unsigned g_frame;
static bool g_written; // a non-zero value is in the game's byte

void bluewake_text_speed_set(int speed) {
    g_speed = speed < 1 ? 1 : speed > 5 ? 5 : speed;
}

// A test aid (BLUEWAKE_TEXT_SPEED_LOG): the box's character index each retrace it changes, found by its process profile.
static void log_progress(CPUState* cpu) {
    static int on = -1;
    static u32 box, last_count = 0xFFFFFFFFu;
    if (on < 0)
        on = getenv("BLUEWAKE_TEXT_SPEED_LOG") != NULL;
    if (!on)
        return;
    if (box != 0u && mem_read32(cpu, box + MSG_PROFILE_AT) != PROFILE_MSG) {
        box = 0u;
        last_count = 0xFFFFFFFFu;
    }
    if (box == 0u && g_frame % 6u == 0u) {
        const u8 key[4] = {PROFILE_MSG >> 24, PROFILE_MSG >> 16, PROFILE_MSG >> 8, PROFILE_MSG};
        const u8* ram = cpu->ram;
        for (u32 off = 0; off + 4u <= cpu->ram_size;) {
            const u8* hit = memmem(ram + off, cpu->ram_size - off, key, 4);
            if (hit == NULL)
                break;
            const u32 at = (u32)(hit - ram);
            off = at + 4u;
            if (at < MSG_PROFILE_AT)
                continue;
            const u32 base = 0x80000000u + at - MSG_PROFILE_AT;
            const u32 data = mem_read32(cpu, base + MSG_DATAPROC_AT + DP_BMGDATA);
            if (mem_read16(cpu, base + MSG_PROCNAME_AT) == mem_read16(cpu, PROFILE_MSG + 8u) && data >= 0x80000000u &&
                data < 0x81800000u && mem_read32(cpu, base + MSG_DATAPROC_AT + DP_COUNT) < 4096u) {
                box = base;
                fprintf(stderr, "[text_speed] message box at %08X (speed %dx)\n", box, g_speed);
                break;
            }
        }
    }
    if (box == 0u)
        return;
    const u32 dp = box + MSG_DATAPROC_AT;
    const u32 count = mem_read32(cpu, dp + DP_COUNT);
    if (count != last_count) {
        fprintf(stderr, "[text_speed] retrace %u: character %u (+%d) wait %d status %u\n", g_frame, count,
                last_count == 0xFFFFFFFFu ? 0 : (int)(count - last_count), (int)mem_read32(cpu, dp + DP_WAITTIMER),
                mem_read8(cpu, dp + DP_STATUS));
        last_count = count;
    }
}

void bluewake_text_speed_retrace(CPUState* cpu) {
    if (cpu == NULL || cpu->ram == NULL)
        return;
    ++g_frame;
    int speed = g_speed;
    if (bluewake_game_options_get("instant_text"))
        speed = 1;
    if (speed > 1) {
        mem_write16(cpu, MSG_HIO_CHARS_EXTRA, (u16)(speed - 1));
        if (!g_written)
            fprintf(stderr, "[text_speed] %dx: %d extra characters a frame\n", speed, speed - 1);
        g_written = true;
    } else if (g_written) {
        mem_write16(cpu, MSG_HIO_CHARS_EXTRA, 0u);
        g_written = false;
        fprintf(stderr, "[text_speed] 1x: the game's own speed\n");
    }
    log_progress(cpu);
}
