// The GameCube adapter's protocol (runtime/host/src/gc_adapter_proto.c) replayed from reports built byte by byte to
// the documented layout (docs/fork/research/gc-adapter.md section 2.4; Dolphin's GCAdapter.cpp and SDL's
// SDL_hidapi_gamecube.c agree on it). No real adapter capture was available when this was written, so these are
// not recordings: they are the layout written out, with the odd cases a real adapter produces (a port that
// connects with zeros first, a pad with its stick off centre, a WaveBird, a missing power cable, unplugging).
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gc_adapter_proto.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t status, b1, b2, sx, sy, cx, cy, l, r;
} Block;

static const Block kEmpty = {0};

// A resting wired, powered pad whose stick is off centre, as real ones are.
static Block resting(void) {
    Block b = {0x14, 0, 0, 0x7C, 0x86, 0x81, 0x7E, 0x1C, 0x1A};
    return b;
}

static void make_report(uint8_t out[GCA_REPORT_SIZE], const Block blocks[4]) {
    out[0] = GCA_REPORT_ID;
    for (int p = 0; p < 4; p++)
        memcpy(out + 1 + 9 * p, &blocks[p], 9);
}

static int feed(GcaState* s, const Block blocks[4]) {
    uint8_t report[GCA_REPORT_SIZE];
    make_report(report, blocks);
    return gca_parse_report(s, report, sizeof report);
}

static GcaPad pad_of(const GcaState* s, int port) {
    GcaPad pad;
    const GcaConfig config = {0};
    assert(gca_port_pad(s, port, &config, &pad));
    return pad;
}

static void test_layout(void) {
    assert(sizeof(Block) == 9);
    GcaState s;
    gca_reset(&s);
    Block b[4] = {kEmpty, kEmpty, kEmpty, kEmpty};
    // The byte layout, spelled out: 21 | 14 00 00 7C 86 81 7E 1C 1A | 00*9 | 00*9 | 00*9
    uint8_t report[GCA_REPORT_SIZE] = {0x21, 0x14, 0x00, 0x00, 0x7C, 0x86, 0x81, 0x7E, 0x1C, 0x1A};
    int events = gca_parse_report(&s, report, sizeof report);
    assert(events == (int)GCA_EVENT_CONNECT(0));
    assert(s.port[0].type == GCA_WIRED && s.port[0].powered && s.port[0].have_origin);
    assert(s.port[0].origin[0] == 0x7C && s.port[0].origin[1] == 0x86 && s.port[0].origin[2] == 0x81 &&
           s.port[0].origin[3] == 0x7E && s.port[0].origin[4] == 0x1C && s.port[0].origin[5] == 0x1A);
    (void)b;

    // Every button on its documented bit.
    struct {
        uint8_t b1, b2;
        uint16_t want;
    } cases[] = {
        {0x01, 0x00, GCA_BTN_A},     {0x02, 0x00, GCA_BTN_B},     {0x04, 0x00, GCA_BTN_X},
        {0x08, 0x00, GCA_BTN_Y},     {0x10, 0x00, GCA_BTN_LEFT},  {0x20, 0x00, GCA_BTN_RIGHT},
        {0x40, 0x00, GCA_BTN_DOWN},  {0x80, 0x00, GCA_BTN_UP},    {0x00, 0x01, GCA_BTN_START},
        {0x00, 0x02, GCA_BTN_Z},     {0x00, 0x04, GCA_BTN_R},     {0x00, 0x08, GCA_BTN_L},
        {0xFF, 0x0F, 0x1F7F},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        Block r = resting();
        r.b1 = cases[i].b1;
        r.b2 = cases[i].b2;
        Block blocks[4] = {r, kEmpty, kEmpty, kEmpty};
        assert(feed(&s, blocks) == 0);
        assert(pad_of(&s, 0).buttons == cases[i].want);
    }
    // The pad words the game expects (DolPadState.button / the SI wire): A is 0x0100, Start 0x1000, Z 0x0010.
    assert(GCA_BTN_A == 0x0100 && GCA_BTN_START == 0x1000 && GCA_BTN_Z == 0x0010 && GCA_BTN_L == 0x0040 &&
           GCA_BTN_R == 0x0020 && GCA_BTN_LEFT == 0x0001 && GCA_BTN_UP == 0x0008);
}

