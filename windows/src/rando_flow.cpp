// See rando_flow.h. SoH publishes no license; its file-select flow is the model (docs/fork/research/soh-randomizer.md 2.3,
// z_file_choose.c read as a reference), the code is this project's own. Seeds are made by the Wind Waker Randomizer
// (LagoLunatic, MIT).
#ifdef rename
#undef rename // bw_posix_compat.h's, which <filesystem> must not see
#endif

#include "rando_flow.h"

#include "fs_art.h"
#include "fs_blo.h"
#include "rando_pack.h"
#include "rando_saves.h"
#include "randomizer_menu.h"
#include "notifications.h"
#include "picojson.h"
#include "ship/cvar.h"

#include <imgui.h>

#include "platform/bod_platform.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "game_state.h"
#include "gxruntime/dvd.h"
#include "rando_seed.h"
#include "rando_ui.h"
#include "ui_pad.h"
void bw_settings_restart_now(void);
void bw_settings_open_menu_at(const char* header, const char* page);
bool bw_settings_menu_is_open(void);
// rando_host.c: whether the module can switch the randomizer on and off while the game runs, and does it.
bool bluewake_rando_inplace_supported(void);
bool bluewake_rando_inplace_ready(void);
bool bluewake_rando_inplace_can_leave(void);
void bluewake_rando_inplace_request(bool rando);
int bluewake_rando_inplace_poll(void);
}

