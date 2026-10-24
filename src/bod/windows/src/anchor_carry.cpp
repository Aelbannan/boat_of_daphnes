// Anchor's side of carrying players: see anchor_carry.h.
#include "anchor_carry.h"

#include "puppet_carry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace online {
namespace carry {
namespace {

int get_int(const picojson::object& m, const char* key, int fallback) {
    const auto it = m.find(key);
    return it != m.end() && it->second.is<double>() ? static_cast<int>(std::lround(it->second.get<double>())) : fallback;
}

bool get_pos(const picojson::object& m, float pos[3]) {
    const auto it = m.find("pos");
    if (it == m.end() || !it->second.is<picojson::array>())
        return false;
    const auto& a = it->second.get<picojson::array>();
    if (a.size() != 3)
        return false;
    for (int i = 0; i < 3; ++i) {
        if (!a[i].is<double>())
            return false;
        pos[i] = static_cast<float>(a[i].get<double>());
    }
    return true;
}

picojson::value pos_value(const float pos[3]) {
    return picojson::value(picojson::array{picojson::value(double(pos[0])), picojson::value(double(pos[1])), picojson::value(double(pos[2]))});
}

bool g_allowed;

} // namespace

void set_room_allowed(bool allowed) {
    g_allowed = allowed;
    bluewake_carry_set_room(allowed);
}

bool handle(const std::string& type, const picojson::object& m, int from, const Env& env) {
    if (type != "CARRY_START" && type != "CARRY_POS" && type != "CARRY_END")
        return false;
    float pos[3]{};
    const bool has_pos = get_pos(m, pos);
    const int angle = get_int(m, "angle", 0);
    if (type == "CARRY_START") {
        if (has_pos) {
            std::fprintf(stderr, "[carry] CARRY_START from %s (%d)\n", env.name(from).c_str(), from);
            bluewake_carry_peer_start(from, pos, angle);
            if (g_allowed)
                env.toast(env.name(from), "picked you up (B to break free)");
        }
    } else if (type == "CARRY_POS") {
        if (has_pos) {
            const auto it = m.find("phase");
            const int phase = it != m.end() && it->second.is<std::string>() && it->second.get<std::string>() == "thrown"
                                  ? BLUEWAKE_CARRY_PHASE_THROWN : BLUEWAKE_CARRY_PHASE_HELD;
            bluewake_carry_peer_pos(from, pos, angle, phase);
        }
    } else {
        const auto it = m.find("reason");
        const std::string reason = it != m.end() && it->second.is<std::string>() ? it->second.get<std::string>() : "left";
        const int r = bluewake_carry_reason_from_name(reason.c_str());
        const bool hurt = get_int(m, "hurt", 0) != 0;
        std::fprintf(stderr, "[carry] CARRY_END from %s (%d): %s%s\n", env.name(from).c_str(), from, reason.c_str(), hurt ? ", hurt" : "");
        bluewake_carry_peer_end(from, r, has_pos ? pos : nullptr, angle, hurt);
        if (r == BLUEWAKE_CARRY_REASON_REFUSED)
            env.toast(env.name(from), "cannot be carried now");
        else if (r == BLUEWAKE_CARRY_REASON_BREAKFREE)
            env.toast(env.name(from), "broke free");
    }
    return true;
}

void frame(bool connected, const Env& env) {
    if (!connected) {
        BluewakeCarryEvent drop;
        while (bluewake_carry_take_event(&drop)) {
        }
        return;
    }
    BluewakeCarryEvent ev;
    while (bluewake_carry_take_event(&ev)) {
        picojson::object m;
        m["targetClientId"] = picojson::value(static_cast<int64_t>(ev.peer));
        m["pos"] = pos_value(ev.pos);
        m["angle"] = picojson::value(static_cast<int64_t>(ev.angle));
        if (ev.kind == BLUEWAKE_CARRY_EV_START) {
            m["type"] = picojson::value("CARRY_START");
        } else if (ev.kind == BLUEWAKE_CARRY_EV_POS) {
            m["type"] = picojson::value("CARRY_POS");
            m["phase"] = picojson::value(std::string(ev.phase == BLUEWAKE_CARRY_PHASE_THROWN ? "thrown" : "held"));
        } else {
            m["type"] = picojson::value("CARRY_END");
            m["reason"] = picojson::value(std::string(bluewake_carry_reason_name(ev.reason)));
            m["hurt"] = picojson::value(static_cast<int64_t>(ev.hurt ? 1 : 0));
            std::fprintf(stderr, "[carry] CARRY_END sent to %d: %s\n", ev.peer, bluewake_carry_reason_name(ev.reason));
        }
        env.send(m);
    }
    int peer = 0;
    if (bluewake_carry_state(&peer) != 0 && peer != 0 && !env.in_my_scene(peer)) {
        std::fprintf(stderr, "[carry] player %d is no longer here: the hold ends\n", peer);
        bluewake_carry_peer_gone(peer, BLUEWAKE_CARRY_REASON_AREA);
    }
    int from = 0;
    float at[3];
    if (bluewake_carry_take_hurt(&from, at) && env.hurt_allowed(from))
        env.hit(from, at);
}

void fix_pose(int from, std::vector<unsigned char>& wire) {
    float feet[3];
    if (wire.size() < 18 || !bluewake_carry_puppet_pos(from, feet))
        return;
    std::memcpy(wire.data() + 6, feet, sizeof feet); // the base position, little endian floats (puppet.c wire_encode)
}

void player_gone(int id) { bluewake_carry_peer_gone(id, BLUEWAKE_CARRY_REASON_LEFT); }

void reset() {
    g_allowed = false;
    bluewake_carry_reset();
}

} // namespace carry
} // namespace online
