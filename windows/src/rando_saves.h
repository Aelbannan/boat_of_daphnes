// Randomizer saves (R5): a card of their own, a sidecar saying which seed each file belongs to, and the guard that
// puts an outdated card aside. wwrando's game (GZLE99) repurposes event bits, so a randomizer file must never be read by
// the vanilla game or by another seed; SoH keeps the same facts in the save's own metadata (docs/fork/research/soh-randomizer.md 2.3).
//
//   Save\GZLE01.card              the card: three files, each a vanilla or a randomizer file
//   Save\Randomizer.slots.json    per randomizer file: the seed's name, its hash (two names), its identity and the build
//                                 that made it. A file with no entry is a vanilla file (SoH keeps this in the save itself).
#pragma once

#include "rando_pack.h"

#include <filesystem>
#include <map>
#include <string>

namespace randosaves {

// What makes a randomizer save readable: the save format of this code and the wwrando release whose item ids, flags and
// custom symbols it holds. A change of either puts old cards aside.
extern const char* const kBuild;

struct Slot {
    std::string seed;      // the seed's name (its folder in Randomizer\)
    std::string hash;      // the two names of the seed hash
    std::string identity;  // randopack::Identity: permalink and generator
    std::string build;     // kBuild when the file was made
};

struct Sidecar {
    std::map<int, Slot> slots;  // 1 to 3
};

bool Load(const std::filesystem::path& path, Sidecar* out);
bool Save(const std::filesystem::path& path, const Sidecar& sidecar);

// The file a seed was first played on gets the seed; a file that has one keeps it. True if the sidecar changed.
bool Assign(Sidecar* sidecar, int slot, const BlueWakeRandoPack& pack);
// A new randomizer file replaces whatever the slot held before (a vanilla file that was erased and made again).
void Set(Sidecar* sidecar, int slot, const BlueWakeRandoPack& pack);
// A vanilla file (or an empty slot): the sidecar keeps nothing for it. True if it held an entry.
bool Erase(Sidecar* sidecar, int slot);

struct GuardResult {
    bool renamed = false;
    std::filesystem::path backup;  // where the card went
    std::string message;           // what to tell the player ("" if nothing)
};
// At launch, before the card is opened: if any file in the sidecar was made by another build, the card and the sidecar
// are renamed to <name>-<unixtime>.bak (never deleted) and the player is told, as SoH does with an outdated save.
GuardResult Guard(const std::filesystem::path& card, const std::filesystem::path& sidecar, const std::string& build,
                  std::time_t now);

// What a file belongs to, for the file select: its seed's folder name, "" if the sidecar has no such file.
std::string SeedOfSlot(const Sidecar& sidecar, int slot);

}  // namespace randosaves
