// The seed pack loader (windows/src/rando_pack.cpp) on a small pack in generate_seed.py's shape, and, when
// BLUEWAKE_TEST_SEED_JSON names a real seed.json (RANDO_SEED_JSON, a pack made by scripts/rando/generate_seed.py), on that.
#include "rando_pack.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static std::string locations(int count, const char* item_for_two) {
    std::string out = "[";
    for (int i = 0; i < count; ++i) {
        if (i)
            out += ",";
        out += "{\"id\": " + std::to_string(i) + ", \"name\": \"L" + std::to_string(i) + "\", \"item_id\": " +
               (i == 2 ? std::string(item_for_two) : std::to_string(i % 100)) + "}";
    }
    return out + "]";
}

static std::string pack_json(const std::string& locs, const std::string& extra_options = "") {
    return "{\"schema\": \"bod.seedpack\", \"schema_version\": 1, \"generator\": {\"wwrando_commit\": \"86b97db\"},"
           " \"seed\": \"TEST\", \"permalink\": \"MS4xMC4w\", \"seed_hash\": \"Din Sam\","
           " \"options\": {\"hero_mode\": true, \"sword_mode\": \"Swordless\", \"num_starting_pohs\": 3,"
           " \"player_in_casual_clothes\": false" + extra_options + "},"
           " \"starting\": {\"gear\": [{\"item\": \"Ballad of Gales\", \"item_id\": 110}, {\"item\": \"Progressive Shield\", \"item_id\": 59}],"
           " \"island\": {\"randomized\": false, \"room\": 44, \"name\": \"Outset Island\", \"spawn_id\": 0},"
           " \"game_variables\": {\"sword_mode\": 2, \"num_triforce_shards_to_start_with\": 3, \"starting_quarter_hearts\": 12,"
           " \"starting_magic\": 16, \"skip_rematch_bosses\": 1, \"damage_multiplier\": 4.0, \"option_targeting_mode\": null,"
           " \"captured_prologue_pigs_bitfield\": 4}},"
           " \"locations\": " + locs + "}";
}

int main() {
    BlueWakeRandoPack p;
    std::string error;
    // a good pack
    assert(randopack::Parse(pack_json(locations(320, "null")), &p, &error));
    assert(p.loaded && std::strcmp(p.seed, "TEST") == 0 && std::strcmp(p.seed_hash, "Din Sam") == 0);
    assert(p.num_locations == 320 && p.item_id[0] == 0 && p.item_id[1] == 1 && p.item_id[2] == -1 && p.item_id[319] == 19);
    assert(p.starting_room == 44 && p.sword_mode == 2 && p.triforce_shards == 3 && p.quarter_hearts == 12 && p.magic == 16);
    assert(p.skip_rematch_bosses == 1 && p.pigs_bitfield == 4 && p.targeting_mode == 0);
    assert(p.has_damage_multiplier && p.damage_multiplier == 4.0f);
    assert(p.heros_clothes == 1);
    assert(p.gear_count == 2 && p.gear[0] == 110 && p.gear[1] == 59);
    assert(std::strcmp(bluewake_rando_option(&p, "hero_mode"), "true") == 0);
    assert(std::strcmp(bluewake_rando_option(&p, "sword_mode"), "Swordless") == 0);
    assert(std::strcmp(bluewake_rando_option(&p, "num_starting_pohs"), "3") == 0);
    assert(std::strcmp(bluewake_rando_option(&p, "player_in_casual_clothes"), "false") == 0);
    assert(std::strcmp(bluewake_rando_option(&p, "nothing"), "") == 0);
    // the identity follows the permalink and the generator, not the folder
    const std::string id = randopack::Identity(p);
    assert(id.size() == 16);
    BlueWakeRandoPack q = p;
    std::snprintf(q.permalink, sizeof q.permalink, "OTHER");
    assert(randopack::Identity(q) != id);
    q = p;
    std::snprintf(q.wwrando_commit, sizeof q.wwrando_commit, "other");
    assert(randopack::Identity(q) != id);
    q = p;
    std::snprintf(q.seed, sizeof q.seed, "another name");
    assert(randopack::Identity(q) == id);
    // casual clothes
    BlueWakeRandoPack casual;
    std::string text = pack_json(locations(320, "5"));
    text.replace(text.find("\"player_in_casual_clothes\": false"), 33, "\"player_in_casual_clothes\": true");
    assert(randopack::Parse(text, &casual, &error) && casual.heros_clothes == 0);

    // refusals, each with a reason
    BlueWakeRandoPack bad;
    assert(!randopack::Parse("not json", &bad, &error) && !error.empty() && !bad.loaded);
    assert(!randopack::Parse("{\"schema\": \"other\"}", &bad, &error) && error.find("not a seed pack") != std::string::npos);
    assert(!randopack::Parse(pack_json(locations(0, "1")), &bad, &error) && error.find("0 locations") != std::string::npos);
    assert(!randopack::Parse(pack_json(locations(401, "1")), &bad, &error));
    assert(!randopack::Parse(pack_json(locations(320, "300")), &bad, &error) && error.find("item id") != std::string::npos);
    std::string wrong_order = pack_json(locations(320, "1"));
    wrong_order.replace(wrong_order.find("\"id\": 5,"), 8, "\"id\": 6,");
    assert(!randopack::Parse(wrong_order, &bad, &error) && error.find("id order") != std::string::npos);
    std::string room = pack_json(locations(320, "1"));
    room.replace(room.find("\"room\": 44"), 10, "\"room\": 77");
    assert(!randopack::Parse(room, &bad, &error) && error.find("room") != std::string::npos);

    if (const char* path = std::getenv("BLUEWAKE_TEST_SEED_JSON"); path != nullptr && path[0] != '\0') {
        BlueWakeRandoPack real;
        if (!randopack::Load(path, &real, &error)) {
            std::fprintf(stderr, "real pack: %s\n", error.c_str());
            return 1;
        }
        assert(real.num_locations == 320 && real.starting_room >= 1 && real.gear_count >= 1);
        std::printf("real pack %s (%s): %u locations, %u options, %u starting items, room %d\n", real.seed, real.seed_hash,
                    real.num_locations, real.num_options, real.gear_count, real.starting_room);
    }
    std::puts("rando_pack_test: ok");
    return 0;
}
