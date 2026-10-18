// The online play connection (Network > Anchor): one WebSocket to the room
// server (server/anchor, PROTOCOL.md), on its own thread with Windows' WinHTTP
// WebSocket client. Messages are JSON text; the game side sends and polls.
#pragma once

#include <string>
#include <vector>

namespace anchor {

enum class Status { Disconnected, Connecting, Connected, Failed };

struct Options {
    std::string host = "wwserver.viosarcade.xyz";
    unsigned short port = 443;
    bool secure = true;
    std::string room;
};

// Starts (or restarts) the connection; it reconnects by itself until Stop.
void Start(const Options& options);
void Stop();
Status GetStatus();
std::string LastError();

// Queues a message (one JSON object) to send.
void Send(const std::string& json);
// Takes the messages received since the last call.
std::vector<std::string> Poll();

} // namespace anchor
