// The seed pack loader: Randomizer\<seed>\seed.json as scripts/rando/generate_seed.py writes it, into the host's
// BlueWakeRandoPack (runtime/host/src/rando_seed.h). wwrando (LagoLunatic, MIT) makes the seed; SoH's seed folder is the model.
#pragma once

#include "rando_seed.h"

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

// A seed pack's schema: bod.seedpack, or an older build's name for it (any name ending in .seedpack), so seeds made
// by earlier versions keep loading.
inline bool rando_seedpack_schema(const std::string& schema) {
    static const std::string kSuffix = ".seedpack";
    return schema.size() > kSuffix.size() && schema.compare(schema.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

namespace randopack {

// Reads a seed.json. False with `error` set if the file is missing, is not a seed pack (a schema ending in .seedpack)
// version 1, or does not carry what the game needs.
bool Parse(const std::string& json_text, BlueWakeRandoPack* out, std::string* error);
bool Load(const std::filesystem::path& seed_json, BlueWakeRandoPack* out, std::string* error);

// One seed folder of Randomizer\, for the file select's seed list.
struct Entry {
    std::string name;       // the folder's name (the seed)
    std::string seed_hash;  // the two names
    std::string permalink;
    std::filesystem::path json;
    std::time_t modified = 0;
};
// The seed folders under `root` (Randomizer\) that hold a readable seed.json, newest first.
std::vector<Entry> List(const std::filesystem::path& root);

// The pack's identity for a save: the permalink fixes the seed and the options, and the generator's commit fixes what
// they mean. 16 hex digits of an FNV-1a hash over both.
std::string Identity(const BlueWakeRandoPack& pack);

}  // namespace randopack
