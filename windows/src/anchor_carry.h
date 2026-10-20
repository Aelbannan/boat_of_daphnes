// Anchor's side of carrying players (runtime/host/src/puppet_carry.c): CARRY_START, CARRY_POS and CARRY_END between the
// players, and the pose fix that shows a player I carry at my hands. The protocol is in server/anchor/PROTOCOL.md.
#pragma once

#define PICOJSON_USE_INT64
#include "picojson.h"

#include <functional>
#include <string>
#include <vector>

namespace online {
namespace carry {

// What the Anchor client lends: sending, and what it knows of the other players.
struct Env {
    std::function<void(picojson::object)> send;
    std::function<bool(int)> in_my_scene;        // the player is online in my stage and room
    std::function<bool(int)> hurt_allowed;       // PvP is on and the player may hit me (teams, friendly fire)
    std::function<std::string(int)> name;
    std::function<void(const std::string&, const std::string&)> toast;
    std::function<void(int from, const float pos[3])> hit;  // a thrown player's landing hurts (PvP on)
};

// The room's setting (roomState allowCarrying), every frame while connected.
void set_room_allowed(bool allowed);
// A message from another player. True if it was one of the carry types.
bool handle(const std::string& type, const picojson::object& message, int from, const Env& env);
// Every frame while connected: the host's events out, the players' presence in.
void frame(bool connected, const Env& env);
// A PLAYER_UPDATE's pose bytes from a player I carry: the base position is made my hands'.
void fix_pose(int from, std::vector<unsigned char>& wire);
// A player went away (offline or elsewhere), or the connection closed.
void player_gone(int id);
void reset();

} // namespace carry
} // namespace online
