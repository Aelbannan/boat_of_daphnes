// The GameCube controller adapter's wire protocol (Wii U adapter WUP-028, USB 057E:0337), as plain data:
// no USB, no SDL, no threads, so it can be tested by replaying recorded reports (tests/gc_adapter_proto_test.c).
// gc_adapter.c is the part that talks to the device and the game.
//
// The protocol (a fact of the hardware; documented in docs/fork/research/gc-adapter.md):
//   host -> adapter   0x13               start polling
//   host -> adapter   0x11 m1 m2 m3 m4   rumble per port: 0 off, 1 on
//   adapter -> host   0x21 + 4 x 9 bytes one report per poll, 37 bytes
//     per port:  [0] status  bit 4 wired controller, bit 5 wireless (WaveBird), bit 2 the adapter's second
//                           USB cable is in (rumble has power)
//                [1] A=01 B=02 X=04 Y=08 Left=10 Right=20 Down=40 Up=80
//                [2] Start=01 Z=02 R=04 L=08
//                [3] stick X  [4] stick Y  [5] C-stick X  [6] C-stick Y  [7] L analog  [8] R analog
//
// Origin: a GameCube controller does not report its sticks around 128; each has a resting position of its own.
// The game's pad library reads that position once from the controller (the SI "origin" command) and subtracts
// it. The host answers that command with a fixed centre (main.c), so the origin is taken here instead: the first
// report after a port's controller appears, and every later value is sent as raw minus origin. Triggers the
// same (their origin is a resting value above zero).
//
// The register layout and the origin-on-connect rule follow Dolphin's GCAdapter.cpp (GPL-2.0-or-later); the
// button bit positions agree with SDL's SDL_hidapi_gamecube.c (Zlib). This file is written for this project.
#ifndef BLUEWAKE_GC_ADAPTER_PROTO_H
#define BLUEWAKE_GC_ADAPTER_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GCA_VENDOR_ID 0x057E
#define GCA_PRODUCT_ID 0x0337
#define GCA_PORTS 4
#define GCA_REPORT_SIZE 37
#define GCA_REPORT_ID 0x21
#define GCA_CMD_INIT 0x13
#define GCA_CMD_RUMBLE 0x11
#define GCA_RUMBLE_SIZE 5

// The bits of a pad's button word, as the SI wire carries them (and DolPadState.button).
enum {
    GCA_BTN_LEFT = 0x0001,
    GCA_BTN_RIGHT = 0x0002,
    GCA_BTN_DOWN = 0x0004,
    GCA_BTN_UP = 0x0008,
    GCA_BTN_Z = 0x0010,
    GCA_BTN_R = 0x0020,
    GCA_BTN_L = 0x0040,
    GCA_BTN_A = 0x0100,
    GCA_BTN_B = 0x0200,
    GCA_BTN_X = 0x0400,
    GCA_BTN_Y = 0x0800,
    GCA_BTN_START = 0x1000,
};

typedef enum { GCA_NONE = 0, GCA_WIRED = 1, GCA_WIRELESS = 2 } GcaType;

// What the game is given for one port: sticks around zero (signed, raw minus origin), triggers from zero.
typedef struct GcaPad {
    uint16_t buttons;
    int8_t stick_x, stick_y;
    int8_t c_x, c_y;
    uint8_t trigger_l, trigger_r;
} GcaPad;

typedef struct GcaPort {
    GcaType type;
    bool powered;      // the adapter's second USB cable is in (status bit 2)
    bool have_origin;  // the origin below has been taken
    uint8_t origin[6]; // raw stick X/Y, C-stick X/Y, L, R at connect
    uint8_t raw[6];    // raw values of the latest report
    uint16_t buttons;  // latest buttons
} GcaPort;

typedef struct GcaState {
    GcaPort port[GCA_PORTS];
    uint32_t reports;     // accepted
    uint32_t bad_reports; // refused (wrong size or id)
} GcaState;

typedef struct GcaConfig {
    // Stick values this close to the origin (|raw - origin| <= deadzone) read as zero. 0 sends every
    // difference, which is what a GameCube sends; the game's pad library applies its own dead zone on top.
    uint8_t stick_deadzone;
} GcaConfig;

// What a report changed, as bits: connect of port n is 1 << n, disconnect of port n is 1 << (n + 4).
#define GCA_EVENT_CONNECT(n) (1u << (n))
#define GCA_EVENT_DISCONNECT(n) (1u << ((n) + 4))

void gca_reset(GcaState* state);
// A report as read from the adapter (the first byte is the id 0x21). Returns the events bits, or -1 when
// `data` is not a report (shorter than 37 bytes or not starting with 0x21); state is unchanged then.
int gca_parse_report(GcaState* state, const uint8_t* data, size_t size);

bool gca_port_connected(const GcaState* state, int port);
// Rumble can be driven: a wired controller with the adapter's second USB cable in. A WaveBird has no motor.
bool gca_port_rumble_allowed(const GcaState* state, int port);
// The port's state for the game. False (and a zeroed pad) when no controller is on the port. Until the origin is
// known (a report with every axis at zero is the adapter not having read the controller yet) the sticks and
// triggers read as resting.
bool gca_port_pad(const GcaState* state, int port, const GcaConfig* config, GcaPad* out);

// The 5-byte rumble command for the ports `want` asks for: only ports that can rumble get a 1.
void gca_build_rumble(const GcaState* state, const bool want[GCA_PORTS], uint8_t out[GCA_RUMBLE_SIZE]);

// The game channel each adapter port feeds: port n is channel n. With `first_is_p1` and no controller on port 0,
// the first connected port feeds channel 0 instead (Link reads channel 0), and the others keep their numbers.
// `channel[port]` is that channel for every port, connected or not.
void gca_map_ports(const GcaState* state, bool first_is_p1, int channel[GCA_PORTS]);

// --- recorded reports for tests --------------------------------------------------------------------------
// A replay file: one entry a line, '#' starts a comment.
//   <retrace> <37 bytes in hex, spaces allowed>   the report in effect from that host retrace on
//   <retrace> unplug                              the adapter is unplugged (all ports go) from that retrace
// Retraces must not decrease. The report in effect at a retrace is the last entry at or before it.
typedef struct GcaReplayEntry {
    uint64_t retrace;
    bool unplug;
    uint8_t report[GCA_REPORT_SIZE];
} GcaReplayEntry;

// Parses `text`; returns the entry count, or -1 with `error` (when not NULL, `error_size` bytes) saying why.
int gca_replay_parse(const char* text, GcaReplayEntry* out, size_t max, char* error, size_t error_size);
// Index of the entry in effect at `retrace`, or -1 when none yet.
int gca_replay_index(const GcaReplayEntry* entries, size_t count, uint64_t retrace);

#ifdef __cplusplus
}
#endif

#endif
