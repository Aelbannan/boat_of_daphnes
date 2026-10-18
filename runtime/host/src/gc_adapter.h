// Native support for the Wii U GameCube controller adapter (USB 057E:0337): the app reads and drives it itself,
// with no other program or driver layer between. Windows needs the WinUSB driver on the adapter, which the
// Settings > Controls > GameCube Adapter > Install Adapter Driver button binds once (windows/src/adapter_driver.cpp).
//
// A worker thread keeps the adapter open over SDL's SDL_hid_* functions (SDL's own gamepad layer is told to
// ignore this device, so the two never fight over it), starts it (0x13), reads its 37-byte reports, and sends
// rumble (0x11) to the ports that have power. The wire format and the origin rule are in gc_adapter_proto.h.
// Each adapter port feeds the game's channel of the same number; the controllers' values replace nothing: they
// are merged into what the keyboard and SDL pads give (bluewake_pad_merge).
//
// With no adapter nothing happens: the thread scans now and then and the overlay does no work.
//
//   BLUEWAKE_GCADAPTER=0                        the module and its SDL ignore rule are off
//   BLUEWAKE_TEST_GCADAPTER_REPLAY=<file>       testing: no USB; the recorded reports in the file (format in
//                                               gc_adapter_proto.h) are fed by host retrace through the same
//                                               path the device's reports take
#ifndef BLUEWAKE_GC_ADAPTER_H
#define BLUEWAKE_GC_ADAPTER_H

#include "gxruntime/platform.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLUEWAKE_GCA_NOT_FOUND = 0, // no adapter the SDL can see (not plugged in, or no WinUSB driver bound)
    BLUEWAKE_GCA_CONNECTED = 1, // open and polling
    BLUEWAKE_GCA_IN_USE = 2,    // listed but cannot be opened: another program has it
    BLUEWAKE_GCA_REPLAY = 3,    // testing: fed from a replay file
} BluewakeGcaStatus;

// Once, after the platform input exists. Starts the worker (or loads the replay file).
void bluewake_gc_adapter_start(void);
// At exit: rumble off, the device released.
void bluewake_gc_adapter_stop(void);

// Settings > Controls > First Connected Port Is Player 1.
void bluewake_gc_adapter_set_first_is_p1(bool on);

// Merges the connected ports' controllers into live[0..3] (channel n for port n; see the setting above).
// `retrace` is the host's retrace count (the replay file's clock).
void bluewake_gc_adapter_overlay(DolPadState live[4], uint64_t retrace);

// Rumble. Classic: the game's own motor command for a channel (PAD_MOTOR_STOP 0, RUMBLE 1, STOP_HARD 2).
// Enhanced: all adapter ports on or off together, as every SDL gamepad is driven. Both are remembered and sent
// by the worker to the ports that have power.
void bluewake_gc_adapter_rumble_classic(unsigned channel, unsigned motor);
void bluewake_gc_adapter_rumble_enhanced(bool on);

// For the settings page.
BluewakeGcaStatus bluewake_gc_adapter_status(void);
// Bit n set: a controller is on adapter port n.
unsigned bluewake_gc_adapter_ports(void);

#ifdef __cplusplus
}
#endif

#endif
