// The Check Tracker: see check_tracker.h.
#ifdef rename
#undef rename // bw_posix_compat.h's, which <filesystem> (aurora's headers) must not see
#endif

#include "check_tracker.h"

#include "rando_flow.h"
#include "sea_chart.h"
#include "ship_menu.h"
#include "ship/cvar.h"

#define PICOJSON_USE_INT64
#include "picojson.h"

#include <imgui.h>

#include "platform/bod_platform.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "dev_tools.h"  // the host's (runtime/host/src)
#include "game_state.h"
}

namespace checktracker {
namespace {

namespace fs = std::filesystem;
using shipmenu::WidgetInfo;
using shipmenu::WidgetType;

#define TRACKER_CVAR(name) "gTrackers.CheckTracker." name
constexpr const char* kWindowOpen = "gOpenWindows.CheckTracker";

// --- The game's memory (GZLE01; tww decomp d_save.h) --------------------------------

constexpr unsigned kInfo = 0x803C4C08u;       // g_dComIfG_gameInfo (dSv_info_c)
constexpr unsigned kSnapshot = 0x7A0u;        // through the current stage's memory (0x778, 0x24)
constexpr unsigned kItems = 0x3C;             // dSv_player_item_c::mItems[21], by inventory slot
constexpr unsigned kWallet = 0x12, kMaxMagic = 0x13;
constexpr unsigned kMaxArrows = 0x6F, kMaxBombs = 0x70; // dSv_player_item_max_c
constexpr unsigned kReserve = 0x90;           // dSv_player_get_bag_item_c::mReserveFlags (the delivery letters)
constexpr unsigned kCollect = 0xB4;           // dSv_player_collect_c: swords, shields, bracelets, charms, ..., songs, shards, pearls
constexpr unsigned kMap = 0xC4;               // dSv_player_map_c: field_0x0[4][4], the charts got (1) and salvaged (3)
constexpr unsigned kMemoryTable = 0x380;      // dSv_save_c::mMemory[16], 0x24 each
constexpr unsigned kOcean = 0x5C0;            // dSv_ocean_c: u16 per sea square, a bit per treasure
constexpr unsigned kEvent = 0x624;            // dSv_event_c, 0x100 bytes of event bits
constexpr unsigned kCurrentMemory = 0x778;    // dSv_info_c::mMemory, the stage Link is in
constexpr unsigned kStageInfoPtr = 0x803C9DA0u; // play.mStageData.mpStagInfo
constexpr int kNoItem = 0xFF;

struct Save {
    uint8_t b[kSnapshot];
    unsigned u8(unsigned o) const { return b[o]; }
    unsigned u16(unsigned o) const { return unsigned(b[o]) << 8 | b[o + 1]; }
    unsigned u32(unsigned o) const { return unsigned(b[o]) << 24 | unsigned(b[o + 1]) << 16 | unsigned(b[o + 2]) << 8 | b[o + 3]; }
};

bool read_u32(unsigned address, unsigned* out) {
    unsigned char raw[4];
    if (!bluewake_dev_read(address, raw, 4))
        return false;
    *out = unsigned(raw[0]) << 24 | unsigned(raw[1]) << 16 | unsigned(raw[2]) << 8 | raw[3];
    return true;
}

// The save table of the stage Link is in (dStage_stagInfo_GetSaveTbl), -1 when unknown.
int current_stage_table() {
    unsigned info = 0;
    if (!read_u32(kStageInfoPtr, &info) || info < 0x80000000u || info > 0x81800000u)
        return -1;
    unsigned char prop = 0;
    if (!bluewake_dev_read(info + 9, &prop, 1))
        return -1;
    return (prop >> 1) & 0x7F;
}

// --- The seed's locations ---------------------------------------------------------------

// A bit that says a location was collected (scripts/rando/make_check_flags.py's kinds).
struct Flag {
    char kind = 0; // 'T' chest (mTbox), 'I' item bit (mItem), 'O' ocean (grid, bit), 'C' chart salvaged
    int table = 0, bit = 0, grid = 0;
};

struct Loc {
    int id = 0;
    std::string name, area, rest, item;
    int itemId = -1;
    bool progress = true;
    std::vector<Flag> flags; // none: nothing in the save says this one is collected (an NPC's or shop's gift)
    bool collected = false, reachable = false, skipped = false;
    bool byTeam = false;   // collected while the team's save came in (Anchor): a teammate's
    std::string hint;      // what the seed's hints say of it ("" none)
    bool inferred = false; // collected judged from the save holding its (only) item, for a gift with no flag of its own
};

// One of the seed's hints (seed.json "hints", records): what the hint-givers (fishmen, Old Man Ho Ho, Korl, the Savage
// Labyrinth's tablet and the Big Octo's fairy) can tell. Hints are listed whether or not this game has heard them.
struct Hint {
    std::string kind, place, reward, importance;
};

struct Area {
    std::string name;
    std::vector<int> locs; // indices into g_locs
};

std::vector<Loc> g_locs;
std::vector<Area> g_areas;
std::map<int, std::string> g_item_names; // item id -> wwrando's name, from the seed's own items (the charts)
std::map<std::string, std::string> g_stage_areas; // stage name -> area (check_flags.json, from the locations' Paths)
std::map<std::string, int> g_item_copies; // how many locations hold each item in this seed
std::set<std::string> g_starting;         // the items the seed starts the player with
std::vector<Hint> g_hints;
std::set<std::string> g_barren;          // places a barren hint names
std::atomic<uint64_t> g_team_save_at{0}; // when the team's save last came in (GetTickCount64)
std::string g_seed;          // the seed the data above is of ("" none)
std::string g_seed_error;
int g_slot = -1;             // the save file in play (its skipped checks are loaded)

std::string area_of(const std::string& name) {
    std::string a = name.substr(0, name.find(" - "));
    const std::string sector = " Sector";
    if (a.size() > sector.size() && a.compare(a.size() - sector.size(), sector.size(), sector) == 0)
        a.erase(a.size() - sector.size());
    return a;
}

// Outset, Windfall, Dragon Roost and Forest Haven first (where play starts), the other islands in Sea Chart order, the
// dungeons, then what is not on the chart.
std::vector<std::string> area_order() {
    static const char* const first[] = {"Outset Island", "Windfall Island", "Dragon Roost Island", "Forest Haven"};
    static const char* const dungeons[] = {"Dragon Roost Cavern", "Forbidden Woods", "Tower of the Gods", "Forsaken Fortress",
                                           "Earth Temple", "Wind Temple", "Hyrule", "Ganon's Tower"};
    std::vector<std::string> order(std::begin(first), std::end(first));
    auto listed = [&](const std::string& s) {
        return std::find(order.begin(), order.end(), s) != order.end() ||
               std::find(std::begin(dungeons), std::end(dungeons), s) != std::end(dungeons);
    };
    for (int room = 1; room <= 49; ++room)
        if (!listed(sea_chart::Island(room)))
            order.emplace_back(sea_chart::Island(room));
    order.insert(order.end(), std::begin(dungeons), std::end(dungeons));
    order.emplace_back("Mailbox");
    order.emplace_back("The Great Sea");
    return order;
}

std::string u8str(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path tools_dir() {
    // BLUEWAKE_RANDO_DIR (testing): the folder with the scripts, in place of rando\ beside the exe.
    if (const char* env = std::getenv("BLUEWAKE_RANDO_DIR"); env != nullptr && env[0] != '\0')
        return fs::u8path(env);
    const fs::path exe = bodplat::ExeFolder();
    std::error_code ec;
    if (fs::exists(exe / "rando" / "check_logic_server.py", ec))
        return exe / "rando";
    // A build in the repo (build\windowspp): the scripts as they are checked in.
    const fs::path repo = exe.parent_path().parent_path().parent_path() / "scripts" / "rando";
    return fs::exists(repo / "check_logic_server.py", ec) ? repo : exe / "rando";
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Loads the active seed's locations and the flag table; sets g_seed_error when it cannot.
void load_seed(const std::string& seed) {
    g_locs.clear();
    g_areas.clear();
    g_item_names.clear();
    g_item_copies.clear();
    g_stage_areas.clear();
    g_starting.clear();
    g_hints.clear();
    g_barren.clear();
    g_seed_error.clear();
    g_seed = seed;
    const std::string text = read_file(fs::u8path(randoflow::SeedJson()));
    picojson::value root;
    if (text.empty() || !picojson::parse(root, text).empty() || !root.is<picojson::object>()) {
        g_seed_error = "The seed file could not be read.";
        return;
    }
    picojson::value flags_doc;
    const std::string flags_text = read_file(tools_dir() / "check_flags.json");
    if (flags_text.empty() || !picojson::parse(flags_doc, flags_text).empty()) {
        g_seed_error = "check_flags.json is missing (rando\\ beside the game): collected checks cannot be told.";
        flags_doc = picojson::value();
    }
    const picojson::value& table = flags_doc.get("flags");
    if (flags_doc.get("stage_areas").is<picojson::object>())
        for (const auto& [stage, area] : flags_doc.get("stage_areas").get<picojson::object>())
            g_stage_areas[stage] = area.to_str();
    for (const picojson::value& l : root.get("locations").get<picojson::array>()) {
        Loc loc;
        loc.id = int(l.get("id").get<int64_t>());
        loc.name = l.get("name").to_str();
        loc.item = l.get("item").to_str();
        loc.itemId = l.get("item_id").is<int64_t>() ? int(l.get("item_id").get<int64_t>()) : -1;
        loc.progress = l.get("progress_location").evaluate_as_boolean();
        loc.area = area_of(loc.name);
        const size_t dash = loc.name.find(" - ");
        loc.rest = dash == std::string::npos ? loc.name : loc.name.substr(dash + 3);
        if (table.is<picojson::object>() && table.contains(loc.name)) {
            for (const picojson::value& f : table.get(loc.name).get<picojson::array>()) {
                Flag fl;
                const std::string k = f.get("k").to_str();
                fl.kind = k == "tbox" ? 'T' : k == "item" ? 'I' : k == "ocean" ? 'O' : k == "chart" ? 'C' : 0;
                fl.table = int(f.get("t").is<int64_t>() ? f.get("t").get<int64_t>() : 0);
                fl.bit = int(f.get("b").is<int64_t>() ? f.get("b").get<int64_t>() : 0);
                fl.grid = int(f.get("g").is<int64_t>() ? f.get("g").get<int64_t>() : 0);
                if (fl.kind)
                    loc.flags.push_back(fl);
            }
        }
        if (loc.itemId >= 0)
            g_item_names[loc.itemId] = loc.item;
        ++g_item_copies[loc.item];
        g_locs.push_back(std::move(loc));
    }
    {
        std::set<std::string> seen;
        const picojson::value& records = root.get("hints").get("records");
        if (records.is<picojson::array>())
            for (const picojson::value& r : records.get<picojson::array>()) {
                Hint h{r.get("kind").to_str(), r.get("place").to_str(), r.get("reward").to_str(), r.get("importance").to_str()};
                const std::string loc = r.get("location").to_str();
                if (!seen.insert(h.kind + "|" + h.place + "|" + h.reward).second)
                    continue;
                g_hints.push_back(h);
                if (h.kind == "barren")
                    g_barren.insert(h.place);
                for (Loc& l : g_locs)
                    if (!loc.empty() && l.name == loc && (h.kind == "item" || h.kind == "location" || h.kind == "fixed_location"))
                        l.hint = h.kind == "item" ? "An item hint names this place." : "A hint names this location.";
            }
    }
    for (const picojson::value& v : root.get("starting").get("logic_items").get<picojson::array>())
        g_starting.insert(v.to_str());
    const std::vector<std::string> order = area_order();
    std::map<std::string, size_t> at;
    for (size_t i = 0; i < g_locs.size(); ++i) {
        const std::string& a = g_locs[i].area;
        if (!at.count(a)) {
            at[a] = g_areas.size();
            g_areas.push_back({a, {}});
        }
        g_areas[at[a]].locs.push_back(int(i));
    }
    auto rank = [&](const std::string& a) {
        const auto it = std::find(order.begin(), order.end(), a);
        return it == order.end() ? order.size() : size_t(it - order.begin());
    };
    std::stable_sort(g_areas.begin(), g_areas.end(), [&](const Area& x, const Area& y) { return rank(x.name) < rank(y.name); });
    size_t mapped = 0;
    for (const Loc& l : g_locs)
        mapped += l.flags.empty() ? 0 : 1;
    std::fprintf(stderr, "[checktracker] seed %s: %zu locations in %zu areas, %zu with a save flag\n", seed.c_str(), g_locs.size(),
                 g_areas.size(), mapped);
}

// --- Skipped checks, kept per seed and save file (Save\CheckTracker.json) ------------------

fs::path skipped_path() { return fs::u8path(randoflow::SaveFolder()) / "CheckTracker.json"; }
// A slot's list in the file: "<seed>#<slot>" for the skipped checks, "<seed>#<slot>#team" for those a teammate collected.
std::string skipped_key(int slot, bool team = false) { return g_seed + "#" + std::to_string(slot) + (team ? "#team" : ""); }

void load_skipped(int slot) {
    for (Loc& l : g_locs)
        l.skipped = l.byTeam = false;
    picojson::value root;
    const std::string text = read_file(skipped_path());
    if (text.empty() || !picojson::parse(root, text).empty() || !root.is<picojson::object>())
        return;
    for (int team = 0; team < 2; ++team) {
        const picojson::value& list = root.get(skipped_key(slot, team != 0));
        if (!list.is<picojson::array>())
            continue;
        std::set<int> ids;
        for (const picojson::value& v : list.get<picojson::array>())
            if (v.is<int64_t>())
                ids.insert(int(v.get<int64_t>()));
        for (Loc& l : g_locs)
            (team ? l.byTeam : l.skipped) = ids.count(l.id) != 0;
    }
}

void save_skipped(int slot) {
    picojson::object root;
    const std::string text = read_file(skipped_path());
    picojson::value old;
    if (!text.empty() && picojson::parse(old, text).empty() && old.is<picojson::object>())
        root = old.get<picojson::object>();
    for (int team = 0; team < 2; ++team) {
        picojson::array ids;
        for (const Loc& l : g_locs)
            if (team ? l.byTeam : l.skipped)
                ids.emplace_back(int64_t(l.id));
        if (ids.empty())
            root.erase(skipped_key(slot, team != 0));
        else
            root[skipped_key(slot, team != 0)] = picojson::value(ids);
    }
    std::error_code ec;
    fs::create_directories(skipped_path().parent_path(), ec);
    std::ofstream out(skipped_path(), std::ios::binary | std::ios::trunc);
    out << picojson::value(root).serialize(true);
}

// --- What the save says ---------------------------------------------------------------------

unsigned memory_base(int table, int current) {
    return table == current ? kCurrentMemory : kMemoryTable + unsigned(table) * 0x24u;
}

bool flag_set(const Flag& f, const Save& s, int current) {
    switch (f.kind) {
    case 'T':
        return f.table >= 0 && f.table < 16 && (s.u32(memory_base(f.table, current)) >> f.bit & 1u) != 0;
    case 'I':
        return f.table >= 0 && f.table < 16 && (s.u32(memory_base(f.table, current) + 0x14) >> f.bit & 1u) != 0;
    case 'O':
        return f.grid >= 0 && f.grid < 0x32 && (s.u16(kOcean + unsigned(f.grid) * 2) >> f.bit & 1u) != 0;
    case 'C':
        return f.bit >= 0 && f.bit < 128 && (s.u32(kMap + 0x30 + unsigned(f.bit >> 5) * 4) >> (f.bit & 31) & 1u) != 0;
    }
    return false;
}

using Counts = std::map<std::string, int>;

void raise(Counts& c, const std::string& name, int n) {
    if (n > 0 && c[name] < n)
        c[name] = n;
}

// What the player has, in wwrando's item names, read from the save (the logic helper ignores names it does not know).
Counts inventory_of(const Save& s) {
    Counts c;
    static const struct { int id; const char* name; int n; } kSlotItems[] = {
        {0x20, "Telescope", 1},      {0x78, "Boat's Sail", 1},          {0x22, "Wind Waker", 1}, {0x25, "Grappling Hook", 1},
        {0x24, "Spoils Bag", 1},     {0x2D, "Boomerang", 1},            {0x34, "Deku Leaf", 1},  {0x21, "Tingle Tuner", 1},
        {0x23, "Progressive Picto Box", 1}, {0x26, "Progressive Picto Box", 2}, {0x29, "Iron Boots", 1},
        {0x2A, "Magic Armor", 1},    {0x2C, "Bait Bag", 1},             {0x27, "Progressive Bow", 1},
        {0x35, "Progressive Bow", 2}, {0x36, "Progressive Bow", 3},     {0x31, "Bombs", 1},      {0x30, "Delivery Bag", 1},
        {0x2F, "Hookshot", 1},       {0x33, "Skull Hammer", 1},
    };
    int bottles = 0;
    for (unsigned slot = 0; slot < 21; ++slot) {
        const int item = int(s.u8(kItems + slot));
        if (item == kNoItem)
            continue;
        if (slot >= 14 && slot <= 17) // the four bottle slots
            ++bottles;
        for (const auto& e : kSlotItems)
            if (e.id == item)
                raise(c, e.name, e.n);
    }
    raise(c, "Empty Bottle", bottles);
    const unsigned sword = s.u8(kCollect + 0) & 0xF;
    raise(c, "Progressive Sword", (sword & 8) ? 4 : (sword & 4) ? 3 : (sword & 2) ? 2 : (sword & 1) ? 1 : 0);
    const unsigned shield = s.u8(kCollect + 1) & 3;
    raise(c, "Progressive Shield", (shield & 2) ? 2 : (shield & 1) ? 1 : 0);
    if (s.u8(kCollect + 2) & 1)
        raise(c, "Power Bracelets", 1);
    static const char* const songs[6] = {"Wind's Requiem", "Ballad of Gales", "Command Melody", "Earth God's Lyric",
                                         "Wind God's Aria", "Song of Passing"};
    for (int i = 0; i < 6; ++i)
        if (s.u8(kCollect + 9) & (1u << i))
            raise(c, songs[i], 1);
    for (int i = 0; i < 8; ++i)
        if (s.u8(kCollect + 0xA) & (1u << i))
            raise(c, "Triforce Shard " + std::to_string(i + 1), 1);
    static const char* const pearls[3] = {"Nayru's Pearl", "Din's Pearl", "Farore's Pearl"};
    for (int i = 0; i < 3; ++i)
        if (s.u8(kCollect + 0xB) & (1u << i))
            raise(c, pearls[i], 1);
    raise(c, "Progressive Wallet", int(std::min(s.u8(kWallet), 2u)));
    raise(c, "Progressive Magic Meter", int(std::min(s.u8(kMaxMagic) / 16u, 2u)));
    auto upgrades = [](unsigned cap) { return cap >= 99 ? 2 : cap >= 60 ? 1 : 0; };
    raise(c, "Progressive Bomb Bag", upgrades(s.u8(kMaxBombs)));
    raise(c, "Progressive Quiver", upgrades(s.u8(kMaxArrows)));
    // The delivery bag's papers (reserve flags), wwrando's own event bits for the Tingle statues and Hurricane Spin.
    const unsigned reserve = s.u32(kReserve);
    if (reserve & (1u << 0xD)) raise(c, "Note to Mom", 1);
    if (reserve & (1u << 0xE)) raise(c, "Maggie's Letter", 1);
    if (reserve & (1u << 0xF)) raise(c, "Moblin's Letter", 1);
    if (reserve & (1u << 0x10)) raise(c, "Cabana Deed", 1);
    static const struct { unsigned byte, mask; const char* name; } kEvents[] = {
        {0x6A, 0x04, "Dragon Tingle Statue"},    {0x6A, 0x08, "Forbidden Tingle Statue"}, {0x6A, 0x10, "Goddess Tingle Statue"},
        {0x6A, 0x20, "Earth Tingle Statue"},     {0x6A, 0x40, "Wind Tingle Statue"},      {0x69, 0x01, "Hurricane Spin"},
    };
    for (const auto& e : kEvents)
        if (s.u8(kEvent + e.byte) & e.mask)
            raise(c, e.name, 1);
    // Charts: collect map n (1 to 64) is item 0xFF - n; the seed's own items name them.
    for (int bit = 0; bit < 64; ++bit) {
        if (!(s.u32(kMap + 0x10 + unsigned(bit >> 5) * 4) >> (bit & 31) & 1u))
            continue;
        const auto it = g_item_names.find(0xFE - bit);
        if (it != g_item_names.end())
            raise(c, it->second, 1);
    }
    return c;
}

// --- The reachability helper (wwrando's Logic in the bundled Python) ------------------------------

class Helper {
public:
    ~Helper() { Stop(); }

    // Starts the helper for this seed file; does nothing when it already runs for it.
    void Start(const std::string& seedJson) {
        if (mSeed == seedJson && mThread.joinable())
            return;
        Stop();
        mSeed = seedJson;
        mQuit = false;
        mState = "starting";
        mThread = std::thread([this, seedJson] { Run(seedJson); });
    }

    void Stop() {
        if (!mThread.joinable())
            return;
        mQuit = true;
        mCv.notify_all();
        {
            std::lock_guard<std::mutex> lock(mProcessMutex);
            if (mProcess != nullptr)
                mProcess->Kill(); // unblocks the worker's read
        }
        mThread.join();
        mSeed.clear();
        std::lock_guard<std::mutex> lock(mMutex);
        mReachable.clear();
        mHave = false;
    }

    // Asks for the reachable locations given what the player has (a JSON object of counts). The latest request wins.
    void Ask(const std::string& itemsJson) {
        std::lock_guard<std::mutex> lock(mMutex);
        mPending = itemsJson;
        mHasPending = true;
        mCv.notify_all();
    }

    bool Get(std::set<std::string>* out) {
        std::lock_guard<std::mutex> lock(mMutex);
        *out = mReachable;
        return mHave;
    }
    std::string State() {
        std::lock_guard<std::mutex> lock(mMutex);
        return mState;
    }

private:
    void SetState(const std::string& s) {
        std::lock_guard<std::mutex> lock(mMutex);
        mState = s;
    }

    void Run(const std::string& seedJson) {
        const fs::path tools = tools_dir();
        const char* rt = std::getenv("RANDO_RUNTIME");
        const fs::path runtime = rt != nullptr && rt[0] != '\0' ? fs::u8path(rt) : tools;
        const fs::path python = bodplat::PythonExe(runtime);
        const fs::path script = tools / "check_logic_server.py";
        if (!fs::exists(python) || !fs::exists(script)) {
            SetState("The randomizer runtime is not installed (rando\\ beside the game).");
            return;
        }
        std::error_code ec;
        const fs::path logPath = fs::u8path(randoflow::SaveFolder()) / "CheckTrackerHelper.log";
        fs::create_directories(logPath.parent_path(), ec);
        bodplat::Process proc;
        bodplat::Process::Options helper;
        helper.program = python;
        helper.args = {u8str(script), seedJson};
        helper.env = {{"RANDO_RUNTIME", u8str(runtime)}, {"PYTHONUTF8", "1"}};
        helper.stderrFile = logPath;
        helper.pipes = true;
        if (!proc.Start(helper)) {
            SetState("The logic helper could not be started.");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mProcessMutex);
            mProcess = &proc;
        }
        std::fprintf(stderr, "[checktracker] logic helper started (pid %lu) for %s\n", proc.Pid(), seedJson.c_str());

        std::string buffer;
        auto readLine = [&](std::string* out) {
            for (;;) {
                const size_t nl = buffer.find('\n');
                if (nl != std::string::npos) {
                    *out = buffer.substr(0, nl);
                    buffer.erase(0, nl + 1);
                    while (!out->empty() && out->back() == '\r')
                        out->pop_back();
                    return true;
                }
                char chunk[8192];
                const long got = proc.ReadStdout(chunk, sizeof chunk);
                if (got <= 0)
                    return false;
                buffer.append(chunk, static_cast<size_t>(got));
            }
        };
        std::string reply;
        if (!readLine(&reply)) {
            SetState(mQuit ? "" : "The logic helper stopped (see Save\\CheckTrackerHelper.log).");
        } else {
            picojson::value ready;
            picojson::parse(ready, reply);
            std::fprintf(stderr, "[checktracker] logic helper ready: %s\n", reply.c_str());
            SetState("ready");
            for (;;) {
                std::string request;
                {
                    std::unique_lock<std::mutex> lock(mMutex);
                    mCv.wait(lock, [&] { return mHasPending || mQuit; });
                    if (mQuit)
                        break;
                    request = mPending;
                    mHasPending = false;
                }
                const std::string msg = "{\"id\":1,\"items\":" + request + "}\n";
                if (!proc.WriteStdin(msg.data(), msg.size()) || !readLine(&reply)) {
                    if (!mQuit)
                        SetState("The logic helper stopped (see Save\\CheckTrackerHelper.log).");
                    break;
                }
                picojson::value r;
                if (!picojson::parse(r, reply).empty() || !r.get("reachable").is<picojson::array>()) {
                    SetState("The logic helper answered with an error: " + r.get("error").to_str());
                    continue;
                }
                std::set<std::string> names;
                for (const picojson::value& n : r.get("reachable").get<picojson::array>())
                    names.insert(n.to_str());
                std::lock_guard<std::mutex> lock(mMutex);
                mReachable = std::move(names);
                mHave = true;
                ++mAnswers;
                std::fprintf(stderr, "[checktracker] reachable: %zu locations (answer %d)\n", mReachable.size(), mAnswers);
            }
        }
        proc.CloseStdin();
        proc.Kill();
        {
            std::lock_guard<std::mutex> lock(mProcessMutex);
            mProcess = nullptr;
        }
    }

    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCv;
    std::string mSeed, mPending, mState;
    bool mHasPending = false, mHave = false;
    std::atomic<bool> mQuit{false};
    std::mutex mProcessMutex;
    bodplat::Process* mProcess = nullptr; // the helper while Run has it, under mProcessMutex
    std::set<std::string> mReachable;
    int mAnswers = 0;
};

Helper g_helper;
std::string g_last_request;

// --- Where Link is (to open that area) -------------------------------------------------------------

std::string current_area() {
    const BluewakeLocation where = bluewake_game_location();
    const std::string stage = where.stage;
    if (stage == "sea") {
        const char* island = sea_chart::Island(where.room);
        return island != nullptr ? island : "";
    }
    const auto it = g_stage_areas.find(stage);
    if (it != g_stage_areas.end())
        return it->second;
    return "";
}

// --- The window ------------------------------------------------------------------------------------

constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);

bool contains_nocase(const std::string& hay, const std::string& needle) {
    if (needle.empty())
        return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return it != hay.end();
}

bool opt(const char* cvar, int def = 0) { return CVarGetInteger(cvar, def) != 0; }

void update_state(const Save& save) {
    const int current = current_stage_table();
    std::set<std::string> reachable;
    const bool answered = g_helper.Get(&reachable);
    Counts fromLocations;
    const Counts saved = inventory_of(save);
    for (Loc& l : g_locs) {
        l.collected = false;
        l.inferred = false;
        for (const Flag& f : l.flags)
            if (flag_set(f, save, current)) {
                l.collected = true;
                break;
            }
        // A gift with no flag of its own (an NPC's, a shop's): when its item is the only one of its kind in the seed and
        // the save holds it, this is where it came from.
        if (!l.collected && l.flags.empty() && g_item_copies[l.item] == 1 && !g_starting.count(l.item) && saved.count(l.item)) {
            l.collected = true;
            l.inferred = true;
        }
        l.reachable = answered && reachable.count(l.name) != 0;
        // The item found there counts as the player's (this is how the dungeon keys are known: the save only holds the
        // ones not yet used). Never above what the save itself counts for the same item.
        if (l.collected)
            ++fromLocations[l.item];
    }
    // The log says what turned collected (the first look at a file is one line of the total).
    static std::vector<char> was;
    static std::string was_seed;
    if (was.size() != g_locs.size() || was_seed != g_seed + "#" + std::to_string(g_slot)) {
        was.assign(g_locs.size(), 0);
        was_seed = g_seed + "#" + std::to_string(g_slot);
        size_t n = 0;
        for (const Loc& l : g_locs)
            n += l.collected;
        std::fprintf(stderr, "[checktracker] file %d: %zu of %zu checks collected\n", g_slot, n, g_locs.size());
        for (size_t i = 0; i < g_locs.size(); ++i)
            was[i] = g_locs[i].collected;
    }
    for (size_t i = 0; i < g_locs.size(); ++i)
        if (g_locs[i].collected != bool(was[i])) {
            const bool team = g_locs[i].collected && bodplat::TickMs() - g_team_save_at.load() < 8000 && g_team_save_at.load() != 0;
            if (team) {
                g_locs[i].byTeam = true;
                save_skipped(g_slot);
            }
            std::fprintf(stderr, "[checktracker] %s: %s (%s)%s\n", g_locs[i].collected ? "collected" : "no longer collected",
                         g_locs[i].name.c_str(), g_locs[i].item.c_str(), team ? " by a teammate" : "");
            was[i] = g_locs[i].collected;
        }
    Counts have = saved;
    for (const auto& [name, n] : fromLocations)
        raise(have, name, n);
    picojson::object o;
    for (const auto& [name, n] : have)
        o[name] = picojson::value(int64_t(n));
    const std::string request = picojson::value(o).serialize();
    if (request != g_last_request) {
        g_last_request = request;
        g_helper.Ask(request);
    }
}

void draw_window(float scale, float fontScale) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float fx = CVarGetFloat(TRACKER_CVAR("Position.X"), 0.015f), fy = CVarGetFloat(TRACKER_CVAR("Position.Y"), 0.15f);
    const float fw = CVarGetFloat(TRACKER_CVAR("Size.W"), 0.26f), fh = CVarGetFloat(TRACKER_CVAR("Size.H"), 0.7f);
    static ImVec2 placed_for{};
    const bool resized = placed_for.x != display.x || placed_for.y != display.y;
    placed_for = display;
    const ImGuiCond cond = resized ? ImGuiCond_Always : ImGuiCond_Once;
    ImGui::SetNextWindowPos(ImVec2(fx * display.x, fy * display.y), cond);
    ImGui::SetNextWindowSize(ImVec2(fw * display.x, fh * display.y), cond);
    const CVarColor bg = CVarGetColor(TRACKER_CVAR("BgColor"), CVarColor{0, 0, 0, 200});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(bg.r / 255.f, bg.g / 255.f, bg.b / 255.f, bg.a / 255.f));
    bool open = true;
    ImGui::Begin("Check Tracker", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking);
    ImGui::SetWindowFontScale(fontScale * float(std::clamp(CVarGetInteger(TRACKER_CVAR("TextScale"), 100), 50, 200)) / 100.f);
    // The place and size are kept as fractions of the screen once the mouse is let go on the window.
    if (display.x > 0 && display.y > 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        if (std::fabs(pos.x / display.x - fx) > 1e-4f || std::fabs(pos.y / display.y - fy) > 1e-4f ||
            std::fabs(size.x / display.x - fw) > 1e-4f || std::fabs(size.y / display.y - fh) > 1e-4f) {
            CVarSetFloat(TRACKER_CVAR("Position.X"), pos.x / display.x);
            CVarSetFloat(TRACKER_CVAR("Position.Y"), pos.y / display.y);
            CVarSetFloat(TRACKER_CVAR("Size.W"), size.x / display.x);
            CVarSetFloat(TRACKER_CVAR("Size.H"), size.y / display.y);
            CVarSaveNextFrame();
        }
    }
    if (!open) {
        CVarSetInteger(kWindowOpen, 0);
        CVarSaveNextFrame();
    }