namespace randoflow {
namespace {

namespace fs = std::filesystem;

constexpr unsigned kLeft = 0x0001, kRight = 0x0002, kDown = 0x0004, kUp = 0x0008, kA = 0x0100, kB = 0x0200,
                   kStart = 0x1000;

constexpr const char* kModeCVar = "gRandomizer.Mode";
constexpr const char* kSeedCVar = "gRandomizer.PlaySeed";
constexpr const char* kResumeCVar = "gRandomizer.ResumeAtFileSelect";
constexpr const char* kLogoDisc = "res/Object/TlogoE.arc";

// What the file select's text says (SoH's English lines; FileSelectEnhancements.cpp).
constexpr const char* kStartText = "Start Randomizer";
constexpr const char* kGenerateText = "Generate New Randomizer Seed";
constexpr const char* kSettingsText = "Open Randomizer Settings";
constexpr const char* kGeneratingText = "Generating...";
constexpr const char* kNoSeedText = "No randomizer seed loaded.\nPlease generate one first,\nor drop a seed file on the game window.";

std::string g_dir;            // the data folder, with a trailing backslash
std::atomic<bool> g_active{false};  // this session runs the randomizer
bool g_resume;                // return to the file select by itself (after a restart)
BlueWakeRandoPack g_pack;     // the seed the session plays (when g_active)
std::string g_seed;           // its folder's name ("" if none)
BlueWakeRandoPack g_ready;    // the seed Start Randomizer would play: the one just made, dropped or last used
std::string g_ready_seed;
randosaves::Sidecar g_sidecar;
std::vector<std::string> g_mounted;  // the DVD overlay paths this session's seed put in place
std::string g_notice;         // a line over the screen's bottom ("" none)
unsigned g_notice_frames = 0;

fs::path root() {
    // BLUEWAKE_RANDOMIZER_DIR (testing): another folder of seed packs in place of Randomizer\.
    if (const char* env = std::getenv("BLUEWAKE_RANDOMIZER_DIR"); env != nullptr && env[0] != '\0')
        return fs::u8path(env);
    return fs::u8path(g_dir) / "Randomizer";
}
fs::path sidecar_path() {
    // Beside the card it belongs to (the testing card override moves both).
    if (const char* env = std::getenv("BLUEWAKE_CARD_PATH"); env != nullptr && env[0] != 0) {
        fs::path p = fs::u8path(env);
        p.replace_extension(".slots.json");
        return p;
    }
    return fs::u8path(g_dir) / "Save" / "Randomizer.slots.json";
}
std::string utf8(const fs::path& p) {
    const auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

// No quest select: BLUEWAKE_NO_FILE_SELECT_UI=1, and every run that scripts the new game's pad presses itself (the training
// playbacks of a build, the scenario runner's "boot"), whose A on the file must reach the game.
bool ui_off() {
    return std::getenv("BLUEWAKE_NO_FILE_SELECT_UI") != nullptr || std::getenv("BLUEWAKE_PAD_PULSE_ON_TITLE_READY") != nullptr;
}

void notice(const std::string& text) {
    g_notice = text;
    g_notice_frames = 420;
    std::fprintf(stderr, "[randomizer] %s\n", text.c_str());
}

// --- the session ----------------------------------------------------------------------------------------------

int g_restart_in = 0; // frames until restart_into restarts the game (it says so first)
void toast(const std::string& message);

void restart_into(int mode, const std::string& seed) {
    CVarSetInteger(kModeCVar, mode);
    if (!seed.empty())
        CVarSetString(kSeedCVar, seed.c_str());
    CVarSetInteger(kResumeCVar, 1);
    CVarSave();
    std::fprintf(stderr, "[randomizer] restarting into %s%s%s\n", mode ? "Randomizer" : "Vanilla", seed.empty() ? "" : ", seed ",
                 seed.c_str());
    // Said first, for a second, then done (Frame): the window closing at once looked like a crash.
    notice(std::string("Restarting the game into ") + (mode ? "the Randomizer" : "Vanilla") + "...");
    toast(std::string("Restarting the game into ") + (mode ? "the Randomizer" : "Vanilla") + "...");
    g_restart_in = 60;
}

bool load_pack(const std::string& seed, BlueWakeRandoPack* out, std::string* error) {
    return !seed.empty() && randopack::Load(root() / fs::u8path(seed) / "seed.json", out, error);
}

// The seed's own files (every stage, event and text file wwrando changes, the banner, the new archives) in the DVD
// overlay, and wwrando's title logo. What goes in is remembered so a switch back to vanilla takes it out again.
void mount_seed(const std::string& seed) {
    if (seed.empty())
        return;
    const fs::path files = root() / fs::u8path(seed) / "overlay" / "files";
    std::error_code ec;
    int ok = 0, failed = 0;
    for (fs::recursive_directory_iterator it(files, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const fs::path rel = fs::relative(it->path(), files, ec);
        const std::string disc = utf8(rel);
        if (disc.rfind("rels/", 0) == 0)
            continue;  // the modules are the mod's (game\rando\rels), not the DVD's
        std::ifstream file(it->path(), std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!data.empty() && dvd_overlay_set(disc.c_str(), data.data(), static_cast<u32>(data.size()))) {
            ++ok;
            g_mounted.push_back(disc);
        } else {
            ++failed;
            std::fprintf(stderr, "[randomizer] seed file %s: not applied (not on the disc, or unreadable)\n", disc.c_str());
        }
    }
    std::fprintf(stderr, "[randomizer] seed files: %d in the DVD overlay, %d not applied\n", ok, failed);
    // wwrando's title logo, kept in Randomizer\logo for the next time.
    const fs::path cache = root() / "logo" / "TlogoE.arc";
    const fs::path fresh = files / "res" / "Object" / "TlogoE.arc";
    if (fs::exists(fresh, ec)) {
        fs::create_directories(cache.parent_path(), ec);
        fs::copy_file(fresh, cache, fs::copy_options::overwrite_existing, ec);
    }
    std::ifstream in(fs::exists(fresh, ec) ? fresh : cache, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        std::fprintf(stderr, "[randomizer] no title logo yet (it is made with the first seed)\n");
        return;
    }
    const bool logo = dvd_overlay_set(kLogoDisc, bytes.data(), static_cast<u32>(bytes.size()));
    if (logo)
        g_mounted.push_back(kLogoDisc);
    std::fprintf(stderr, "[randomizer] title logo %s (%zu bytes) %s\n", kLogoDisc, bytes.size(), logo ? "ok" : "not applied");
}

void unmount_seed() {
    for (const std::string& path : g_mounted)
        dvd_overlay_clear(path.c_str());
    g_mounted.clear();
}

void set_ready(const std::string& seed, const BlueWakeRandoPack& pack) {
    g_ready = pack;
    g_ready_seed = seed;
}

// What follows a switch of the session: the new file's hand-off to the game, or the existing file's.
enum After { AfterNewVanilla = 1, AfterNewRando, AfterExisting };
struct Pending {
    bool active = false;
    bool rando = false;
    std::string seed;
    BlueWakeRandoPack pack;
    int after = 0;
    unsigned frames = 0;
};
Pending g_pending;
bool g_played;  // a stage has been played in this process
bool g_music_gen;          // the pirates' theme is playing for a generation
int g_pirate_in = 0;       // frames until the pirates' theme is started (the pirate ship's wave banks load first)
void continue_after(int after);

// A switch waiting for the file select's hook (switch_then), tried again each frame.
struct SwitchWait {
    bool active = false;
    bool rando = false;
    std::string seed;
    int after = 0;
    int frames = 0;
};
SwitchWait g_switch_wait;


// Puts the session in the mode (and, for the randomizer, on the seed) and then does `after`. In place when the module can:
// the seed's files and pack are set here, the game switches at its next frame, and Frame finishes the job when it has
// (finish_switch). Otherwise the game is restarted into it and this returns (the process is going away).
void switch_then(bool rando, const std::string& seed, int after) {
    if (rando == g_active.load() && (!rando || seed == g_seed)) {
        continue_after(after);
        return;
    }
    // In place when the module can. (After a played stage this crashed until the host rebuilt the game's dynamic module controls
    // for the code now in force: the randomizer's cDyl code has its own DMC array, empty after a switch, and no REL loaded.)
    // Leaving the randomizer in place needs the module's trampolines; without them the game restarts into vanilla.
    // The module can switch but the file select's hook has not run yet (the first moments of the file select, as when a
    // relaunch resumes into it): wait for it, a few seconds at most, rather than restart (the restart looked like a crash,
    // user 2026-10-17 16:40).
    if (bluewake_rando_inplace_supported() && !bluewake_rando_ui_hook_ran() && g_switch_wait.frames < 300 &&
        !(!rando && g_active.load() && !bluewake_rando_inplace_can_leave())) {
        g_switch_wait = SwitchWait{true, rando, seed, after, g_switch_wait.frames};
        return;
    }
    g_switch_wait = SwitchWait{};
    if (!bluewake_rando_inplace_ready() || (!rando && g_active.load() && !bluewake_rando_inplace_can_leave())) {
        restart_into(rando ? 1 : 0, seed);
        return;
    }
    BlueWakeRandoPack pack;
    std::string error;
    if (rando && !load_pack(seed, &pack, &error)) {
        notice("That seed cannot be loaded: " + error);
        return;
    }
    unmount_seed();
    if (rando) {
        mount_seed(seed);
        bluewake_rando_set_pack(&pack);
    }
    g_pending = Pending{true, rando, seed, rando ? pack : BlueWakeRandoPack{}, after, 0};
    bluewake_ui_pad_set_focus(true);
    bluewake_rando_inplace_request(rando);
}

// --- the file select -----------------------------------------------------------------------------------------

enum class Step { Idle, Quest, Menu, Hand };

struct Ui {
    Step step = Step::Idle;
    int slot = 0;            // the file the player is making (0 to 2)
    int quest = 0;           // the quest the selector shows
    int row = 0;             // the randomizer menu's line
    unsigned frames = 0;     // in this step
    float alpha = 0.f;       // fade of the step's contents
    bool want_rando_new = false;  // a new randomizer file is being made on `slot` with g_ready
    // Hand-off to the game's own menu: one A to open the file's menu, a second on Start (a new file).
    int hand_phase = 0;
    unsigned hand_wait = 0;
    bool hand_confirm_start = false;
    int generating_prev = 0;  // the generator's state at the last frame
};
Ui g_ui;
std::atomic<int> g_ui_step{0};            // Step as an int, read by the pad gate
std::atomic<int> g_request{0};            // the pad gate's request: 1 a new file's A, 2 an existing file of the other quest
std::atomic<int> g_request_slot{0};
std::atomic<int> g_slot_quest[3];         // per file: 0 vanilla, 1 randomizer, 2 a randomizer file of another build
std::atomic<int> g_slot_here[3];          // per file: the session can play it as it is (same quest, same seed)
std::mutex g_drop_mutex;
std::string g_dropped;
std::string g_picked;  // Load Seed File's choice, waiting for the file select

// The file naming (the name entry) a new file in progress: whose seed it is.
struct Naming {
    bool active = false;
    int slot = 0;
    bool rando = false;
};
Naming g_naming;

void set_step(Step s) {
    g_ui.step = s;
    g_ui.frames = 0;
    g_ui_step = static_cast<int>(s);
}

void sfx(unsigned id) { bluewake_rando_ui_sfx(id); }

// The pad gate, on the game thread: the A that would open a file in the list. A new file's A is kept for the quest
// select; an existing file of the other quest (or another seed) is kept while the session is switched.
bool rebuild_running();

bool pad_gate(unsigned edges, void*) {
    if ((edges & (kA | kStart)) == 0u || g_ui_step.load() != 0 || g_request.load() != 0)
        return false;
    const BluewakeLocation loc = bluewake_game_location();
    if (loc.scene_proc != BLUEWAKE_SCENE_NAME || loc.fs_main != 4 || loc.fs_sub != 0 || loc.fs_end != 0)
        return false;
    const int slot = loc.fs_slot;
    if (slot < 0 || slot > 2)
        return false;
    // A seed dropped on the window is still being made from the disc: no file opens until it is the session's (one
    // opened meanwhile went in with the seed before it and was recorded wrong, user 2026-10-17 13:57).
    if (rebuild_running())
        return true;
    if (loc.fs_new[slot]) {
        g_request_slot = slot;
        g_request = 1;
        return true;
    }
    if (g_slot_quest[slot].load() != 0 && g_slot_here[slot].load() == 0) {
        g_request_slot = slot;
        g_request = 2;
        return true;
    }
    if (g_slot_quest[slot].load() == 0 && g_active.load()) {
        g_request_slot = slot;
        g_request = 2;
        return true;
    }
    return false;
}

void refresh_slots() {
    randosaves::Sidecar fresh;
    if (randosaves::Load(sidecar_path(), &fresh))
        g_sidecar = fresh;
    for (int i = 0; i < 3; ++i) {
        const auto it = g_sidecar.slots.find(i + 1);
        int quest = 0, here = 1;
        if (it != g_sidecar.slots.end()) {
            quest = it->second.build == randosaves::kBuild ? 1 : 2;
            here = quest == 1 && g_active.load() && it->second.seed == g_seed ? 1 : 0;
        } else {
            here = g_active.load() ? 0 : 1;
        }
        g_slot_quest[i] = quest;
        g_slot_here[i] = here;
    }
}

void save_sidecar() { randosaves::Save(sidecar_path(), g_sidecar); }

// Takes the controls: the game is deaf to the pad until give_back.
void take_pad() { bluewake_ui_pad_set_focus(true); }

// --- hash ---------------------------------------------------------------------------------------------------

std::vector<std::string> hash_words(const std::string& hash) {
    std::vector<std::string> words;
    size_t start = 0;
    while (start < hash.size()) {
        size_t end = hash.find(' ', start);
        if (end == std::string::npos)
            end = hash.size();
        if (end > start)
            words.push_back(hash.substr(start, end - start));
        start = end + 1;
    }
    return words;
}

// --- drawing ------------------------------------------------------------------------------------------------

// The file select is laid out in a 640 by 480 box, scaled to the window's height and centred.
using Space = fsblo::View;
Space space() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    Space sp;
    sp.s = std::min(size.x / 640.f, size.y / 480.f);
    sp.ox = (size.x - 640.f * sp.s) * 0.5f;
    sp.oy = (size.y - 480.f * sp.s) * 0.5f;
    return sp;
}

ImU32 col(int r, int g, int b, float a = 1.f) { return IM_COL32(r, g, b, static_cast<int>(255.f * std::clamp(a, 0.f, 1.f))); }

// Text with the game's outline: a dark ring round the letters. In the game's own font once the disc's font is read (the
// lettering of the file select), in the app's font until then.
constexpr float kGameFontScale = 1.18f;  // the game's line is taller than the size the layout was drawn with

float text_width(const Space& sp, float size, const char* s) {
    if (fsart::FontReady())
        return fsart::TextWidth(size * kGameFontScale * sp.s, s) / sp.s;
    return ImGui::GetFont()->CalcTextSizeA(size * sp.s, FLT_MAX, 0.f, s).x / sp.s;
}

void text(ImDrawList* dl, const Space& sp, float size, float x, float y, ImU32 fill, ImU32 outline, const char* s, bool centred = false) {
    const bool game = fsart::FontReady();
    const float px = size * (game ? kGameFontScale : 1.f) * sp.s;
    ImVec2 pos = sp.at(x, y);
    if (centred)
        pos.x -= text_width(sp, size, s) * sp.s * 0.5f;
    const float r = std::max(1.f, px * (game ? 0.06f : 0.07f));
    static const float d[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-.7f, -.7f}, {.7f, -.7f}, {-.7f, .7f}, {.7f, .7f}};
    if (game) {
        for (const auto& o : d)
            fsart::DrawString(dl, ImVec2(pos.x + o[0] * r, pos.y + o[1] * r), px, outline, s);
        fsart::DrawString(dl, pos, px, fill, s);
        return;
    }
    ImFont* font = ImGui::GetFont();
    // The app's font has an atlas of its own: the foreground list draws with ImGui's, so the text says which it uses.
    dl->PushTextureID(font->ContainerAtlas->TexID);
    for (const auto& o : d)
        dl->AddText(font, px, ImVec2(pos.x + o[0] * r, pos.y + o[1] * r), outline, s);
    dl->AddText(font, px, pos, fill, s);
    dl->PopTextureID();
}

// --- the file select's own panes ------------------------------------------------------------------------------------------
// The quest select and the randomizer menu are made of the file select's own panes (file_select.blo, read from the disc: fs_blo.h)
// at the sizes and places the game gives them: the title strip (exk0, ex00), the message window (ntk0, nt00) and the row of
// nameplates (Start, Copy, Erase, Return), over the game's own board, which stays; the game's list, strip and buttons are hidden
// while these are up (bluewake_fs_cover) and its corner prompts ("Choose", "Return") are the game's own. The colours are the
// blo's, and the greyed plate is the one the game gives a Copy it cannot do (d_file_select.cpp menuColorChange).
using fsblo::Tag;

fsblo::Pose pose(float a) {
    fsblo::Pose p;
    p.alpha = a;
    // The panes that are shown by fading in start from a low alpha in the blo; at rest the game has them opaque.
    static const uint32_t kBodies[] = {Tag("nt00"), Tag("rc00"), Tag("a000"), Tag("1000"), Tag("2000"), Tag("3000"), Tag("ex00"),
                                       Tag("s000"), Tag("cp00"), Tag("dl00"), Tag("re00"), Tag("no00"), Tag("ys00")};
    for (const uint32_t t : kBodies)
        p.alpha_of[t] = 255;
    return p;
}

// The title strip, with its words.
void title_strip(ImDrawList* dl, const Space& sp, const char* title, float a) {
    const fsblo::Screen& s = fsblo::FileSelect();
    if (!s.ready())
        return;
    // exk0 is the strip's shadow with the strip itself (ex00) under it, as ntk0 is for the message window.
    fsblo::Pose p = pose(a);
    p.skip.insert(Tag("exp2"));
    p.text[Tag("expl")] = title;
    fsblo::Draw(dl, sp, s, Tag("exk0"), p);
}

// The message window, at the size the game gives it: shadow, torn strips, paper. Returns the paper's middle.
ImVec2 message_window(ImDrawList* dl, const Space& sp, float a) {
    if (fsblo::FileSelect().ready())
        fsblo::WindowWithShadow(dl, sp, 47.f, 154.f, 541.f, 194.f, 0xFFC83C00u, 0xFFE696FFu, a);
    return ImVec2(47.f + 541.f * 0.5f, 154.f + 194.f * 0.5f);
}

// Words in the message window's box (the game's white message text), one line per '\n'.
void message_text(ImDrawList* dl, const Space& sp, const std::string& text, float x, float y, float w, float h, int hbind, float a,
                  ImU32 top = 0, ImU32 bottom = 0) {
    const fsblo::Screen& s = fsblo::FileSelect();
    const int i = s.find(Tag("mes1"));
    if (i < 0)
        return;
    fsblo::Node box = s.nodes[size_t(i)];
    box.w = w;
    box.h = h;
    box.hbind = hbind;
    fsblo::PutText(dl, sp, box, x, y, text, a, top, bottom);
}

// A nameplate of the row (slot 0 to 3: Start, Copy, Erase, Return), moved to x, with its words. `lit` shows the cursor on it,
// `grey` is the game's plate for a thing that cannot be done.
void nameplate(ImDrawList* dl, const Space& sp, int slot, float x, const char* words, bool lit, bool grey, float a, int size = 0) {
    static const uint32_t kText[4] = {Tag("stat"), Tag("copy"), Tag("dlle"), Tag("retu")};
    const fsblo::Screen& s = fsblo::FileSelect();
    if (!s.ready())
        return;
    // The plate is the game's item_nameplate (88 x 48 texels a half: the left half, and the same turned for the right) at the
    // game's width of 120 units, and as tall as that width gives it at the texture's own proportions (the game's 60 x 64 a
    // half stretched it): 33 units, centred on the 64-unit slot.
    constexpr float kHalf = 60.f, kTall = 48.f * (kHalf / 88.f);
    const float bx = x - 10.f, by = 388.f + (64.f - kTall) * 0.5f;
    const uint32_t black = grey ? 0x5A5A5A00u : 0x286E1E00u, white = grey ? 0xC8C8C8FFu : 0xB0FFA6FFu;
    fsblo::Picture(dl, sp, "item_nameplate.bti", bx + 10.f, by + 10.f, kHalf, kTall, 0, 0x00000000u, 0x000000FFu, a * 50.f / 255.f);
    fsblo::Picture(dl, sp, "item_nameplate.bti", bx + 10.f + kHalf, by + 10.f, kHalf, kTall, 3, 0x00000000u, 0x000000FFu, a * 50.f / 255.f);
    fsblo::Picture(dl, sp, "item_nameplate.bti", bx, by, kHalf, kTall, 0, black, white, a);
    fsblo::Picture(dl, sp, "item_nameplate.bti", bx + kHalf, by, kHalf, kTall, 3, black, white, a);
    if (lit) {
        // The cursor is the same plate again in white, in the same two rectangles, its alpha pulsing as the game's cursor does.
        const float pulse = 0.5f + 0.5f * std::sin(float(g_ui.frames) * 0.12f);
        const float ca = a * (30.f + 70.f * pulse) / 255.f;
        fsblo::Picture(dl, sp, "item_nameplate.bti", bx, by, kHalf, kTall, 0, 0xFFFFFF00u, 0xFFFFFFFFu, ca);
        fsblo::Picture(dl, sp, "item_nameplate.bti", bx + kHalf, by, kHalf, kTall, 3, 0xFFFFFF00u, 0xFFFFFFFFu, ca);
    }
    // The words, in the game's text box for this plate (110 x 31 at the plate's 5, 17), the 24 point text smaller if the plate
    // is not wide enough for them; centred on the plate.
    const int ti = s.find(kText[slot]);
    if (ti >= 0) {
        fsblo::Node box = s.nodes[size_t(ti)];
        const float width = fsblo::TextWidth(box, words);
        if (size > 0 || width > 104.f)
            box.font_x = box.font_y = size > 0 ? size : std::max(14, int(24.f * 104.f / width));
        box.vbind = 0;
        box.h = kTall;
        fsblo::PutText(dl, sp, box, bx + 5.f, by, words, a);
    }
}

void draw_hash(ImDrawList* dl, const Space& sp, const std::string& hash, float x, float y, float size, float a, float gap = 6.f) {
    float at = x;
    // Each icon on the translucent dark disc the file select puts under its Triforce (ci02, ba_base_00).
    const fsart::Image disc = fsart::Pic("ba_base_00.bti", 0x00000000u, 0x000000FFu);
    for (const std::string& word : hash_words(hash)) {
        const fsart::Image icon = fsart::HashIcon(word);
        const float pad = size * 0.22f;
        if (disc.tex != 0)
            dl->AddImage(disc.tex, sp.at(at - pad, y - pad), sp.at(at + size + pad, y + size + pad), ImVec2(0, 0), ImVec2(1, 1),
                         col(255, 255, 255, 0.45f * a));
        if (icon.tex != 0)
            dl->AddImage(icon.tex, sp.at(at, y), sp.at(at + size, y + size), ImVec2(0, 0), ImVec2(1, 1), col(255, 255, 255, a));
        at += size + gap;
    }
}

// The title logo of a quest, fitted into a box (the game's own picture, the aspect kept).
void draw_logo(ImDrawList* dl, const Space& sp, int quest, float cx, float top, float max_w, float max_h, float a) {
    const fsart::Logo& logo = fsart::GetLogo(quest);
    if (!logo.ready())
        return;
    const float rm = float(logo.main.h) / float(logo.main.w), rs = float(logo.sub.h) / float(logo.sub.w);
    // the subtitle sits under the title at 78 percent of its width, tucked 4 pixels in
    float width = max_w;
    if (width * (rm + 0.78f * rs) - 4.f > max_h)
        width = (max_h + 4.f) / (rm + 0.78f * rs);
    const float mw = width, mh = width * rm, sw = width * 0.78f, sh = sw * rs;
    const float total = mh + sh - 4.f;
    const float y = top + (max_h - total) * 0.5f;
    dl->AddImage(logo.main.tex, sp.at(cx - mw / 2, y), sp.at(cx + mw / 2, y + mh), ImVec2(0, 0), ImVec2(1, 1), col(255, 255, 255, a));
    dl->AddImage(logo.sub.tex, sp.at(cx - sw / 2, y + mh - 4), sp.at(cx + sw / 2, y + mh - 4 + sh), ImVec2(0, 0), ImVec2(1, 1), col(255, 255, 255, a));
}

// The corner's prompts, as the game's corner shows them (main_parts_fileselect.blo): "Choose" on the green button and, on a screen
// that has a way back, "Return" on the red one. The game draws these under the screens' own panes; these screens draw the strip
// over that corner, so they draw the prompts again over the strip, as the game's own list does.
void corner_prompts(ImDrawList* dl, const Space& sp, float a) {
    const fsblo::Screen& s = fsblo::MainParts();
    if (!s.ready())
        return;
    fsblo::Pose p;
    p.alpha = a;
    fsblo::Draw(dl, sp, s, Tag("cent"), p);
    fsblo::Draw(dl, sp, s, Tag("bawp"), p);
    fsblo::Draw(dl, sp, s, Tag("baat"), p);
    fsblo::Draw(dl, sp, s, 0x00626162u /* the glyph's pane is named NUL, b, a, b */, p);
    fsblo::Draw(dl, sp, s, Tag("bawd"), p);
}

// "Select a Quest": the strip, the message window with the chosen quest's logo, and two nameplates, Vanilla and Randomizer.
void draw_quest(ImDrawList* dl, const Space& sp, float a) {
    title_strip(dl, sp, "Select a Quest", a);
    corner_prompts(dl, sp, a);
    const ImVec2 mid = message_window(dl, sp, a);
    draw_logo(dl, sp, g_ui.quest, mid.x, 154.f + 22.f, 360.f, 194.f - 44.f, a);
    const float x0 = mid.x - 125.5f;
    nameplate(dl, sp, 1, x0, "Vanilla", g_ui.quest == 0, false, a, 19);
    nameplate(dl, sp, 2, x0 + 131.f, "Randomizer", g_ui.quest == 1, false, a, 19);
}

// "Randomizer": the strip, the message window (the seed, or what is missing), and Start, Generate, Settings, Return.
void draw_menu(ImDrawList* dl, const Space& sp, float a, bool generating, bool have_seed) {
    title_strip(dl, sp, "Randomizer", a);
    corner_prompts(dl, sp, a);
    const ImVec2 mid = message_window(dl, sp, a);
    constexpr float kTextX = 47.f + 29.f, kWide = 541.f - 58.f;
    const ImU32 red_top = col(176, 36, 28, a), red_bottom = col(150, 24, 20, a);  // SoH's red, dark enough to read on the paper
    if (generating) {
        message_text(dl, sp, kGeneratingText, kTextX, 154.f + 40.f, kWide, 114.f, 0, a);
    } else if (!have_seed && randomizer::LastOutcome() != 2) {
        // SoH's three lines
        message_text(dl, sp, kNoSeedText, kTextX, 154.f + 30.f, kWide, 134.f, 0, a, red_top, red_bottom);
    } else {
        message_text(dl, sp, "Seed " + g_ready_seed, kTextX, 154.f + 36.f, kWide, 40.f, 0, a);
        const size_t n = hash_words(g_ready.seed_hash).size();
        draw_hash(dl, sp, g_ready.seed_hash, mid.x - (float(n) * 50.f - 14.f) * 0.5f, 154.f + 100.f, 36.f, a, 14.f);
    }
    if (!generating && randomizer::LastOutcome() == 2) {
        // The generator's own words, broken into lines that fit the window.
        const std::string r = randomizer::LastResult();
        const fsblo::Screen& fs = fsblo::FileSelect();
        const int bi = fs.find(Tag("mes1"));
        std::string wrapped, line;
        size_t at = 0;
        while (bi >= 0 && at < r.size()) {
            size_t end = r.find(' ', at);
            if (end == std::string::npos)
                end = r.size();
            const std::string word = r.substr(at, end - at);
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && fsblo::TextWidth(fs.nodes[size_t(bi)], trial) > kWide) {
                wrapped += line + "\n";
                line = word;
            } else {
                line = trial;
            }
            at = end + 1;
        }
        wrapped += line;
        message_text(dl, sp, wrapped, kTextX, 154.f + 30.f, kWide, 134.f, 0, a, red_top, red_bottom);
    }
    const bool grey_start = !have_seed || generating;
    nameplate(dl, sp, 0, 86.f, "Start", g_ui.row == 0, grey_start, a);
    nameplate(dl, sp, 1, 217.f, "Generate", g_ui.row == 1, generating, a);
    nameplate(dl, sp, 2, 348.f, "Settings", g_ui.row == 2, generating, a);
    nameplate(dl, sp, 3, 480.f, "Return", g_ui.row == 3 && !generating, generating, a);
}

// What the file select shows over its own list: the hash of a randomizer file under the cursor (as SoH draws it) and,
// under each randomizer file's record, its quest.
void draw_list_extras(ImDrawList* dl, const Space& sp, const BluewakeLocation& loc) {
    // Each randomizer file's record (the green pane): its quest and seed hash on the name's line, at the pane's right.
    static const float kPaneTop[3] = {148.f, 225.f, 301.f};
    constexpr float kPaneRight = 551.f;
    for (int i = 0; i < 3; ++i) {
        const auto it = g_sidecar.slots.find(i + 1);
        if (it == g_sidecar.slots.end() || loc.fs_new[i])
            continue;
        const auto words = hash_words(it->second.hash);
        const float icons = 16.f, gap = 3.f;
        const float width = words.size() * (icons + gap);
        const float x = kPaneRight - 8.f - width;
        draw_hash(dl, sp, it->second.hash, x, kPaneTop[i] + 5.f, icons, 1.f, gap);
        const char* label = it->second.build == randosaves::kBuild ? "Randomizer" : "Randomizer (other version)";
        text(dl, sp, 13.f, x - 8.f - text_width(sp, 13.f, label), kPaneTop[i] + 5.f, col(255, 244, 170), col(50, 60, 20), label);
    }
    const auto sel = g_sidecar.slots.find(loc.fs_slot + 1);
    if (loc.fs_slot >= 0 && loc.fs_slot < 3 && !loc.fs_new[loc.fs_slot] && sel != g_sidecar.slots.end())
        draw_hash(dl, sp, sel->second.hash, 64, 8, 24.f, 1.f);
}

// Words in one line of the game's message font, in a box of a size (the file select's 'mes1' text box, resized): left, centre or
// right (hbind 2, 0, 1).
void box_text(ImDrawList* dl, const Space& sp, float x, float y, float w, float h, int hbind, float size, const std::string& words, float a) {
    const fsblo::Screen& s = fsblo::FileSelect();
    const int i = s.find(Tag("mes1"));
    if (i < 0)
        return;
    fsblo::Node box = s.nodes[size_t(i)];
    box.w = w;
    box.h = h;
    box.hbind = hbind;
    box.vbind = 0;
    box.font_x = box.font_y = int(size);
    fsblo::PutText(dl, sp, box, x, y, words, a);
}

float box_text_width(float size, const std::string& words) {
    const fsblo::Screen& s = fsblo::FileSelect();
    const int i = s.find(Tag("mes1"));
    if (i < 0)
        return 0.f;
    fsblo::Node box = s.nodes[size_t(i)];
    box.font_x = box.font_y = int(size);
    return fsblo::TextWidth(box, words);
}

// The name entry: the seed's hash and its name, in a small message window of the file select's kind at the top left.
void draw_naming(ImDrawList* dl, const Space& sp) {
    if (!g_naming.rando || !g_ready.loaded || !fsblo::FileSelect().ready())
        return;
    const size_t n = hash_words(g_ready.seed_hash).size();
    const std::string label = "Seed";
    const float icons = 24.f, gap = 6.f;
    const float width = 16.f + 14.f + float(n) * (icons + gap) + box_text_width(20.f, label) + 14.f + 16.f;
    fsblo::Window(dl, sp, 40.f, 6.f, width, 48.f, 0xFFC83C00u, 0xFFE696FFu, 1.f);
    draw_hash(dl, sp, g_ready.seed_hash, 40.f + 30.f, 18.f, icons, 1.f, gap);
    box_text(dl, sp, 40.f + 30.f + float(n) * (icons + gap) + 4.f, 14.f, box_text_width(20.f, label) + 4.f, 28.f, 2, 20.f, label, 1.f);
}

// A line over the screen's bottom (what could not be done): the game's message window with the words in it.
void draw_notice(ImDrawList* dl, const Space& sp) {
    if (g_notice.empty() || !fsblo::FileSelect().ready())
        return;
    const float a = std::min(1.f, g_notice_frames / 60.f);
    const float w = std::min(580.f, box_text_width(18.f, g_notice) + 16.f + 28.f);
    fsblo::Window(dl, sp, 320.f - w / 2, 428.f, w, 44.f, 0xFFC83C00u, 0xFFE696FFu, a);
    box_text(dl, sp, 320.f - w / 2, 436.f, w, 28.f, 0, 18.f, g_notice, a);
}

// --- the flow -------------------------------------------------------------------------------------------------

void load_ready_from_cvar() {
    std::string seed = CVarGetString(kSeedCVar, "");
    if (seed.empty())
        seed = CVarGetString("gRandomizer.LastSeed", "");
    std::string error;
    BlueWakeRandoPack pack;
    if (load_pack(seed, &pack, &error))
        set_ready(seed, pack);
}

// The player has chosen a quest for a new file: on to the game's own menu and name entry.
void hand_to_game(bool confirm_start) {
    set_step(Step::Hand);
    g_ui.hand_phase = 0;
    g_ui.hand_wait = 0;
    g_ui.hand_confirm_start = confirm_start;
    bluewake_ui_pad_set_focus(false);
}

void start_new_file(bool rando) {
    g_naming.active = true;
    g_naming.slot = g_ui.slot;
    g_naming.rando = rando;
    sfx(BLUEWAKE_SE_DECIDE);
    hand_to_game(true);
}

void continue_after(int after) {
    switch (after) {
    case AfterNewVanilla:
        start_new_file(false);
        break;
    case AfterNewRando:
        start_new_file(true);
        break;
    default:
        refresh_slots();
        g_naming.active = false;
        hand_to_game(false);
        break;
    }
}

// The game has switched (or not): the session now is what the player chose, and what they were doing goes on.
void poll_switch() {
    if (!g_pending.active)
        return;
    ++g_pending.frames;
    const int result = bluewake_rando_inplace_poll();
    if (result == 0 && g_pending.frames < 900)
        return;
    const Pending done = g_pending;
    g_pending.active = false;
    if (result != 1) {
        notice("The game could not switch quests while it runs.");
        bluewake_ui_pad_set_focus(false);
        set_step(Step::Idle);
        return;
    }
    g_pack = done.pack;
    g_seed = done.rando ? done.seed : std::string();
    g_active = done.rando;
    std::fprintf(stderr, "[randomizer] session is now %s%s%s\n", done.rando ? "Randomizer, seed " : "Vanilla", g_seed.c_str(),
                 done.rando ? (std::string(" (") + g_pack.seed_hash + ")").c_str() : "");
    refresh_slots();
    continue_after(done.after);
}

void confirm_quest() {
    if (g_ui.quest == 0) {
        switch_then(false, "", AfterNewVanilla);
        return;
    }
    sfx(BLUEWAKE_SE_DECIDE);
    g_ui.row = 0;
    set_step(Step::Menu);
}

void start_randomizer() {
    if (!g_ready.loaded) {
        sfx(BLUEWAKE_SE_ERROR);
        return;
    }
    switch_then(true, g_ready_seed, AfterNewRando);
}

void on_generation_done(bool made) {
    // While the pirates' theme plays, the generator's end (the scene change back, the jingle, the file select's music) is ordered
    // together in Frame, as one queue: the jingle must follow the scene change, which stops every sound.
    if (made) {
        load_ready_from_cvar();
        if (!g_music_gen)
            bluewake_rando_ui_subbgm(BLUEWAKE_BGM_CHART_FILLED);
    } else if (!g_music_gen) {
        sfx(BLUEWAKE_SE_ERROR);
    }
}

// A dropped seed file being made ready on a worker thread (the game files take about 15 s): handle_drop starts it, Frame
// finishes it. Until it ends nothing about the session changes.
struct Rebuild {
    std::atomic<bool> running{false};
    std::mutex mutex;
    bool done = false;
    bool created = false;  // the seed's folder did not exist before this drop
    std::string seed, error;
    fs::path target;
} g_rebuild;
bool rebuild_running() { return g_rebuild.running.load(); }

void toast(const std::string& message) {
    notice(message);
    notifications::Options o;
    o.prefix = "Randomizer";
    o.message = message;
    notifications::Emit(std::move(o));
}

// The generator's pin ("save1/wwrando-1.10.0-86b97db" -> "86b97db"): a seed made by another wwrando means other placements.
std::string build_commit() {
    const std::string build = randosaves::kBuild;
    return build.substr(build.rfind('-') + 1);
}

// Makes the pack the session's seed: Start Randomizer plays it, the file select shows its hash, the jingle plays.
void adopt(const std::string& name, const BlueWakeRandoPack& pack) {
    set_ready(name, pack);
    CVarSetString(kSeedCVar, name.c_str());
    CVarSetString("gRandomizer.LastSeed", name.c_str());
    sfx(BLUEWAKE_SE_SOLVED);
    toast("Seed " + name + " (" + std::string(pack.seed_hash) + ") loaded");
}

// A seed's own .json (the whole seed in one file, nothing from the disc): validated here, its game files rebuilt from the
// player's disc by the same path generation uses, then adopted. Anything wrong is a toast and nothing changes.
void drop_seed_file(const fs::path& file) {
    std::error_code ec;
    std::string text;
    {
        std::ifstream in(file, std::ios::binary);
        text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    picojson::value root_value;
    if (text.empty() || !picojson::parse(root_value, text).empty() || !root_value.is<picojson::object>()) {
        sfx(BLUEWAKE_SE_ERROR);
        toast("That file is not a randomizer seed (it is empty or not valid JSON).");
        return;
    }
    if (!rando_seedpack_schema(root_value.get("schema").to_str())) {
        sfx(BLUEWAKE_SE_ERROR);
        toast("That file is not a randomizer seed (not a Boat of Daphnes seed file).");
        return;
    }
    const picojson::value& ver = root_value.get("schema_version");
    if (ver.serialize() != "1") {
        sfx(BLUEWAKE_SE_ERROR);
        toast("That seed file is from another version of the game (seed format " + root_value.get("schema_version").serialize() +
              ", this game reads 1).");
        return;
    }
    const std::string commit =
        root_value.get("generator").is<picojson::object>() ? root_value.get("generator").get("wwrando_commit").to_str() : "";
    const std::string mine = build_commit();
    if (commit.empty() || (commit.rfind(mine, 0) != 0 && mine.rfind(commit, 0) != 0)) {
        sfx(BLUEWAKE_SE_ERROR);
        toast("That seed was made by another randomizer version (" + (commit.empty() ? std::string("unknown") : commit.substr(0, 7)) +
              ", this game is " + mine + ").");
        return;
    }
    BlueWakeRandoPack pack;
    std::string error;
    if (!randopack::Parse(text, &pack, &error)) {
        sfx(BLUEWAKE_SE_ERROR);
        toast("That seed file is damaged: " + error);
        return;
    }
    const std::string name = pack.seed;
    if (name.empty() || name.find_first_of("/\\:*?\"<>|") != std::string::npos || name.find("..") != std::string::npos) {
        sfx(BLUEWAKE_SE_ERROR);
        toast("That seed file has an unusable seed name.");
        return;
    }
    const fs::path target = root() / fs::u8path(name);
    // Already made on this PC from this very file: no rebuild.
    {
        std::ifstream have(target / "seed.json", std::ios::binary);
        const std::string old((std::istreambuf_iterator<char>(have)), std::istreambuf_iterator<char>());
        if (have && old == text && fs::exists(target / "overlay" / "files", ec)) {
            adopt(name, pack);
            return;
        }
    }
    if (g_rebuild.running.exchange(true)) {
        toast("A seed is still being prepared; drop this one again in a moment.");
        return;
    }
    const bool created = !fs::exists(target, ec);
    fs::create_directories(target, ec);
    {
        std::ofstream out(target / "seed.json", std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            g_rebuild.running = false;
            toast("The seed could not be saved to the Randomizer folder.");
            return;
        }
    }
    std::fprintf(stderr, "[randomizer] seed file %s: rebuilding the game files for seed %s\n", utf8(file).c_str(), name.c_str());
    toast("Preparing seed " + name + " from your game disc...");
    {
        std::lock_guard<std::mutex> lock(g_rebuild.mutex);
        g_rebuild.done = false;
        g_rebuild.seed = name;
        g_rebuild.created = created;
        g_rebuild.target = target;
        g_rebuild.error.clear();
    }
    const std::string permalink = pack.permalink;
    std::thread([target, permalink] {
        const std::string why = randomizer::RebuildOverlay(utf8(target), permalink);
        std::lock_guard<std::mutex> lock(g_rebuild.mutex);
        g_rebuild.error = why;
        g_rebuild.done = true;
    }).detach();
}

// Frame: the worker has ended.
void poll_rebuild() {
    if (!g_rebuild.running.load())
        return;
    std::string seed, error;
    bool created;
    fs::path target;
    {
        std::lock_guard<std::mutex> lock(g_rebuild.mutex);
        if (!g_rebuild.done)
            return;
        seed = g_rebuild.seed;
        error = g_rebuild.error;
        created = g_rebuild.created;
        target = g_rebuild.target;
        g_rebuild.done = false;
    }
    g_rebuild.running = false;
    BlueWakeRandoPack pack;
    std::string load_error;
    std::error_code ec;
    if (error.empty() && !load_pack(seed, &pack, &load_error))
        error = load_error;
    if (error.empty() && !fs::exists(target / "overlay" / "files", ec))
        error = "its game files were not made";
    if (!error.empty()) {
        if (created)
            fs::remove_all(target, ec);
        sfx(BLUEWAKE_SE_ERROR);
        toast("That seed cannot be loaded: " + error);
        return;
    }
    adopt(seed, pack);
}

void handle_drop(const std::string& path, bool on_file_select) {
    fs::path p = fs::u8path(path);
    std::error_code ec;
    if (!on_file_select) {
        toast("To load a seed file, drop it on the file select screen.");
        return;
    }
    // One .json that is the whole seed (no overlay folder beside it) takes the new route.
    if (fs::is_regular_file(p, ec) && p.extension() == ".json" && !fs::exists(p.parent_path() / "overlay", ec)) {
        drop_seed_file(p);
        return;
    }
    // A spoiler log, or a seed pack folder's own seed.json: the seed pack it belongs to is the folder that holds it.
    fs::path seed_json = p.filename() == "seed.json" ? p : p.parent_path() / "seed.json";
    BlueWakeRandoPack pack;
    std::string error;
    if (!fs::exists(seed_json, ec) || !randopack::Load(seed_json, &pack, &error)) {
        sfx(BLUEWAKE_SE_ERROR);
        toast("That file is not a randomizer seed" + (error.empty() ? std::string() : " (" + error + ")"));
        return;
    }
    // A seed from outside Randomizer\ is copied in, so its overlay files travel with it.
    std::string name = pack.seed;
    const fs::path target = root() / fs::u8path(name);
    if (!fs::equivalent(seed_json.parent_path(), target, ec)) {
        fs::create_directories(target, ec);
        fs::copy(seed_json.parent_path(), target, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    }
    if (load_pack(name, &pack, &error)) {
        adopt(name, pack);
    } else {
        sfx(BLUEWAKE_SE_ERROR);
        toast("That seed cannot be loaded: " + error);
    }
}

void handle_pad_quest(unsigned pressed) {
    if (pressed & (kLeft | kRight)) {
        g_ui.quest ^= 1;  // two quests: either way wraps round
        sfx(BLUEWAKE_SE_CURSOR);
    }
    if (pressed & kB) {
        sfx(BLUEWAKE_SE_CANCEL);
        set_step(Step::Idle);
        bluewake_ui_pad_set_focus(false);
        return;
    }
    if (pressed & (kA | kStart))
        confirm_quest();
}

void handle_pad_menu(unsigned pressed, bool generating) {
    if (generating)
        return;
    // The plates are a row (Start, Generate, Settings, Return): either axis moves along it.
    if (pressed & (kRight | kDown)) {
        g_ui.row = (g_ui.row + 1) % 4;
        sfx(BLUEWAKE_SE_CURSOR);
    }
    if (pressed & (kLeft | kUp)) {
        g_ui.row = (g_ui.row + 3) % 4;
        sfx(BLUEWAKE_SE_CURSOR);
    }
    if ((pressed & kB) || ((pressed & (kA | kStart)) && g_ui.row == 3)) {
        sfx(BLUEWAKE_SE_CANCEL);
        set_step(Step::Quest);
        return;
    }
    if (pressed & (kA | kStart)) {
        switch (g_ui.row) {
        case 0:
            start_randomizer();
            break;
        case 1:
            sfx(BLUEWAKE_SE_DECIDE);
            randomizer::StartGeneration();
            // SoH plays the horse game's music while it generates; here the pirates' theme, until the seed is made.
            bluewake_rando_ui_scene(BLUEWAKE_SCENE_PSHIP);
            g_pirate_in = 1; // the queue holds the theme until the scene's wave banks have loaded
            g_music_gen = true;
            break;
        default:
            sfx(BLUEWAKE_SE_DECIDE);
            bluewake_ui_pad_set_focus(false);
            set_step(Step::Idle);
            bw_settings_open_menu_at("Randomizer", "General");
            break;
        }
    }
}

// Runs the hand-off to the game: its menu opens on one A, and for a new file Start is pressed too.
void run_hand(const BluewakeLocation& loc) {
    ++g_ui.hand_wait;
    switch (g_ui.hand_phase) {
    case 0:  // let the pad come up (the A that chose is still down)
        if (g_ui.hand_wait > 10) {
            bluewake_ui_pad_inject(kA, 6);
            g_ui.hand_phase = 1;
            g_ui.hand_wait = 0;
        }
        break;
    case 1:  // the game's menu opening
        if (loc.fs_main == 4 && loc.fs_sub == 3) {
            g_ui.hand_phase = 2;
            g_ui.hand_wait = 0;
        } else if (g_ui.hand_wait > 90) {
            g_ui.hand_phase = 0;  // missed: again
            g_ui.hand_wait = 0;
        }
        break;
    case 2:
        if (!g_ui.hand_confirm_start || g_ui.hand_wait > 20) {
            if (g_ui.hand_confirm_start)
                bluewake_ui_pad_inject(kA, 6);
            g_ui.hand_phase = 3;
            g_ui.hand_wait = 0;
        }
        break;
    default:  // done once the game has left the list
        if (loc.fs_main != 4 || g_ui.hand_wait > 240) {
            if (loc.fs_main == 4)
                g_naming.active = false;  // it did not take: no new file
            set_step(Step::Idle);
        }
        break;
    }
}

} // namespace

bool Active() { return g_active.load(); }

void Launch(const char* data_dir) {
    g_dir = data_dir;
    g_resume = CVarGetInteger(kResumeCVar, 0) != 0;
    if (g_resume) {  // once: a crash must not loop
        CVarSetInteger(kResumeCVar, 0);
        CVarSave();
    }
    // A launch is a vanilla launch: the quest belongs to the file the player starts. Only a restart the file select asked
    // for (a module that cannot switch while the game runs) comes back in the randomizer; BLUEWAKE_RANDOMIZER_MODE=1 is
    // the same for a test.
    const char* forced = std::getenv("BLUEWAKE_RANDOMIZER_MODE");
    g_active = (CVarGetInteger(kModeCVar, 0) == 1 && g_resume) || (forced != nullptr && forced[0] == '1');
    if (CVarGetInteger(kModeCVar, 0) != 0 && !g_active.load()) {
        CVarSetInteger(kModeCVar, 0);
        CVarSave();
    }
    if (g_active.load()) {
        std::string seed = CVarGetString(kSeedCVar, "");
        if (seed.empty())
            seed = CVarGetString("gRandomizer.LastSeed", "");
        std::string error;
        if (!seed.empty() && load_pack(seed, &g_pack, &error)) {
            g_seed = seed;
            bluewake_rando_set_pack(&g_pack);
            std::fprintf(stderr, "[randomizer] seed %s (%s)\n", seed.c_str(), g_pack.seed_hash);
        } else {
            std::fprintf(stderr, "[randomizer] no seed pack loaded (%s)\n", seed.empty() ? "none chosen" : error.c_str());
        }
        // The module's randomizer variant, alongside the mods already chosen.
        const char* mods = std::getenv("BLUEWAKE_MODS");
        std::string list = mods != nullptr && mods[0] != '\0' ? mods : "betterww";
        if (list.find("rando") == std::string::npos)
            list += ",rando";
        bodplat::SetEnv("BLUEWAKE_MODS", list);
    }
    randosaves::Load(sidecar_path(), &g_sidecar);
    load_ready_from_cvar();
    if (g_pack.loaded && g_ready_seed.empty())
        set_ready(g_seed, g_pack);
    // A scripted new game (ui_off): the game's own file select with no quest select; a new file in a randomizer session
    // belongs to the session's seed.
    if (!ui_off())
        bluewake_ui_pad_set_gate(pad_gate, nullptr);
}

void OnDrop(const char* path) {
    std::lock_guard<std::mutex> lock(g_drop_mutex);
    g_dropped = path != nullptr ? path : "";
}

void OnSaved(int slot) {
    if (slot < 0 || slot > 2 || !g_active.load() || !g_pack.loaded)
        return;
    randosaves::Load(sidecar_path(), &g_sidecar);
    if (g_sidecar.slots.count(slot + 1) != 0)
        return;
    randosaves::Set(&g_sidecar, slot + 1, g_pack);
    save_sidecar();
    std::fprintf(stderr, "[randomizer] file %d saved: seed %s kept in the slots list\n", slot + 1, g_seed.c_str());
}

void OnPick(const char* path) {
    std::lock_guard<std::mutex> lock(g_drop_mutex);
    g_picked = path != nullptr ? path : "";
}

void Frame(float scale) {
    (void)scale;
    if (g_restart_in > 0 && --g_restart_in == 0)
        bw_settings_restart_now();
    const BluewakeLocation here = bluewake_game_location();
    {
        static int last_scene = -1;
        if (here.scene_proc != last_scene) {
            std::fprintf(stderr, "[randomizer] scene process %d (slot %d)\n", here.scene_proc, here.data_num);
            if (here.scene_proc == BLUEWAKE_SCENE_NAME)
                refresh_slots();
            last_scene = here.scene_proc;
        }
    }
    static unsigned frames = 0;
    if (frames++ == 0 && g_active.load() != bluewake_rando_session_runs_it()) {
        // The host's truth: a module that switches in place starts every session as vanilla whatever an old setting says.
        std::fprintf(stderr, "[randomizer] the session starts %s\n", bluewake_rando_session_runs_it() ? "as a randomizer one" : "as a vanilla one");
        g_active = bluewake_rando_session_runs_it();
        if (!g_active.load()) {
            g_pack = BlueWakeRandoPack{};
            g_seed.clear();
        }
    }
    // Back to the file select after a restart: Start at the title screen, a few times if it is missed.
    if (g_resume) {
        if (here.scene_proc == BLUEWAKE_SCENE_NAME) {
            g_resume = false;
        } else if (here.scene_proc == BLUEWAKE_SCENE_TITLE && frames % 90 == 0) {
            bluewake_ui_pad_inject(static_cast<uint16_t>(kStart), 6);
        }
    }
    if (g_notice_frames > 0 && --g_notice_frames == 0)
        g_notice.clear();

    // A file the game has just started: whose it is.
    static int started_slot = -1;
    if (here.scene_proc == BLUEWAKE_SCENE_PLAY && here.has_player) {
        g_played = true;
        const int slot = here.data_num + 1;
        if (slot >= 1 && slot <= 3 && slot != started_slot) {
            started_slot = slot;
            randosaves::Load(sidecar_path(), &g_sidecar);
            if (g_naming.active && g_naming.slot + 1 == slot) {
                if (g_naming.rando && g_active.load()) {
                    randosaves::Set(&g_sidecar, slot, g_pack);
                    std::fprintf(stderr, "[randomizer] file %d is a new randomizer file, seed %s (%s)\n", slot, g_seed.c_str(), g_pack.seed_hash);
                } else {
                    randosaves::Erase(&g_sidecar, slot);
                    std::fprintf(stderr, "[randomizer] file %d is a new vanilla file\n", slot);
                }
                save_sidecar();
            } else if (ui_off() && g_active.load() && g_pack.loaded &&
                       g_sidecar.slots.count(slot) == 0) {
                randosaves::Set(&g_sidecar, slot, g_pack);
                save_sidecar();
                std::fprintf(stderr, "[randomizer] file %d is seed %s\n", slot, g_seed.c_str());
            } else {
                const auto it = g_sidecar.slots.find(slot);
                if (it != g_sidecar.slots.end())
                    std::fprintf(stderr, "[randomizer] file %d resumes seed %s (%s)\n", slot, it->second.seed.c_str(), it->second.hash.c_str());
                else
                    std::fprintf(stderr, "[randomizer] file %d is a vanilla file\n", slot);
            }
            g_naming.active = false;
        }
    } else if (here.scene_proc != BLUEWAKE_SCENE_PLAY) {
        started_slot = -1;
    }

    std::string dropped;
    {
        std::lock_guard<std::mutex> lock(g_drop_mutex);
        dropped.swap(g_dropped);
        // A chosen file waits for the file select (said once), then goes the way a drop there does.
        static std::string told;
        if (!g_picked.empty() && dropped.empty()) {
            if (here.scene_proc == BLUEWAKE_SCENE_NAME) {
                dropped.swap(g_picked);
                told.clear();
            } else if (told != g_picked) {
                told = g_picked;
                toast("The seed file loads when you reach the file select.");
            }
        }
    }
    // BLUEWAKE_TEST_DROP=<file>[@<frame>] (a test): that file is dropped on the window at that frame of the run, or on
    // the file select scene when no frame is given.
    if (static bool tested = false; !tested) {
        const char* test = std::getenv("BLUEWAKE_TEST_DROP");
        if (test == nullptr || test[0] == 0) {
            tested = true;
        } else {
            std::string spec = test;
            long at = -1;
            if (const size_t pos = spec.rfind('@');
                pos != std::string::npos && pos + 1 < spec.size() && spec.find_first_not_of("0123456789", pos + 1) == std::string::npos) {
                at = std::atol(spec.c_str() + pos + 1);
                spec.resize(pos);
            }
            if (at >= 0 ? static_cast<long>(frames) >= at : here.scene_proc == BLUEWAKE_SCENE_NAME) {
                tested = true;
                dropped = spec;
            }
        }
    }
    poll_rebuild();
    if (!dropped.empty())
        handle_drop(dropped, here.scene_proc == BLUEWAKE_SCENE_NAME);

    if (here.scene_proc != BLUEWAKE_SCENE_NAME) {
        bluewake_fs_cover(false);
        if (g_ui.step != Step::Idle) {
            set_step(Step::Idle);
            bluewake_ui_pad_set_focus(false);
        }
        g_request = 0;
        return;
    }

    {
        // The randomizer files' plates are crimson, as Second Quest files' are gold (game_state.c).
        unsigned mask = 0;
        for (int i = 0; i < 3; ++i)
            if (!here.fs_new[i] && g_sidecar.slots.count(i + 1))
                mask |= 1u << i;
        bluewake_fs_set_rando_slots(mask);
    }
    unsigned pressed = bluewake_ui_pad_take_pressed();
    // The settings menu is open over the file select (Open Randomizer Settings): it has the pad, not this.
    const bool menu_open = bw_settings_menu_is_open();
    if (menu_open)
        pressed = 0;
    // Over the game, under every ImGui window (the settings menu, trackers): the foreground list drew it through them.
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const Space sp = space();

    // The file list changed under the sidecar (a file erased in the game): keep only files that exist.
    {
        bool changed = false;
        for (int i = 0; i < 3 && here.fs_main >= 3; ++i)
            if (here.fs_new[i] && g_sidecar.slots.count(i + 1) && !(g_naming.active && g_naming.slot == i)) {
                randosaves::Erase(&g_sidecar, i + 1);
                changed = true;
            }
        if (changed) {
            save_sidecar();
            refresh_slots();
        }
    }

    // Name entry: leaving it (B back to the list) cancels the new file.
    if (g_naming.active && here.fs_main == 4 && g_ui.step == Step::Idle)
        g_naming.active = false;

    poll_switch();
    if (g_switch_wait.active) {
        ++g_switch_wait.frames;
        if (bluewake_rando_ui_hook_ran() || g_switch_wait.frames >= 300) {
            const SwitchWait w = g_switch_wait;
            g_switch_wait.active = false;
            switch_then(w.rando, w.seed, w.after);
        }
        pressed = 0;
    }
    if (g_pending.active)
        pressed = 0;

    // The pad gate's request: the player pressed A on a file.
    if (const int request = g_request.exchange(0); request != 0 && g_ui.step == Step::Idle) {
        const int slot = g_request_slot.load();
        refresh_slots();
        if (request == 1) {
            g_ui.slot = slot;
            g_ui.quest = g_active.load() ? 1 : 0;
            g_ui.alpha = 0.f;
            take_pad();
            set_step(Step::Quest);
            pressed = 0;
            sfx(BLUEWAKE_SE_DECIDE);
        } else {
            // An existing file whose quest or seed is not the session's: the session becomes the file's, then the game goes on.
            const auto it = g_sidecar.slots.find(slot + 1);
            g_ui.slot = slot;
            if (it == g_sidecar.slots.end()) {
                switch_then(false, "", AfterExisting);
            } else if (it->second.build != randosaves::kBuild) {
                sfx(BLUEWAKE_SE_ERROR);
                notice("This randomizer file was made by another version of the game and cannot be loaded.");
            } else if (!fs::exists(root() / fs::u8path(it->second.seed) / "seed.json")) {
                sfx(BLUEWAKE_SE_ERROR);
                notice("The seed of this file (" + it->second.seed + ") is missing from the Randomizer folder.");
            } else {
                switch_then(true, it->second.seed, AfterExisting);
            }
            pressed = 0;
        }
    }

    // The generator's end: the made-seed jingle (the fishman's chart drawing) or the error sound, then the file select's own
    // music back; on success the new seed is the one Start Randomizer plays.
    {
        const int outcome = randomizer::LastOutcome();
        static int last_outcome = 0;
        if (outcome != last_outcome) {
            if (outcome != 0 && g_ui.step == Step::Menu)
                on_generation_done(outcome == 1);
            if (outcome != 0 && g_music_gen) {
                g_music_gen = false;
                g_pirate_in = 0;
                // The audio scene goes back first (it stops the pirates' theme and every jingle), then the jingle, then the file
                // select's music the moment the jingle has ended: the queue keeps the order.
                bluewake_rando_ui_scene(0xFFFFu);
                if (outcome == 1) {
                    bluewake_rando_ui_subbgm(BLUEWAKE_BGM_CHART_FILLED);
                    bluewake_rando_ui_bgm_after_subbgm(BLUEWAKE_BGM_SELECT);
                } else {
                    sfx(BLUEWAKE_SE_ERROR);
                    bluewake_rando_ui_bgm(BLUEWAKE_BGM_SELECT);
                }
            }
            last_outcome = outcome;
        }
        if (g_pirate_in > 0 && --g_pirate_in == 0 && g_music_gen)
            bluewake_rando_ui_bgm(BLUEWAKE_BGM_PIRATE);
    }

    const bool generating = randomizer::Generating();
    const bool have_seed = g_ready.loaded;
    switch (g_ui.step) {
    case Step::Quest:
        g_ui.alpha = std::min(1.f, g_ui.alpha + 0.1f);
        handle_pad_quest(pressed);
        break;
    case Step::Menu:
        g_ui.alpha = std::min(1.f, g_ui.alpha + 0.1f);
        handle_pad_menu(pressed, generating);
        break;
    case Step::Hand:
        run_hand(here);
        break;
    default:
        break;
    }
    ++g_ui.frames;

    // The game's list is hidden while the quest select and the menu are up: they draw the file select's panes themselves.
    bluewake_fs_cover(g_ui.step == Step::Quest || g_ui.step == Step::Menu);
    switch (g_ui.step) {
    case Step::Quest:
        draw_quest(dl, sp, g_ui.alpha);
        break;
    case Step::Menu:
        draw_menu(dl, sp, g_ui.alpha, generating, have_seed);
        break;
    default:
        if (here.fs_main == 4 && (here.fs_sub == 0 || here.fs_sub == 1))
            draw_list_extras(dl, sp, here);
        if (here.fs_main >= 6 && here.fs_main <= 8)
            draw_naming(dl, sp);
        break;
    }
    draw_notice(dl, sp);
    {
        // The overlay's state in the log (scenario frames hold the game's picture, not this overlay): what a player reads.
        std::string state;
        switch (g_ui.step) {
        case Step::Quest:
            state = std::string("quest select: ") + (g_ui.quest ? "< Randomizer >" : "< Vanilla >") + " (file " + std::to_string(g_ui.slot + 1) + ")";
            break;
        case Step::Menu:
            state = std::string("randomizer menu: ") + (g_ui.row == 0 ? "> " : "  ") + kStartText + (have_seed ? "" : " (off)") + " | " +
                    (g_ui.row == 1 ? "> " : "  ") + (generating ? kGeneratingText : kGenerateText) + " | " +
                    (g_ui.row == 2 ? "> " : "  ") + kSettingsText;
            if (!have_seed && g_ui.row == 0 && !generating)
                state += " | No randomizer seed loaded. Please generate one first, or drop a seed file on the game window.";
            else if (have_seed)
                state += " | seed " + g_ready_seed + " (" + g_ready.seed_hash + ")";
            break;
        case Step::Hand:
            state = "handing over to the game";
            break;
        default:
            if (g_naming.active && here.fs_main >= 6) {
                state = std::string("name entry, ") + (g_naming.rando ? std::string("randomizer seed ") + g_ready.seed_hash : std::string("vanilla"));
            } else if (here.fs_main == 4 && here.fs_sub == 0) {
                state = "file list";
                for (int i = 0; i < 3; ++i) {
                    const auto it = g_sidecar.slots.find(i + 1);
                    state += std::string(" | file ") + std::to_string(i + 1) + ": " +
                             (here.fs_new[i] ? "empty" : it != g_sidecar.slots.end() ? "Randomizer " + it->second.hash : std::string("Vanilla"));
                }
            }
            break;
        }
        static std::string last;
        if (state != last) {
            if (!state.empty())
                std::fprintf(stderr, "[randomizer] panel %s\n", state.c_str());
            last = state;
        }
    }
}

void OnDiscReady() {
    // The stock title logo, kept before a seed's own replaces it in the DVD overlay.
    fsart::CaptureStock();
    const char* rels = std::getenv("BLUEWAKE_RELS_DIR");
    const fs::path game = rels != nullptr ? fs::path(rels).parent_path() : fs::path();
    std::vector<fs::path> sources;
    sources.push_back(game / "rando" / "logo" / "TlogoE.arc");  // the prepared game folder's (prepare_extras.py)
    if (!g_ready_seed.empty())
        sources.push_back(root() / fs::u8path(g_ready_seed) / "overlay" / "files" / "res" / "Object" / "TlogoE.arc");
    sources.push_back(root() / "logo" / "TlogoE.arc");
    fsart::SetRandoSources(sources);
    if (!g_active.load())
        return;
    // The variant's patched RELs come from the player's disc (first launch, rando\prepare_extras.py) and sit beside the
    // prepared rels\ as rando\rels\. Without them the seed would play on the unpatched game: say so.
    {
        std::error_code ec;
        const fs::path made = game / "rando" / "rels";
        if (rels == nullptr || !fs::exists(made / "d_a_ship.rel", ec)) {
            g_notice = "The randomizer's game files were not made from your disc (see extras.json in the prepared game folder "
                       "and the session log).";
            std::fprintf(stderr, "[randomizer] the patched RELs are missing (%s): seeds cannot play correctly\n", made.string().c_str());
        }
    }
    mount_seed(g_seed);
}

std::string Seed() { return g_seed; }

std::string SeedJson() {
    if (g_seed.empty())
        return "";
    return utf8(root() / fs::u8path(g_seed) / "seed.json");
}

std::string SaveFolder() { return utf8(sidecar_path().parent_path()); }

} // namespace randoflow