static void test_origin(void) {
    GcaState s;
    gca_reset(&s);
    Block rest = resting();
    Block blocks[4] = {kEmpty, kEmpty, rest, kEmpty};
    // A pad plugged into port 3: connect event, origin = that first report.
    assert(feed(&s, blocks) == (int)GCA_EVENT_CONNECT(2));
    assert(gca_port_connected(&s, 2) && !gca_port_connected(&s, 0));
    GcaPad pad = pad_of(&s, 2);
    // At rest the game gets zeros, though the raw bytes are 0x7C/0x86 and so on.
    assert(pad.stick_x == 0 && pad.stick_y == 0 && pad.c_x == 0 && pad.c_y == 0);
    assert(pad.trigger_l == 0 && pad.trigger_r == 0 && pad.buttons == 0);

    // Stick tilted right and up, C-stick left, L half pressed, A held.
    blocks[2].sx = 0xFF;
    blocks[2].sy = 0xB6; // 0x86 + 0x30
    blocks[2].cx = 0x01;
    blocks[2].cy = 0x7E;
    blocks[2].l = 0x9C; // 0x1C + 0x80
    blocks[2].r = 0x10; // below its origin: reads as 0
    blocks[2].b1 = 0x01;
    assert(feed(&s, blocks) == 0);
    pad = pad_of(&s, 2);
    assert(pad.stick_x == 127);
    assert(pad.stick_y == 0x30);
    assert(pad.c_x == 0x01 - 0x81);
    assert(pad.c_y == 0);
    assert(pad.trigger_l == 0x80);
    assert(pad.trigger_r == 0);
    assert(pad.buttons == GCA_BTN_A);

    // The other end: the stick fully left and down.
    blocks[2].sx = 0x00;
    blocks[2].sy = 0x00;
    feed(&s, blocks);
    pad = pad_of(&s, 2);
    assert(pad.stick_x == -124 && pad.stick_y == -128 + 0); // 0x00 - 0x7C, 0x00 - 0x86 clamps at -128
    assert(pad.stick_y == -128);

    // The origin does not move while the pad stays connected, even if the stick rests elsewhere later.
    blocks[2].sx = 0x90;
    feed(&s, blocks);
    assert(s.port[2].origin[0] == 0x7C);

    // Unplug, plug back in with the stick held off centre: the new origin is the new first report
    // (this is the X+Y+Start recalibration, which makes the adapter report the port empty and then full again).
    Block gone[4] = {kEmpty, kEmpty, kEmpty, kEmpty};
    assert(feed(&s, gone) == (int)GCA_EVENT_DISCONNECT(2));
    assert(!gca_port_connected(&s, 2));
    GcaPad none;
    assert(!gca_port_pad(&s, 2, NULL, &none) && none.buttons == 0 && none.stick_x == 0);
    Block again = resting();
    again.sx = 0x88;
    Block back[4] = {kEmpty, kEmpty, again, kEmpty};
    assert(feed(&s, back) == (int)GCA_EVENT_CONNECT(2));
    assert(s.port[2].origin[0] == 0x88);
    assert(pad_of(&s, 2).stick_x == 0);
}

static void test_zero_first_report(void) {
    // The adapter reports a port as present before it has read the pad: the first report has the type bits and
    // every stick byte zero. That is not an origin; the next report with real values is.
    GcaState s;
    gca_reset(&s);
    Block pending = {0x14, 0, 0, 0, 0, 0, 0, 0, 0};
    Block blocks[4] = {pending, kEmpty, kEmpty, kEmpty};
    assert(feed(&s, blocks) == (int)GCA_EVENT_CONNECT(0));
    assert(!s.port[0].have_origin);
    GcaPad pad = pad_of(&s, 0);
    assert(pad.stick_x == 0 && pad.stick_y == 0 && pad.trigger_l == 0); // resting, not the raw zeros
    blocks[0] = resting();
    assert(feed(&s, blocks) == 0);
    assert(s.port[0].have_origin && s.port[0].origin[0] == 0x7C);
    assert(pad_of(&s, 0).stick_x == 0);
}