    if (!g_seed_error.empty())
        ImGui::TextWrapped("%s", g_seed_error.c_str());
    const std::string state = g_helper.State();
    if (state != "ready" && !state.empty())
        ImGui::TextDisabled("%s", state == "starting" ? "Working out what is reachable..." : state.c_str());

    const bool hideCollected = opt(TRACKER_CVAR("HideCollected")), onlyReachable = opt(TRACKER_CVAR("ShowOnlyReachable"));
    const bool showNonProgress = opt(TRACKER_CVAR("ShowNonProgress")), hideSkipped = opt(TRACKER_CVAR("HideSkipped"));
    const bool greenReachable = opt(TRACKER_CVAR("GreenReachable"), 1), spoiler = opt(TRACKER_CVAR("Spoiler"));
    const bool showHints = opt(TRACKER_CVAR("ShowHints"), 1);
    const bool showItems = opt(TRACKER_CVAR("ShowCollectedItems"), 1), openCurrent = opt(TRACKER_CVAR("OpenCurrentArea"), 1);

    // Totals over the locations the settings make part of the seed.
    int total = 0, collected = 0, reachable = 0;
    for (const Loc& l : g_locs) {
        if (!l.progress && !showNonProgress)
            continue;
        ++total;
        collected += l.collected;
        reachable += !l.collected && l.reachable;
    }
    ImGui::Text("Collected %d / %d   Reachable %d   Remaining %d", collected, total, reachable, total - collected);

