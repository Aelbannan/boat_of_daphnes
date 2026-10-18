// See rando_pack.h.
#ifdef rename
#undef rename // bw_posix_compat.h's, which <filesystem> must not see
#endif

#include "rando_pack.h"

#define PICOJSON_USE_INT64
#include "picojson.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

namespace randopack {
namespace {

namespace fs = std::filesystem;

template <size_t N>
void copy_text(char (&dst)[N], const std::string& src) {
    std::snprintf(dst, N, "%s", src.c_str());
}

std::string utf8(const fs::path& p) {
    const auto u = p.u8string();  // char8_t in C++20
    return std::string(u.begin(), u.end());
}

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr)
        *error = message;
    return false;
}

int64_t integer(const picojson::value& v, int64_t fallback) {
    if (v.is<int64_t>())
        return v.get<int64_t>();
    if (v.is<double>())
        return static_cast<int64_t>(v.get<double>());
    return fallback;
}

// An option's value as the host compares it: booleans as "true"/"false", numbers in decimal, text as is.
std::string option_text(const picojson::value& v) {
    if (v.is<bool>())
        return v.get<bool>() ? "true" : "false";
    if (v.is<int64_t>())
        return std::to_string(v.get<int64_t>());
    if (v.is<double>())
        return std::to_string(static_cast<int64_t>(v.get<double>()));
    if (v.is<std::string>())
        return v.get<std::string>();
    return "";
}

}  // namespace

bool Parse(const std::string& json_text, BlueWakeRandoPack* out, std::string* error) {
    picojson::value root;
    const std::string problem = picojson::parse(root, json_text);
    if (!problem.empty() || !root.is<picojson::object>())
        return fail(error, "not a JSON object: " + problem);
    if (!rando_seedpack_schema(root.get("schema").to_str()) || integer(root.get("schema_version"), 0) != 1)
        return fail(error, "not a seed pack (schema bod.seedpack, version 1)");

    BlueWakeRandoPack pack;
    std::memset(&pack, 0, sizeof pack);
    copy_text(pack.seed, root.get("seed").to_str());
    copy_text(pack.seed_hash, root.get("seed_hash").to_str());
    copy_text(pack.permalink, root.get("permalink").to_str());
    if (root.get("generator").is<picojson::object>())
        copy_text(pack.wwrando_commit, root.get("generator").get("wwrando_commit").to_str());
    if (pack.seed[0] == '\0' || pack.permalink[0] == '\0')
        return fail(error, "the pack has no seed or permalink");

    // options
    if (!root.get("options").is<picojson::object>())
        return fail(error, "the pack has no options");
    for (const auto& [name, value] : root.get("options").get<picojson::object>()) {
        if (pack.num_options >= BLUEWAKE_RANDO_MAX_OPTIONS)
            return fail(error, "more options than the host holds");
        if (name.size() >= sizeof pack.options[0].name)
            continue;
        copy_text(pack.options[pack.num_options].name, name);
        copy_text(pack.options[pack.num_options].value, option_text(value));
        pack.num_options++;
    }

    // locations: id is the index; item_id is null for an Ice Trap chest (it keeps the vanilla item)
    if (!root.get("locations").is<picojson::array>())
        return fail(error, "the pack has no locations");
    const auto& locations = root.get("locations").get<picojson::array>();
    if (locations.empty() || locations.size() > BLUEWAKE_RANDO_MAX_LOCATIONS)
        return fail(error, "the pack has " + std::to_string(locations.size()) + " locations");
    for (size_t i = 0; i < locations.size(); ++i) {
        const picojson::value& loc = locations[i];
        if (!loc.is<picojson::object>() || integer(loc.get("id"), -1) != static_cast<int64_t>(i))
            return fail(error, "location " + std::to_string(i) + " is not in id order");
        const int64_t item = loc.get("item_id").is<picojson::null>() ? -1 : integer(loc.get("item_id"), -1);
        if (item > 0xFF)
            return fail(error, "location " + std::to_string(i) + " has item id " + std::to_string(item));
        pack.item_id[i] = static_cast<s16>(item);
    }
    pack.num_locations = static_cast<u32>(locations.size());

    // starting state
    const picojson::value& starting = root.get("starting");
    if (!starting.is<picojson::object>() || !starting.get("island").is<picojson::object>() ||
        !starting.get("game_variables").is<picojson::object>() || !starting.get("gear").is<picojson::array>())
        return fail(error, "the pack has no starting state");
    pack.starting_room = static_cast<s32>(integer(starting.get("island").get("room"), -1));
    if (pack.starting_room < 1 || pack.starting_room > 49)
        return fail(error, "the starting island's room is " + std::to_string(pack.starting_room));
    const picojson::value& vars = starting.get("game_variables");
    pack.sword_mode = static_cast<u8>(integer(vars.get("sword_mode"), 0));
    pack.triforce_shards = static_cast<u8>(integer(vars.get("num_triforce_shards_to_start_with"), 0));
    pack.quarter_hearts = static_cast<u16>(integer(vars.get("starting_quarter_hearts"), 12));
    pack.magic = static_cast<u8>(integer(vars.get("starting_magic"), 0));
    pack.skip_rematch_bosses = static_cast<u8>(integer(vars.get("skip_rematch_bosses"), 0));
    pack.pigs_bitfield = static_cast<u8>(integer(vars.get("captured_prologue_pigs_bitfield"), 0));
    pack.targeting_mode = static_cast<u8>(integer(vars.get("option_targeting_mode"), 0) != 0 ? 1 : 0);
    if (vars.get("damage_multiplier").is<double>() || vars.get("damage_multiplier").is<int64_t>()) {
        pack.has_damage_multiplier = true;
        pack.damage_multiplier = static_cast<float>(vars.get("damage_multiplier").is<double>()
                                                         ? vars.get("damage_multiplier").get<double>()
                                                         : static_cast<double>(vars.get("damage_multiplier").get<int64_t>()));
    }
    // wwrando's change_starting_clothes: casual clothes only when the option asks for them
    pack.heros_clothes = std::strcmp(bluewake_rando_option(&pack, "player_in_casual_clothes"), "true") == 0 ? 0 : 1;
    const auto& gear = starting.get("gear").get<picojson::array>();
    if (gear.size() > BLUEWAKE_RANDO_MAX_GEAR - 1)
        return fail(error, "more starting items than the game holds (" + std::to_string(gear.size()) + ")");
    for (const auto& item : gear) {
        const int64_t id = integer(item.get("item_id"), -1);
        if (id < 0 || id > 0xFE)
            return fail(error, "a starting item has id " + std::to_string(id));
        pack.gear[pack.gear_count++] = static_cast<u8>(id);
    }
    pack.loaded = true;
    *out = pack;
    return true;
}