static void test_deadzone(void) {
    GcaState s;
    gca_reset(&s);
    Block blocks[4] = {resting(), kEmpty, kEmpty, kEmpty};
    feed(&s, blocks);
    blocks[0].sx = 0x7C + 3;
    blocks[0].sy = 0x86 - 3;
    feed(&s, blocks);
    GcaPad pad;
    GcaConfig config = {0};
    gca_port_pad(&s, 0, &config, &pad);
    assert(pad.stick_x == 3 && pad.stick_y == -3); // no dead zone by default: what a GameCube sends
    config.stick_deadzone = 3;
    gca_port_pad(&s, 0, &config, &pad);
    assert(pad.stick_x == 0 && pad.stick_y == 0);
    config.stick_deadzone = 2;
    gca_port_pad(&s, 0, &config, &pad);
    assert(pad.stick_x == 3 && pad.stick_y == -3);
}

static void test_rumble_power(void) {
    GcaState s;
    gca_reset(&s);
    Block powered = resting();                         // 0x14: wired + power
    Block unpowered = resting();
    unpowered.status = 0x10;                           // wired, the grey cable is out
    Block wave = resting();
    wave.status = 0x20;                                // WaveBird: no motor
    Block wave_powered = resting();
    wave_powered.status = 0x24;                        // power bit with a WaveBird: still no motor
    Block blocks[4] = {powered, unpowered, wave, wave_powered};
    assert(feed(&s, blocks) == (int)(GCA_EVENT_CONNECT(0) | GCA_EVENT_CONNECT(1) | GCA_EVENT_CONNECT(2) |
                                     GCA_EVENT_CONNECT(3)));
    assert(s.port[2].type == GCA_WIRELESS && s.port[3].type == GCA_WIRELESS);
    assert(gca_port_rumble_allowed(&s, 0));
    assert(!gca_port_rumble_allowed(&s, 1));
    assert(!gca_port_rumble_allowed(&s, 2));
    assert(!gca_port_rumble_allowed(&s, 3));

    const bool all[4] = {true, true, true, true};
    const bool none[4] = {false, false, false, false};
    uint8_t out[GCA_RUMBLE_SIZE];
    gca_build_rumble(&s, all, out);
    const uint8_t expect_all[5] = {0x11, 1, 0, 0, 0};
    assert(memcmp(out, expect_all, 5) == 0);
    gca_build_rumble(&s, none, out);
    const uint8_t expect_none[5] = {0x11, 0, 0, 0, 0};
    assert(memcmp(out, expect_none, 5) == 0);

    // The cable is plugged in while the controller is connected: rumble follows the power bit.
    blocks[1].status = 0x14;
    assert(feed(&s, blocks) == 0);
    assert(gca_port_rumble_allowed(&s, 1));
    const bool second[4] = {false, true, false, false};
    gca_build_rumble(&s, second, out);
    const uint8_t expect_second[5] = {0x11, 0, 1, 0, 0};
    assert(memcmp(out, expect_second, 5) == 0);

    // A port nobody is on never gets a 1.
    blocks[1] = kEmpty;
    feed(&s, blocks);
    gca_build_rumble(&s, all, out);
    assert(out[2] == 0);
}

static void test_bad_reports(void) {
    GcaState s;
    gca_reset(&s);
    Block blocks[4] = {resting(), kEmpty, kEmpty, kEmpty};
    feed(&s, blocks);
    const GcaState before = s;
    uint8_t report[GCA_REPORT_SIZE];
    make_report(report, (Block[4]){kEmpty, kEmpty, kEmpty, kEmpty});
    assert(gca_parse_report(&s, report, GCA_REPORT_SIZE - 1) == -1); // short
    report[0] = 0x22;
    assert(gca_parse_report(&s, report, GCA_REPORT_SIZE) == -1);     // not a pad report
    assert(gca_parse_report(&s, NULL, GCA_REPORT_SIZE) == -1);
    assert(s.bad_reports == before.bad_reports + 3);
    assert(gca_port_connected(&s, 0)); // an unplug is only a good report that says so
    // A longer read (a 64-byte buffer) with a good report at the front is accepted.
    uint8_t big[64] = {0};
    big[0] = GCA_REPORT_ID;
    assert(gca_parse_report(&s, big, sizeof big) == (int)GCA_EVENT_DISCONNECT(0));
}

