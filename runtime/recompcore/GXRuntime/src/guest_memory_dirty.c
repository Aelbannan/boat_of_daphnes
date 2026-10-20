// SPDX-License-Identifier: GPL-3.0-or-later
#include "gxruntime/guest_memory_dirty.h"

#include <string.h>

#define DOL_MEM1_TRACKED_SIZE 0x04000000u // a 32 MB MEM1 array, or the 64 MB one of the larger module
// MEM2 (the Wii's extended RAM, physical 0x10000000 up; the host gives the guest some for the puppets' heaps): tracked
// too, up to 256 MB, so a texture there is cached by its dirty epoch like one in MEM1 and not hashed at every draw.
#define DOL_MEM2_PHYSICAL_BASE 0x10000000u
#define DOL_MEM2_TRACKED_SIZE 0x10000000u
#define DOL_MEM1_DIRTY_PAGE_SHIFT 12u
#define DOL_MEM1_DIRTY_PAGE_SIZE (1u << DOL_MEM1_DIRTY_PAGE_SHIFT)
#define DOL_MEM1_DIRTY_PAGE_COUNT \
    (DOL_MEM1_TRACKED_SIZE / DOL_MEM1_DIRTY_PAGE_SIZE)
#define DOL_MEM2_DIRTY_PAGE_COUNT \
    (DOL_MEM2_TRACKED_SIZE / DOL_MEM1_DIRTY_PAGE_SIZE)

// MEM1's pages first, then MEM2's.
static u64 g_page_epochs[DOL_MEM1_DIRTY_PAGE_COUNT + DOL_MEM2_DIRTY_PAGE_COUNT];
static u64 g_next_epoch;

static bool physical_range(u32 address, u32 size, u32* start, u32* end) {
    if (size == 0u)
        return false;
    const u32 physical = address & 0x3FFFFFFFu;
    const u64 physical_end = (u64)physical + size;
    u32 base_page = 0u;
    u32 base = 0u;
    u32 limit = DOL_MEM1_TRACKED_SIZE;
    if (physical >= DOL_MEM2_PHYSICAL_BASE) {
        base_page = DOL_MEM1_DIRTY_PAGE_COUNT;
        base = DOL_MEM2_PHYSICAL_BASE;
        limit = DOL_MEM2_PHYSICAL_BASE + DOL_MEM2_TRACKED_SIZE;
    }
    if (physical >= limit || physical_end > limit)
        return false;
    *start = base_page + ((physical - base) >> DOL_MEM1_DIRTY_PAGE_SHIFT);
    *end = base_page + (((u32)(physical_end - base) - 1u) >> DOL_MEM1_DIRTY_PAGE_SHIFT);
    return true;
}

void dol_guest_memory_dirty_reset(void) {
    memset(g_page_epochs, 0, sizeof(g_page_epochs));
    g_next_epoch = 0u;
}

void dol_guest_memory_dirty_mark(u32 address, u32 size) {
    u32 start;
    u32 end;
    if (!physical_range(address, size, &start, &end))
        return;
    const u64 epoch = ++g_next_epoch;
    for (u32 page = start; page <= end; ++page)
        g_page_epochs[page] = epoch;
}

bool dol_guest_memory_dirty_epoch(u32 address, u32 size, u64* epoch) {
    u32 start;
    u32 end;
    if (epoch == NULL || !physical_range(address, size, &start, &end))
        return false;
    u64 newest = 0u;
    for (u32 page = start; page <= end; ++page) {
        if (g_page_epochs[page] > newest)
            newest = g_page_epochs[page];
    }
    *epoch = newest;
    return true;
}