bool Load(const fs::path& seed_json, BlueWakeRandoPack* out, std::string* error) {
    std::ifstream in(seed_json, std::ios::binary);
    if (!in)
        return fail(error, "cannot read " + utf8(seed_json));
    std::stringstream text;
    text << in.rdbuf();
    return Parse(text.str(), out, error);
}

std::vector<Entry> List(const fs::path& root) {
    std::vector<Entry> out;
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec))
            continue;
        const fs::path json = it->path() / "seed.json";
        if (!fs::is_regular_file(json, ec))
            continue;
        // The list needs the name, hash and permalink only: a cheap read of the head would do, but a pack is 300 KB
        // and a few dozen of them are listed once per file-select visit.
        std::ifstream in(json, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        picojson::value v;
        if (!picojson::parse(v, text.str()).empty() || !v.is<picojson::object>() ||
            !rando_seedpack_schema(v.get("schema").to_str()))
            continue;
        Entry entry;
        entry.name = utf8(it->path().filename());
        entry.seed_hash = v.get("seed_hash").to_str();
        entry.permalink = v.get("permalink").to_str();
        entry.json = json;
        const auto time = fs::last_write_time(json, ec);
        entry.modified = ec ? 0 : static_cast<std::time_t>(std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.modified > b.modified; });
    return out;
}

std::string Identity(const BlueWakeRandoPack& pack) {
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](const char* s) {
        for (; *s != '\0'; ++s) {
            hash ^= static_cast<unsigned char>(*s);
            hash *= 1099511628211ull;
        }
        hash ^= 0xFF;
        hash *= 1099511628211ull;
    };
    mix(pack.permalink);
    mix(pack.wwrando_commit);
    char text[32];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

}  // namespace randopack