static void test_port_mapping(void) {
    GcaState s;
    gca_reset(&s);
    int channel[4];
    Block only3[4] = {kEmpty, kEmpty, resting(), kEmpty};
    feed(&s, only3);
    gca_map_ports(&s, false, channel);
    assert(channel[0] == 0 && channel[1] == 1 && channel[2] == 2 && channel[3] == 3);
    gca_map_ports(&s, true, channel);
    assert(channel[2] == 0); // the first connected port is player 1
    assert(channel[0] == 0 && channel[1] == 1 && channel[3] == 3);

    // A pad in port 1 as well: port 1 is player 1 by itself, nothing moves.
    Block both[4] = {resting(), kEmpty, resting(), kEmpty};
    feed(&s, both);
    gca_map_ports(&s, true, channel);
    assert(channel[0] == 0 && channel[2] == 2);

    // Ports 2 and 4 only: the first of them is player 1.
    Block two_four[4] = {kEmpty, resting(), kEmpty, resting()};
    feed(&s, two_four);
    gca_map_ports(&s, true, channel);
    assert(channel[1] == 0 && channel[3] == 3);

    Block nothing[4] = {kEmpty, kEmpty, kEmpty, kEmpty};
    feed(&s, nothing);
    gca_map_ports(&s, true, channel);
    assert(channel[0] == 0 && channel[1] == 1 && channel[2] == 2 && channel[3] == 3);
}

// A whole session as the adapter would send it: it starts empty, a pad arrives (zeros first), is played, a second
// pad arrives, the first is unplugged. Checked report by report.
static void test_session(void) {
    GcaState s;
    gca_reset(&s);
    Block z = {0x14, 0, 0, 0, 0, 0, 0, 0, 0};
    const Block e4[4] = {kEmpty, kEmpty, kEmpty, kEmpty};
    struct {
        Block b[4];
        int events;
        bool have0;
        int8_t x0;
        uint16_t buttons0;
    } steps[] = {
        {{kEmpty, kEmpty, kEmpty, kEmpty}, 0, false, 0, 0},
        {{z, kEmpty, kEmpty, kEmpty}, (int)GCA_EVENT_CONNECT(0), false, 0, 0},
        {{{0x14, 0, 0, 0x80, 0x80, 0x80, 0x80, 0x20, 0x20}, kEmpty, kEmpty, kEmpty}, 0, true, 0, 0},
        {{{0x14, 0x01, 0, 0xA0, 0x80, 0x80, 0x80, 0x20, 0x20}, kEmpty, kEmpty, kEmpty}, 0, true, 0x20, GCA_BTN_A},
        {{{0x14, 0x01, 0x01, 0x60, 0x80, 0x80, 0x80, 0x20, 0x20}, kEmpty, {0x10, 0, 0, 0x7F, 0x80, 0x7F, 0x80, 0, 0},
          kEmpty},
         (int)GCA_EVENT_CONNECT(2), true, -0x20, GCA_BTN_A | GCA_BTN_START},
        {{kEmpty, kEmpty, {0x10, 0, 0, 0x7F, 0x80, 0x7F, 0x80, 0, 0}, kEmpty}, (int)GCA_EVENT_DISCONNECT(0), false, 0, 0},
    };
    (void)e4;
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        assert(feed(&s, steps[i].b) == steps[i].events);
        assert(gca_port_connected(&s, 0) == (i >= 1 && i <= 4));
        if (i >= 2 && i <= 4) {
            const GcaPad pad = pad_of(&s, 0);
            assert(pad.stick_x == steps[i].x0);
            assert(pad.buttons == steps[i].buttons0);
        }
        assert(s.port[0].have_origin == steps[i].have0);
    }
    assert(s.reports == sizeof steps / sizeof steps[0] && s.bad_reports == 0);
}