    static char search[64] = "";
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##search", "Search checks", search, sizeof search);
    auto toggle = [](const char* label, const char* cvar) {
        bool v = opt(cvar);
        if (ImGui::Checkbox(label, &v)) {
            CVarSetInteger(cvar, v ? 1 : 0);
            CVarSaveNextFrame();
        }
    };
    toggle("Hide Collected", TRACKER_CVAR("HideCollected"));
    ImGui::SameLine();
    toggle("Only Reachable", TRACKER_CVAR("ShowOnlyReachable"));
    toggle("Non-Progress Locations", TRACKER_CVAR("ShowNonProgress"));
    ImGui::Separator();

    static std::string last_area;
    const std::string area_now = current_area();
    const bool areaChanged = openCurrent && area_now != last_area;
    last_area = area_now;

    ImGui::BeginChild("##checks", ImVec2(0, 0), false);
    for (const Area& a : g_areas) {
        int aTotal = 0, aCollected = 0, aReachable = 0;
        for (int i : a.locs) {
            const Loc& l = g_locs[i];
            if (!l.progress && !showNonProgress)
                continue;
            ++aTotal;
            aCollected += l.collected;
            aReachable += !l.collected && l.reachable;
        }
        if (aTotal == 0)
            continue;
        if (hideCollected && aCollected == aTotal)
            continue;
        const bool barren = g_barren.count(a.name) != 0;
        char header[160];
        std::snprintf(header, sizeof header, "%s%s  %d/%d%s###area_%s", a.name.c_str(), barren && showHints ? " [barren]" : "", aCollected, aTotal,
                      aReachable > 0 ? (std::string("  (") + std::to_string(aReachable) + " reachable)").c_str() : "", a.name.c_str());
        if (areaChanged && a.name == area_now)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_Text, aCollected == aTotal ? ImVec4(0.55f, 0.75f, 0.55f, 1.f) : ImVec4(1, 1, 1, 1));
        const bool openArea = ImGui::CollapsingHeader(header);
        ImGui::PopStyleColor();
        if (areaChanged && a.name == area_now)
            ImGui::SetScrollHereY(0.f);
        if (!openArea)
            continue;
        ImGui::Indent();
        for (int i : a.locs) {
            Loc& l = g_locs[i];
            if (!l.progress && !showNonProgress)
                continue;
            if (l.collected && hideCollected)
                continue;
            if (l.skipped && hideSkipped)
                continue;
            if (onlyReachable && (!l.reachable || l.collected))
                continue;
            if (!contains_nocase(l.name, search))
                continue;
            ImVec4 color;
            const char* mark = "";
            if (l.collected) {
                color = ImVec4(0.45f, 0.65f, 0.45f, 1.f);
                mark = "[x] ";
            } else if (l.skipped) {
                color = ImVec4(0.45f, 0.55f, 0.85f, 1.f);
                mark = "[-] ";
            } else if (l.reachable) {
                color = greenReachable ? ImVec4(0.55f, 1.f, 0.55f, 1.f) : ImVec4(1, 1, 1, 1);
            } else {
                color = ImVec4(0.6f, 0.6f, 0.6f, 1.f);
            }
            std::string text = std::string(mark) + l.rest;
            const bool hinted = !l.collected && showHints && !l.hint.empty();
            if (l.collected ? showItems : (spoiler || hinted))
                text += "  -> " + l.item + (hinted && !spoiler ? " (hinted)" : "");
            if (l.collected && l.byTeam)
                text += "  (teammate)";
            if (l.flags.empty() && !l.collected && !l.skipped)
                text += " *";
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Selectable((text + "##loc" + std::to_string(l.id)).c_str(), false);
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                l.skipped = !l.skipped;
                save_skipped(g_slot);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(l.name.c_str());
                if (!l.hint.empty())
                    ImGui::TextDisabled("%s", l.hint.c_str());
                if (l.byTeam)
                    ImGui::TextDisabled("A teammate collected this (their save came in through Anchor).");
                ImGui::TextDisabled("%s", l.inferred ? "Collected (judged from your items)" : l.collected ? "Collected" : l.skipped ? "Skipped" : l.reachable ? "Reachable now" : "Not reachable yet");
                if (l.flags.empty() && !l.collected)
                    ImGui::TextDisabled("The game keeps no flag for this one: mark it with a right click when done.");
                else
                    ImGui::TextDisabled("Right click: %s", l.skipped ? "no longer skip" : "skip this check");
                ImGui::EndTooltip();
            }
        }
        ImGui::Unindent();
    }
    if (!g_hints.empty() && ImGui::CollapsingHeader("Hints###hints")) {
        ImGui::Indent();
        for (const Hint& h : g_hints) {
            std::string line;
            if (h.kind == "barren")
                line = h.place + " is barren";
            else if (h.kind == "path")
                line = h.place + " is on the path to " + h.reward;
            else if (h.kind == "item")
                line = h.reward + " is in " + h.place + (h.importance.empty() ? "" : " (" + h.importance + ")");
            else
                line = h.place + " holds " + h.reward;
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::Unindent();
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleColor();
    (void)scale;
    (void)kWhite;
}

} // namespace

