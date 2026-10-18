// Custom player content shared through the online play server (docs/fork/research/anchor-puppets.md,
// "Custom player content"): the player's active custom Link (Link.arc, voice, equipment) is hashed, uploaded
// to the server if it lacks it, and advertised by its manifest hash in the client state; what other players
// advertise is downloaded in the background into cache\anchor-content\<sha256>, checked, and handed to their
// puppets. Nothing downloaded is ever run: Link.arc is parsed as a RARC and its body model is data for the
// game's model loader.
//
// Used from the app's thread each frame; the network and hashing run on a worker thread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace content {

// A peer's body model, downloaded and checked.
struct Ready {
    int id = 0;
    std::string hash; // the manifest's
    std::vector<uint8_t> body; // cl.bdl
};

// Each frame: the server's HTTP base ("https://host" or "http://host:port"), whether sharing is on, and
// whether the connection is up. Starts the work that is due.
void Tick(const std::string& http_base, bool share, bool connected);
// The manifest hash of the player's own content, "" for none (not sharing, or nothing custom, or still
// uploading); and whether it changed since the last call (the client state is to be sent again).
std::string LocalHash();
bool LocalChanged();
// A peer advertised this manifest ("" for none).
void Want(int id, const std::string& hash);
void Forget(int id);
// Body models that finished downloading since the last call.
std::vector<Ready> TakeReady();
// Peers whose body model is not to be used any longer (their content went, or sharing was turned off).
std::vector<int> TakeCleared();

// At quit: stops the worker thread (it must not be joinable when the process exits).
void Shutdown();

// SHA-256 as lower case hex.
std::string Sha256Hex(const uint8_t* data, size_t size);

} // namespace content
