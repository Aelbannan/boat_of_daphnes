// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Tingle Tuner's Game Boy Advance, inside the app: mGBA (MPL-2.0, runtime/third_party/mgba) running a free
// BIOS (emibios, LGPL-3.0-or-later, runtime/third_party/emibios), on a worker thread, joined to the guest's
// serial interface (SI) channel as a JoyBus device. The guest's own GBA SDK (translated code) does the JoyBus
// multiboot; this file answers its SI transfers the way a GBA would and keeps the GBA's clock on the guest's.
// The design follows Dolphin's integrated GBA (Core/HW/GBACore.cpp, SI_DeviceGBAEmu.cpp, GPL-2.0-or-later).
#ifndef BLUEWAKE_GBA_LINK_H
#define BLUEWAKE_GBA_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GBA_LINK_WIDTH 240
#define GBA_LINK_HEIGHT 160
#define GBA_LINK_GUEST_HZ 486000000u /* the guest clock the host's absolute_cycles counts */

// Starts the GBA: bios_path is a 16 KiB BIOS file, or NULL for the bundled emibios. Returns false (and logs) when
// the core cannot be made. Safe to call twice (the second call is a no-op while running).
bool gba_link_start(const char* bios_path);
void gba_link_stop(void);
bool gba_link_running(void);
// Restarts the GBA from reset (the BIOS receiver listens for a multiboot again).
void gba_link_reset(void);

// One SI transfer to the GBA on the guest's clock. out/out_len: the bytes the guest sends (command first); in:
// at least 8 bytes. Returns the response length, or 0 for "no response" (the SI reports NO_RESPONSE, as when no
// GBA is plugged in or the GBA is not in JoyBus mode yet).
int gba_link_transfer(uint64_t guest_cycles, const uint8_t* out, unsigned out_len, uint8_t* in);
// The same in two halves: begin posts the command (false: no GBA, the transfer gets no response) and returns at
// once; end, when the transfer's wire time is over, gives the answer as gba_link_transfer does, waiting only if the
// GBA has not been run to it yet. One transfer at a time, from the guest thread.
bool gba_link_transfer_begin(uint64_t guest_cycles, const uint8_t* out, unsigned out_len);
int gba_link_transfer_end(uint8_t* in);
// Guest cycles a transfer with this JoyBus command byte takes on the wire (GC 200 kbit/s, GBA 250 kbit/s).
unsigned gba_link_wire_cycles(uint8_t command);
// Lets the GBA run up to the guest's current time without waiting for it (every retrace).
void gba_link_sync(uint64_t guest_cycles);
// The GBA's buttons, KEYINPUT order: A B Select Start Right Left Up Down R L (bit 0 = A).
void gba_link_set_keys(uint16_t keys);

// The newest finished GBA frame as 240x160 RGBA8 (alpha 0xFF) into rgba (153600 bytes); returns the frame
// counter, which changes when a new frame came in since the last call with the same value of *seen. Returns 0
// when no frame has been drawn yet.
uint32_t gba_link_copy_frame(uint8_t* rgba, uint32_t seen);
// Audio: interleaved stereo s16 at gba_link_audio_rate() Hz. Returns frames read.
size_t gba_link_read_audio(int16_t* out, size_t max_frames);
unsigned gba_link_audio_rate(void);

// Diagnostics for the log and the menu.
typedef struct GbaLinkStats {
    bool running;
    bool joybus; /* the GBA has put its serial port in JoyBus mode (it answers) */
    uint64_t transfers;
    uint64_t no_response;
    uint64_t frames;
    uint32_t last_joystat;
} GbaLinkStats;
void gba_link_stats(GbaLinkStats* out);

#ifdef __cplusplus
}
#endif
#endif