static void test_replay_file(void) {
    // Entries are written the way a replay file is: hex, with spaces and comments around.
    char line1[256], line2[256], line3[256];
    const Block first = {0x14, 0, 0, 0x80, 0x80, 0x80, 0x80, 0x20, 0x20};
    const Block pressed = {0x14, 0x01, 0, 0xC0, 0x80, 0x80, 0x80, 0x20, 0x20};
    const Block plain = {0x10, 0, 0, 0x80, 0x80, 0x80, 0x80, 0, 0};
    const Block rows[3][4] = {{first, kEmpty, kEmpty, kEmpty}, {pressed, kEmpty, kEmpty, kEmpty},
                              {kEmpty, plain, kEmpty, kEmpty}};
    char* lines[3] = {line1, line2, line3};
    for (int r = 0; r < 3; r++) {
        uint8_t report[GCA_REPORT_SIZE];
        make_report(report, rows[r]);
        size_t at = 0;
        for (int b = 0; b < GCA_REPORT_SIZE; b++)
            at += (size_t)snprintf(lines[r] + at, 256 - at, "%02X%s", report[b], b % 9 == 0 ? "  " : " ");
    }
    char text[1024];
    snprintf(text, sizeof text,
             "# a comment\n10 %s\n\n  40 %s   # A and the stick right\n40 unplug\n90 %s\n", line1, line2, line3);
    GcaReplayEntry entries[8];
    char error[96];
    const int n = gca_replay_parse(text, entries, 8, error, sizeof error);
    if (n != 4)
        printf("replay parse: %d (%s)\n", n, error);
    assert(n == 4);
    assert(entries[0].retrace == 10 && !entries[0].unplug && entries[0].report[0] == 0x21 && entries[0].report[1] == 0x14);
    assert(entries[1].retrace == 40 && entries[1].report[2] == 0x01 && entries[1].report[4] == 0xC0);
    assert(entries[2].retrace == 40 && entries[2].unplug);
    assert(entries[3].retrace == 90 && entries[3].report[10] == 0x10);
    assert(gca_replay_index(entries, 4, 0) == -1);
    assert(gca_replay_index(entries, 4, 9) == -1);
    assert(gca_replay_index(entries, 4, 10) == 0);
    assert(gca_replay_index(entries, 4, 39) == 0);
    assert(gca_replay_index(entries, 4, 40) == 2); // the last entry at or before it
    assert(gca_replay_index(entries, 4, 89) == 2);
    assert(gca_replay_index(entries, 4, 100000) == 3);

    // Refused files say why.
    assert(gca_replay_parse("5 21 14\n", entries, 8, error, sizeof error) == -1 && strstr(error, "line 1") != NULL);
    assert(gca_replay_parse("zz\n", entries, 8, error, sizeof error) == -1);
    assert(gca_replay_parse("10 unplug\n5 unplug\n", entries, 8, error, sizeof error) == -1 &&
           strstr(error, "decrease") != NULL);
    assert(gca_replay_parse("1 unplug\n2 unplug\n", entries, 1, error, sizeof error) == -1);
    assert(gca_replay_parse("1 2114 xx\n", entries, 8, error, sizeof error) == -1);
    assert(gca_replay_parse("", entries, 8, error, sizeof error) == 0);

    // Run the recorded entries through the parser the way gc_adapter.c does.
    assert(gca_replay_parse(text, entries, 8, error, sizeof error) == 4); // the refusals above reused the array
    GcaState s;
    gca_reset(&s);
    assert(gca_parse_report(&s, entries[0].report, GCA_REPORT_SIZE) == (int)GCA_EVENT_CONNECT(0));
    assert(gca_parse_report(&s, entries[1].report, GCA_REPORT_SIZE) == 0);
    GcaPad pad = pad_of(&s, 0);
    assert(pad.buttons == GCA_BTN_A && pad.stick_x == 0x40 && pad.stick_y == 0);
}

int main(void) {
    test_layout();
    test_origin();
    test_zero_first_report();
    test_deadzone();
    test_rumble_power();
    test_bad_reports();
    test_port_mapping();
    test_session();
    test_replay_file();
    puts("gc_adapter_proto_test: ok");
    return 0;
}
