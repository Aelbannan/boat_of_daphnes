// See rando_saves.h.
#ifdef rename
#undef rename // bw_posix_compat.h's, which <filesystem> must not see
#endif

#include "rando_saves.h"

#define PICOJSON_USE_INT64
#include "picojson.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace randosaves {

namespace fs = std::filesystem;

// save format 1; wwrando 1.10.0 at commit 86b97db (scripts/windows/rando_runtime.py's pin)
const char* const kBuild = "save1/wwrando-1.10.0-86b97db";

bool Load(const fs::path& path, Sidecar* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream text;
    text << in.rdbuf();
    picojson::value v;
    if (!picojson::parse(v, text.str()).empty() || !v.is<picojson::object>() || !v.get("slots").is<picojson::object>())
        return false;
    Sidecar sidecar;
    for (const auto& [key, value] : v.get("slots").get<picojson::object>()) {
        const int slot = std::atoi(key.c_str());
        if (slot < 1 || slot > 3 || !value.is<picojson::object>())
            continue;
        Slot s;
        s.seed = value.get("seed").to_str();
        s.hash = value.get("hash").to_str();
        s.identity = value.get("identity").to_str();
        s.build = value.get("build").to_str();
        sidecar.slots[slot] = s;
    }
    *out = sidecar;
    return true;
}

bool Save(const fs::path& path, const Sidecar& sidecar) {
    picojson::object slots;
    for (const auto& [slot, s] : sidecar.slots) {
        picojson::object o;
        o["seed"] = picojson::value(s.seed);
        o["hash"] = picojson::value(s.hash);
        o["identity"] = picojson::value(s.identity);
        o["build"] = picojson::value(s.build);
        slots[std::to_string(slot)] = picojson::value(o);
    }
    picojson::object root;
    root["version"] = picojson::value(static_cast<int64_t>(1));
    root["slots"] = picojson::value(slots);
    // Written beside and moved in, so a crash never leaves half a sidecar.
    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << picojson::value(root).serialize(true);
        if (!out)
            return false;
    }
    std::error_code ec;
    fs::rename(temp, path, ec);
    return !ec;
}

bool Assign(Sidecar* sidecar, int slot, const BlueWakeRandoPack& pack) {
    if (slot < 1 || slot > 3 || sidecar->slots.count(slot))
        return false;
    Slot s;
    s.seed = pack.seed;
    s.hash = pack.seed_hash;
    s.identity = randopack::Identity(pack);
    s.build = kBuild;
    sidecar->slots[slot] = s;
    return true;
}

void Set(Sidecar* sidecar, int slot, const BlueWakeRandoPack& pack) {
    if (slot < 1 || slot > 3)
        return;
    sidecar->slots.erase(slot);
    Assign(sidecar, slot, pack);
}

bool Erase(Sidecar* sidecar, int slot) { return sidecar->slots.erase(slot) != 0; }

GuardResult Guard(const fs::path& card, const fs::path& sidecar_path, const std::string& build, std::time_t now) {
    GuardResult result;
    Sidecar sidecar;
    if (!Load(sidecar_path, &sidecar))
        return result;
    bool outdated = false;
    for (const auto& [slot, s] : sidecar.slots)
        if (s.build != build)
            outdated = true;
    if (!outdated)
        return result;
    const std::string stamp = std::to_string(static_cast<long long>(now));
    std::error_code ec;
    fs::path backup = card;
    backup.replace_extension("");
    backup = backup.string() + "-" + stamp + ".bak";
    if (fs::exists(card, ec))
        fs::rename(card, backup, ec);
    if (ec)
        return result;  // could not move it aside: leave everything as it is rather than lose a save
    fs::path side_backup = sidecar_path;
    side_backup.replace_extension("");
    fs::rename(sidecar_path, side_backup.string() + "-" + stamp + ".bak", ec);
    result.renamed = true;
    result.backup = backup;
    result.message = "Outdated Randomizer Save\n\nThe randomizer saves were made by an older version of the game and "
                     "cannot be loaded. They were renamed to " + backup.filename().string() +
                     " (nothing was deleted); a new randomizer file starts clean.";
    return result;
}

std::string SeedOfSlot(const Sidecar& sidecar, int slot) {
    const auto it = sidecar.slots.find(slot);
    return it == sidecar.slots.end() ? "" : it->second.seed;
}

}  // namespace randosaves
