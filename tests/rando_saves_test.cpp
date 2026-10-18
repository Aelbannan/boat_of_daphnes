// Randomizer saves (windows/src/rando_saves.cpp): the sidecar, the seed per file and the outdated-save guard.
#include "rando_saves.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

static void write(const fs::path& p, const char* text) { std::ofstream(p, std::ios::binary) << text; }

int main() {
    const fs::path dir = fs::temp_directory_path() / "rando_saves_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path card = dir / "Randomizer.card", side = dir / "Randomizer.slots.json";

    BlueWakeRandoPack pack;
    std::memset(&pack, 0, sizeof pack);
    std::snprintf(pack.seed, sizeof pack.seed, "SEEDA");
    std::snprintf(pack.seed_hash, sizeof pack.seed_hash, "Din Sam");
    std::snprintf(pack.permalink, sizeof pack.permalink, "LINK");
    std::snprintf(pack.wwrando_commit, sizeof pack.wwrando_commit, "86b97db");

    randosaves::Sidecar s;
    assert(randosaves::Assign(&s, 2, pack));
    assert(!randosaves::Assign(&s, 2, pack));  // a file keeps its seed
    assert(!randosaves::Assign(&s, 0, pack) && !randosaves::Assign(&s, 4, pack));
    std::snprintf(pack.seed, sizeof pack.seed, "SEEDB");
    assert(randosaves::Assign(&s, 3, pack));
    assert(randosaves::SeedOfSlot(s, 2) == "SEEDA" && randosaves::SeedOfSlot(s, 3) == "SEEDB" && randosaves::SeedOfSlot(s, 1).empty());
    // a new file replaces whatever its slot held; a vanilla file keeps no entry
    std::snprintf(pack.seed, sizeof pack.seed, "SEEDC");
    randosaves::Set(&s, 2, pack);
    assert(randosaves::SeedOfSlot(s, 2) == "SEEDC");
    randosaves::Set(&s, 5, pack);  // no such slot: nothing
    assert(s.slots.size() == 2);
    assert(randosaves::Erase(&s, 2) && !randosaves::Erase(&s, 2) && randosaves::SeedOfSlot(s, 2).empty());
    std::snprintf(pack.seed, sizeof pack.seed, "SEEDA");
    assert(randosaves::Assign(&s, 2, pack));
    assert(randosaves::Save(side, s));
    randosaves::Sidecar back;
    assert(randosaves::Load(side, &back) && back.slots.size() == 2);
    assert(back.slots[2].hash == "Din Sam" && back.slots[2].build == randosaves::kBuild && back.slots[2].identity.size() == 16);
    assert(!fs::exists(side.string() + ".tmp"));

    // no sidecar, or one of this build: nothing happens
    write(card, "CARD");
    assert(!randosaves::Guard(card, dir / "none.json", randosaves::kBuild, 1000).renamed);
    assert(!randosaves::Guard(card, side, randosaves::kBuild, 1000).renamed && fs::exists(card));

    // another build: the card and the sidecar go aside, with a message, and are not deleted
    const randosaves::GuardResult g = randosaves::Guard(card, side, "save2/other", 1234);
    assert(g.renamed && !fs::exists(card) && !fs::exists(side));
    assert(g.backup.filename() == "Randomizer-1234.bak" && fs::exists(g.backup));
    assert(fs::exists(dir / "Randomizer.slots-1234.bak"));
    assert(g.message.find("Outdated Randomizer Save") == 0);
    std::ifstream in(g.backup, std::ios::binary);
    char buf[8] = {0};
    in.read(buf, 4);
    assert(std::strcmp(buf, "CARD") == 0);  // the save is intact
    in.close();  // Windows will not delete a file that is open
    fs::remove_all(dir);
    std::puts("rando_saves_test: ok");
    return 0;
}
