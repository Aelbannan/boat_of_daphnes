#include "gc_adapter_proto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void gca_reset(GcaState* state) { memset(state, 0, sizeof *state); }

static bool axes_all_zero(const uint8_t raw[6]) {
    // Stick X/Y and C-stick X/Y. A controller at rest reads near 128, never 0: all four at zero is the adapter
    // reporting a port it has not read yet.
    return raw[0] == 0 && raw[1] == 0 && raw[2] == 0 && raw[3] == 0;
}

static uint16_t decode_buttons(uint8_t b1, uint8_t b2) {
    uint16_t buttons = 0;
    if (b1 & 0x01) buttons |= GCA_BTN_A;
    if (b1 & 0x02) buttons |= GCA_BTN_B;
    if (b1 & 0x04) buttons |= GCA_BTN_X;
    if (b1 & 0x08) buttons |= GCA_BTN_Y;
    if (b1 & 0x10) buttons |= GCA_BTN_LEFT;
    if (b1 & 0x20) buttons |= GCA_BTN_RIGHT;
    if (b1 & 0x40) buttons |= GCA_BTN_DOWN;
    if (b1 & 0x80) buttons |= GCA_BTN_UP;
    if (b2 & 0x01) buttons |= GCA_BTN_START;
    if (b2 & 0x02) buttons |= GCA_BTN_Z;
    if (b2 & 0x04) buttons |= GCA_BTN_R;
    if (b2 & 0x08) buttons |= GCA_BTN_L;
    return buttons;
}

int gca_parse_report(GcaState* state, const uint8_t* data, size_t size) {
    if (data == NULL || size < GCA_REPORT_SIZE || data[0] != GCA_REPORT_ID) {
        state->bad_reports++;
        return -1;
    }
    int events = 0;
    for (int p = 0; p < GCA_PORTS; p++) {
        const uint8_t* block = data + 1 + 9 * p;
        GcaPort* port = &state->port[p];
        const uint8_t status = block[0];
        const GcaType type = (status & 0x10) ? GCA_WIRED : (status & 0x20) ? GCA_WIRELESS : GCA_NONE;
        if (type == GCA_NONE) {
            if (port->type != GCA_NONE)
                events |= (int)GCA_EVENT_DISCONNECT(p);
            memset(port, 0, sizeof *port);
            continue;
        }
        if (port->type == GCA_NONE) {
            events |= (int)GCA_EVENT_CONNECT(p);
            memset(port, 0, sizeof *port);
        }
        port->type = type;
        port->powered = type == GCA_WIRED && (status & 0x04) != 0;
        port->buttons = decode_buttons(block[1], block[2]);
        for (int i = 0; i < 6; i++)
            port->raw[i] = block[3 + i];
        if (!port->have_origin && !axes_all_zero(port->raw)) {
            memcpy(port->origin, port->raw, sizeof port->origin);
            port->have_origin = true;
        }
    }
    state->reports++;
    return events;
}

bool gca_port_connected(const GcaState* state, int port) {
    return port >= 0 && port < GCA_PORTS && state->port[port].type != GCA_NONE;
}

bool gca_port_rumble_allowed(const GcaState* state, int port) {
    return gca_port_connected(state, port) && state->port[port].type == GCA_WIRED && state->port[port].powered;
}

static int8_t centred(uint8_t raw, uint8_t origin, uint8_t deadzone) {
    int d = (int)raw - (int)origin;
    if (d < 0 ? -d <= (int)deadzone : d <= (int)deadzone)
        return 0;
    return (int8_t)(d < -128 ? -128 : d > 127 ? 127 : d);
}

static uint8_t above(uint8_t raw, uint8_t origin) { return raw > origin ? (uint8_t)(raw - origin) : 0u; }

bool gca_port_pad(const GcaState* state, int port, const GcaConfig* config, GcaPad* out) {
    memset(out, 0, sizeof *out);
    if (!gca_port_connected(state, port))
        return false;
    const GcaPort* p = &state->port[port];
    const uint8_t dz = config != NULL ? config->stick_deadzone : 0u;
    out->buttons = p->buttons;
    if (p->have_origin) {
        out->stick_x = centred(p->raw[0], p->origin[0], dz);
        out->stick_y = centred(p->raw[1], p->origin[1], dz);
        out->c_x = centred(p->raw[2], p->origin[2], dz);
        out->c_y = centred(p->raw[3], p->origin[3], dz);
        out->trigger_l = above(p->raw[4], p->origin[4]);
        out->trigger_r = above(p->raw[5], p->origin[5]);
    }
    return true;
}