void NoteTeamSave() { g_team_save_at = bodplat::TickMs(); }

void Toggle() {
    const int now = CVarGetInteger(kWindowOpen, 0) ? 0 : 1;
    std::fprintf(stderr, "[checktracker] window %s\n", now ? "opened" : "closed");
    CVarSetInteger(kWindowOpen, now);
    CVarSaveNextFrame();
}

void Frame(float scale, float fontScale) {
    if (!CVarGetInteger(kWindowOpen, 0))
        return;
    // In game only: not on the title or the file select, nor before a save is in play.
    const BluewakeLocation where = bluewake_game_location();
    if (where.scene_proc != BLUEWAKE_SCENE_PLAY || where.stage[0] == '\0')
        return;
    if (!randoflow::Active() || randoflow::Seed().empty()) {
        g_helper.Stop();
        g_seed.clear();
        ImGui::SetNextWindowSize(ImVec2(300 * scale, 0), ImGuiCond_Once);
        bool open = true;
        ImGui::Begin("Check Tracker", &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(fontScale);
        ImGui::TextWrapped("The Check Tracker lists a randomizer seed's checks. This file is not a randomizer file.");
        ImGui::End();
        if (!open) {
            CVarSetInteger(kWindowOpen, 0);
            CVarSaveNextFrame();
        }
        return;
    }
    if (randoflow::Seed() != g_seed) {
        load_seed(randoflow::Seed());
        g_slot = -1;
        g_last_request.clear();
    }
    if (where.data_num != g_slot) {
        g_slot = where.data_num;
        load_skipped(g_slot);
        g_last_request.clear();
    }
    g_helper.Start(randoflow::SeedJson());
    static Save save;
    if (!bluewake_dev_read(kInfo, save.b, kSnapshot))
        return;
    update_state(save);
    draw_window(scale, fontScale);
}

void BuildMenu(shipmenu::Menu& m) {
    const char* H = "Randomizer";
    const char* S = "Check Tracker";
    m.AddSidebar(H, S, 1);
    m.AddWidget(H, S, 0, "Check Tracker", WidgetType::SeparatorText);
    m.AddWidget(H, S, 0, "Toggle Check Tracker Window", WidgetType::WindowButton)
        .CVar(kWindowOpen)
        .HideInSearch()
        .Tooltip("Toggles the Check Tracker: every check of the randomizer seed in play by area, collected or not, and "
                 "whether it can be reached with what you have. It shows while a randomizer file is in play.");
    m.AddWidget(H, S, 0, "Check Tracker Settings", WidgetType::SeparatorText);
    m.AddWidget(H, S, 0, "Background Color", WidgetType::CVarColorPicker)
        .CVar(TRACKER_CVAR("BgColor"))
        .DefaultColor(ImVec4(0.f, 0.f, 0.f, 200.f / 255.f))
        .UseAlpha()
        .Tooltip("The color and opacity behind the list.");
    m.AddWidget(H, S, 0, "Text size : %d%%", WidgetType::CVarSliderInt)
        .CVar(TRACKER_CVAR("TextScale"))
        .Range(50, 200)
        .DefaultValue(100);
    m.AddWidget(H, S, 0, "Hide Collected Checks", WidgetType::CVarCheckbox)
        .CVar(TRACKER_CVAR("HideCollected"))
        .Tooltip("Leaves the checks already collected (and areas that are all collected) out of the list.");
    m.AddWidget(H, S, 0, "Show Only Reachable Checks", WidgetType::CVarCheckbox)
        .CVar(TRACKER_CVAR("ShowOnlyReachable"))
        .Tooltip("Lists only the checks you can reach with what you have now.");
    m.AddWidget(H, S, 0, "Show Non-Progress Locations", WidgetType::CVarCheckbox)
        .CVar(TRACKER_CVAR("ShowNonProgress"))
        .Tooltip("Off: only the locations this seed's settings make progress locations. On: all of them.");
    m.AddWidget(H, S, 0, "Hide Skipped Checks", WidgetType::CVarCheckbox)
        .CVar(TRACKER_CVAR("HideSkipped"))
        .Tooltip("Right click a check in the window to skip it (a check you have decided not to do).");
    m.AddWidget(H, S, 0, "Tint Reachable Checks Green", WidgetType::CVarCheckbox)
        .CVar(TRACKER_CVAR("GreenReachable"))
        .DefaultValue(1);
    m.AddWidget(H, S, 0, "Show Items of Collected Checks", WidgetType::CVarCheckbox)
        .CVar(TRACKER_CVAR("ShowCollectedItems"))
        .DefaultValue(1);
    m.AddWidget(H, S, 0, "Spoiler Mode", WidgetType::CVarCheckbox)
        .CVar(TRACKER_CVAR("Spoiler"))
        .Tooltip("Shows the item of every check, collected or not. This spoils the seed.");
    m.AddWidget(H, S, 0, "Show Hinted Items and Barren Areas", WidgetType::CVarCheckbox)
        .CVar(TRACKER_CVAR("ShowHints"))
        .DefaultValue(1)
        .Tooltip("Shows the item of a check the seed's hints name, [barren] on areas a hint calls barren, and a Hints list. "
                 "The seed's hints are listed whether or not you have heard them in the game.");
    m.AddWidget(H, S, 0, "Open the Area Link Is In", WidgetType::CVarCheckbox)
        .CVar(TRACKER_CVAR("OpenCurrentArea"))
        .DefaultValue(1)
        .Tooltip("Opens the area's list when Link enters it.");
}

} // namespace checktracker
