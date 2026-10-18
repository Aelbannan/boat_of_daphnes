// Guest memory for the Dev Tools windows: see dev_tools.h.
#include "dev_tools.h"

#include <stdatomic.h>
#include <string.h>

enum {
    kActorQueue = 0x80372028u, // g_fopAcTg_Queue (node_list_class: head +0, tail +4, size +8)
    kQueueCapacity = 4096,
};

typedef struct Write {
    unsigned address;
    unsigned value;
    unsigned size;
} Write;

static CPUState* g_cpu;
static Write g_queue[kQueueCapacity];
static unsigned g_head, g_tail; // written under g_lock
static atomic_flag g_lock = ATOMIC_FLAG_INIT;

static void lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire)) {
    }
}
static void unlock(void) { atomic_flag_clear_explicit(&g_lock, memory_order_release); }

static bool in_main_memory(unsigned address, unsigned size) {
    return address >= 0x80000000u && size <= 0x1800000u && address - 0x80000000u <= 0x1800000u - size;
}

void bluewake_dev_retrace(CPUState* cpu) {
    g_cpu = cpu;
    if (cpu == NULL || cpu->ram == NULL)
        return;
    for (;;) {
        lock();
        if (g_head == g_tail) {
            unlock();
            return;
        }
        const Write w = g_queue[g_head % kQueueCapacity];
        ++g_head;
        unlock();
        if (!in_main_memory(w.address, w.size))
            continue;
        if (w.size == 4u)
            mem_write32(cpu, w.address, w.value);
        else if (w.size == 2u)
            mem_write16(cpu, w.address, (u16)w.value);
        else
            mem_write8(cpu, w.address, (u8)w.value);
    }
}

bool bluewake_dev_read(unsigned address, void* out, unsigned size) {
    CPUState* cpu = g_cpu;
    if (cpu == NULL || cpu->ram == NULL || !in_main_memory(address, size))
        return false;
    unsigned char* bytes = out;
    for (unsigned i = 0; i < size; ++i)
        bytes[i] = (unsigned char)mem_read8(cpu, address + i);
    return true;
}

void bluewake_dev_write(unsigned address, unsigned value, unsigned size) {
    lock();
    if (g_tail - g_head < kQueueCapacity) {
        g_queue[g_tail % kQueueCapacity] = (Write){address, value, size};
        ++g_tail;
    }
    unlock();
}

void bluewake_dev_write_bytes(unsigned address, const void* bytes, unsigned size) {
    const unsigned char* b = bytes;
    for (unsigned i = 0; i < size; ++i)
        bluewake_dev_write(address + i, b[i], 1u);
}

static unsigned read32(unsigned address) {
    unsigned char b[4] = {0};
    bluewake_dev_read(address, b, 4);
    return ((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) | ((unsigned)b[2] << 8) | b[3];
}

static unsigned read16(unsigned address) {
    unsigned char b[2] = {0};
    bluewake_dev_read(address, b, 2);
    return ((unsigned)b[0] << 8) | b[1];
}

static float read_float(unsigned address) {
    const unsigned bits = read32(address);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

unsigned bluewake_dev_actors(BluewakeDevActor* out, unsigned max) {
    unsigned count = 0;
    unsigned node = read32(kActorQueue);
    // A list is walked from outside the game thread: bound it, and stop at
    // anything that is not a pointer into main memory.
    for (unsigned guard = 0; guard < 4096u && count < max && in_main_memory(node, 16u); ++guard) {
        const unsigned actor = read32(node + 0xCu); // create_tag_class::mpTagData
        if (in_main_memory(actor, 0x290u)) {
            BluewakeDevActor* a = &out[count++];
            a->address = actor;
            a->proc = (short)read16(actor + 0x8u);
            a->params = read32(actor + 0xB0u);
            for (int i = 0; i < 3; ++i)
                a->pos[i] = read_float(actor + 0x1F8u + 4u * (unsigned)i);
            for (int i = 0; i < 3; ++i)
                a->angle[i] = (short)read16(actor + 0x20Cu + 2u * (unsigned)i);
            unsigned char health = 0;
            bluewake_dev_read(actor + 0x285u, &health, 1);
            a->health = (signed char)health;
            a->status = read32(actor + 0x1C4u);
            unsigned char group = 0;
            bluewake_dev_read(actor + 0x1BEu, &group, 1);
            a->group = group;
            a->set_id = (unsigned short)read16(actor + 0x1BCu);
        }
        node = read32(node + 0x8u); // node_class::mpNextNode
    }
    return count;
}