void gca_build_rumble(const GcaState* state, const bool want[GCA_PORTS], uint8_t out[GCA_RUMBLE_SIZE]) {
    out[0] = GCA_CMD_RUMBLE;
    for (int p = 0; p < GCA_PORTS; p++)
        out[1 + p] = want[p] && gca_port_rumble_allowed(state, p) ? 1u : 0u;
}

void gca_map_ports(const GcaState* state, bool first_is_p1, int channel[GCA_PORTS]) {
    for (int p = 0; p < GCA_PORTS; p++)
        channel[p] = p;
    if (!first_is_p1 || gca_port_connected(state, 0))
        return;
    for (int p = 1; p < GCA_PORTS; p++)
        if (gca_port_connected(state, p)) {
            channel[p] = 0;
            return;
        }
}

// --- replay files -----------------------------------------------------------------------------------------

static void set_error(char* error, size_t error_size, int line, const char* what) {
    if (error != NULL && error_size != 0)
        snprintf(error, error_size, "line %d: %s", line, what);
}

int gca_replay_parse(const char* text, GcaReplayEntry* out, size_t max, char* error, size_t error_size) {
    size_t count = 0;
    int line_no = 0;
    uint64_t last_retrace = 0;
    const char* at = text;
    if (error != NULL && error_size != 0)
        error[0] = '\0';
    while (at != NULL && *at != '\0') {
        const char* eol = strchr(at, '\n');
        const size_t length = eol != NULL ? (size_t)(eol - at) : strlen(at);
        char line[512];
        line_no++;
        const char* next = eol != NULL ? eol + 1 : NULL;
        if (length >= sizeof line) {
            set_error(error, error_size, line_no, "line too long");
            return -1;
        }
        memcpy(line, at, length);
        line[length] = '\0';
        at = next;
        char* hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';
        char* p = line;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '\0')
            continue;
        char* end = NULL;
        const unsigned long long retrace = strtoull(p, &end, 10);
        if (end == p) {
            set_error(error, error_size, line_no, "expected a retrace number");
            return -1;
        }
        if (count != 0 && retrace < last_retrace) {
            set_error(error, error_size, line_no, "retraces must not decrease");
            return -1;
        }
        if (count >= max) {
            set_error(error, error_size, line_no, "too many entries");
            return -1;
        }
        p = end;
        while (isspace((unsigned char)*p))
            p++;
        GcaReplayEntry* entry = &out[count];
        memset(entry, 0, sizeof *entry);
        entry->retrace = retrace;
        if (strncmp(p, "unplug", 6) == 0) {
            entry->unplug = true;
        } else {
            size_t digits = 0;
            for (; *p != '\0'; p++) {
                if (isspace((unsigned char)*p))
                    continue;
                if (!isxdigit((unsigned char)*p)) {
                    set_error(error, error_size, line_no, "not a hex digit");
                    return -1;
                }
                if (digits >= 2u * GCA_REPORT_SIZE) {
                    set_error(error, error_size, line_no, "more than 37 bytes");
                    return -1;
                }
                const int v = isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10);
                if ((digits & 1u) == 0)
                    entry->report[digits / 2] = (uint8_t)(v << 4);
                else
                    entry->report[digits / 2] |= (uint8_t)v;
                digits++;
            }
            if (digits != 2u * GCA_REPORT_SIZE) {
                set_error(error, error_size, line_no, "a report is 37 bytes (74 hex digits)");
                return -1;
            }
        }
        last_retrace = retrace;
        count++;
    }
    return (int)count;
}

int gca_replay_index(const GcaReplayEntry* entries, size_t count, uint64_t retrace) {
    int found = -1;
    for (size_t i = 0; i < count && entries[i].retrace <= retrace; i++)
        found = (int)i;
    return found;
}
