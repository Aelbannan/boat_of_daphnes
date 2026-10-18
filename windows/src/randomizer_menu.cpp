// The Randomizer header: see randomizer_menu.h. SoH publishes no license; the layout follows its
// Randomizer menu with code of its own.
#ifdef rename
#undef rename // bw_posix_compat.h's, which <filesystem> must not see
#endif

#include "randomizer_menu.h"
#include "rando_flow.h"

#include "ship_menu.h"
#include "ship/cvar.h"

#define PICOJSON_USE_INT64
#include "picojson.h"

#include <imgui.h>
#include <SDL3/SDL_dialog.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" bool bluewake_bunny_hood_enabled(void);

namespace randomizer {
namespace {

namespace fs = std::filesystem;
using shipmenu::WidgetInfo;
using shipmenu::WidgetType;

const char* const H = "Randomizer";

// One wwrando option, as describe_options.py writes it.
struct Option {
    std::string name, label, group, description, type; // type: bool, int, choice, list
    int def = 0, min = 0, max = 0;
    std::vector<std::string> choices;
    std::vector<std::string> list_default;
    bool hidden = false;
};

std::vector<Option> g_options;
std::map<std::string, size_t> g_by_name;

// Options that are the player's preference, not the seed's: they live in the game's own menus (Controls, Enhancements, Audio,
// Better Wind Waker's rows) and work in vanilla and randomizer sessions alike. Seeds are made with them at the reference game's
// values (below), and a loaded seed's values for them are ignored: the player's setting decides.
// name -> the reference value (variant_sites.py REFERENCE_OPTIONS).
const std::map<std::string, bool>& player_options() {
    static const std::map<std::string, bool> m = {
        {"hero_mode", false},                 // Better Wind Waker's Hero Mode (Quality of Life)
        {"swift_sail", false},                // Better Wind Waker (Items)
        {"instant_text_boxes", false},        // Better Wind Waker's Instant Text (Skips & Speed-ups)
        {"reveal_full_sea_chart", false},     // Better Wind Waker's Reveal Sea Chart
        {"invert_camera_x_axis", false},      // Controls > Invert Camera X Axis
        {"invert_sea_compass_x_axis", true},  // Items > Sailing > Invert Sea Compass X-Axis
        {"remove_title_and_ending_videos", false}, // Skips & Speed-ups > Remove Title and Ending Videos
        {"remove_music", true},               // Audio > Music Volume (0 = no music; the slider decides, the seed's patch stays off)
        {"switch_targeting_mode", true},      // Controls > Use 'Switch' Targeting Mode
    };
    return m;
}
bool is_player_option(const std::string& name) { return player_options().count(name) != 0; }

std::string cvar_of(const std::string& name) { return "gRandomizer.Options." + name; }

// Load Seed File: SDL's file dialog (its callback may come on another thread; randoflow::OnPick takes it from any).
void open_seed_dialog() {
    static const SDL_DialogFileFilter filters[] = {{"Randomizer seed", "json"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(
        [](void*, const char* const* files, int) {
            if (files != nullptr && files[0] != nullptr)
                randoflow::OnPick(files[0]);
        },
        nullptr, nullptr, filters, 2, nullptr, false);
}

fs::path exe_folder() {
    wchar_t path[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
    return fs::path(std::wstring(path, n)).parent_path();
}

std::string utf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream in(s);
    std::string item;
    while (std::getline(in, item, '|'))
        if (!item.empty())
            out.push_back(item);
    return out;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& i : v)
        s += (s.empty() ? "" : "|") + i;
    return s;
}

bool load_options() {
    std::ifstream in(exe_folder() / "assets" / "randomizer" / "options.json", std::ios::binary);
    if (!in)
        return false;
    std::stringstream text;
    text << in.rdbuf();
    picojson::value v;
    if (!picojson::parse(v, text.str()).empty() || !v.is<picojson::object>() || !v.get("options").is<picojson::array>())
        return false;
    for (const auto& e : v.get("options").get<picojson::array>()) {
        if (!e.is<picojson::object>())
            continue;
        Option o;
        o.name = e.get("name").to_str();
        if (o.name == "bunny_hood" && !bluewake_bunny_hood_enabled())
            continue; // experimental (gExperimental.BunnyHood)
        o.label = e.get("label").to_str();
        o.group = e.get("group").to_str();
        o.description = e.get("description").to_str();
        o.type = e.get("type").to_str();
        o.hidden = e.get("hidden").is<bool>() && e.get("hidden").get<bool>();
        const picojson::value& d = e.get("default");
        if (o.type == "bool")
            o.def = d.is<bool>() && d.get<bool>() ? 1 : 0;
        else if (o.type == "int") {
            o.def = d.is<int64_t>() ? static_cast<int>(d.get<int64_t>()) : 0;
            o.min = e.get("min").is<int64_t>() ? static_cast<int>(e.get("min").get<int64_t>()) : 0;
            o.max = e.get("max").is<int64_t>() ? static_cast<int>(e.get("max").get<int64_t>()) : 99;
        } else if (o.type == "choice") {
            for (const auto& c : e.get("choices").get<picojson::array>())
                o.choices.push_back(c.to_str());
            o.def = static_cast<int>(std::find(o.choices.begin(), o.choices.end(), d.to_str()) - o.choices.begin());
            if (o.def >= static_cast<int>(o.choices.size()))
                o.def = 0;
        } else if (d.is<picojson::array>()) {
            for (const auto& c : d.get<picojson::array>())
                o.list_default.push_back(c.to_str());
        }
        g_by_name[o.name] = g_options.size();
        g_options.push_back(std::move(o));
    }
    return !g_options.empty();
}

const Option* find(const std::string& name) {
    const auto it = g_by_name.find(name);
    return it == g_by_name.end() ? nullptr : &g_options[it->second];
}

// The starting gear (wwrando's Starting Items tab): the items moved from the randomized pool.
std::vector<std::string> starting_gear() {
    const Option* o = find("starting_gear");
    if (!CVarExists(cvar_of("starting_gear").c_str()))
        return o ? o->list_default : std::vector<std::string>{};
    return split(CVarGetString(cvar_of("starting_gear").c_str(), ""));
}

std::vector<std::string> gear_pool() {
    std::vector<std::string> pool;
    for (const char* n : {"starting_gear", "randomized_gear"})
        if (const Option* o = find(n))
            pool.insert(pool.end(), o->list_default.begin(), o->list_default.end());
    if (bluewake_bunny_hood_enabled())
        pool.push_back("Bunny Hood"); // ours (generate_seed.py takes it out of the list; the pack grants it as starting gear)
    std::sort(pool.begin(), pool.end());
    return pool;
}

// --- generation -------------------------------------------------------------------------------

std::mutex g_mutex;
std::atomic<bool> g_generating{false};
std::string g_result; // the last generation's message
std::atomic<int> g_outcome{0};

picojson::object chosen_options() {
    picojson::object o;
    for (const Option& opt : g_options) {
        if (opt.hidden || opt.group.empty() || opt.name == "dry_run" || is_player_option(opt.name))
            continue;
        const std::string cv = cvar_of(opt.name);
        if (opt.type == "bool")
            o[opt.name] = picojson::value(CVarGetInteger(cv.c_str(), opt.def) != 0);
        else if (opt.type == "int")
            o[opt.name] = picojson::value(static_cast<int64_t>(std::clamp(CVarGetInteger(cv.c_str(), opt.def), opt.min, opt.max)));
        else if (opt.type == "choice" && !opt.choices.empty())
            o[opt.name] = picojson::value(opt.choices[std::clamp(CVarGetInteger(cv.c_str(), opt.def), 0, int(opt.choices.size()) - 1)]);
    }
    // The player's preferences are not the seed's: every one is made at the reference game's value.
    for (const auto& [name, value] : player_options())
        o[name] = picojson::value(value);
    // wwrando's window rule (randomizer_window.py): swordless takes the swords and Hurricane Spin out of
    // the starting gear.
    const Option* sword = find("sword_mode");
    const bool swordless = sword != nullptr && !sword->choices.empty() &&
                           sword->choices[std::clamp(CVarGetInteger(cvar_of("sword_mode").c_str(), sword->def), 0,
                                                     int(sword->choices.size()) - 1)] == "Swordless";
    picojson::array gear;
    for (const auto& item : starting_gear())
        if ((!swordless || (item != "Progressive Sword" && item != "Hurricane Spin")) &&
            (item != "Bunny Hood" || bluewake_bunny_hood_enabled()))
            gear.push_back(picojson::value(item));
    o["starting_gear"] = picojson::value(gear);
    return o;
}

// Where the seed packs live: BLUEWAKE_RANDOMIZER_DIR (testing), else Randomizer\ in the data folder (beside the exe unless
// BLUEWAKE_DATA_DIR moves it): the folder the file select reads (rando_flow.cpp).
fs::path seed_folder(const fs::path& exe_root) {
    if (const char* env = std::getenv("BLUEWAKE_RANDOMIZER_DIR"); env != nullptr && env[0] != 0)
        return fs::u8path(env);
    if (const char* data = std::getenv("BLUEWAKE_DATA_DIR"); data != nullptr && data[0] != 0)
        return fs::u8path(data) / "Randomizer";
    return exe_root / "Randomizer";
}

// Where a seed's one-file form lives: Randomizer\<seed>.json (the pack, no disc data).
fs::path seed_file_path(const std::string& seed) { return seed_folder(exe_folder()) / fs::u8path(seed + ".json"); }

// The file on the clipboard the way Explorer's Copy puts it (CF_HDROP), so it pastes into a chat or a folder.
void copy_file_to_clipboard(const fs::path& file) {
    const std::wstring w = fs::absolute(file).wstring();
    const SIZE_T bytes = sizeof(DROPFILES) + (w.size() + 2) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GHND, bytes);
    if (mem == nullptr)
        return;
    auto* df = static_cast<DROPFILES*>(GlobalLock(mem));
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    std::memcpy(reinterpret_cast<char*>(df) + sizeof(DROPFILES), w.c_str(), w.size() * sizeof(wchar_t));
    GlobalUnlock(mem);
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        if (SetClipboardData(CF_HDROP, mem) == nullptr)
            GlobalFree(mem);
        CloseClipboard();
    } else {
        GlobalFree(mem);
    }
}

// The seed's game files (stage, event and text files, the banner) as wwrando patches them, for the DVD overlay: the same
// permalink through patch_game.py from the player's own disc, about 15 s. "" when made, else why not. Blocking.
std::string rebuild_overlay(const fs::path& out, const std::string& permalink) {
    const fs::path runtime = exe_folder() / "rando";
    const fs::path python = runtime / "python" / "python.exe";
    const fs::path patcher = runtime / "patch_game.py";
    // The disc the game runs from (the release has none beside the exe: the player's own).
    const char* disc = std::getenv("BLUEWAKE_DISC");
    const fs::path iso = disc != nullptr && disc[0] != '\0' ? fs::u8path(disc) : exe_folder() / "game" / "GZLE01.iso";
    if (!fs::exists(python) || !fs::exists(patcher))
        return "no game files: the randomizer generator is missing";
    if (!fs::exists(iso))
        return "no game files: the disc is missing";
    std::wstring pcmd = L"\"" + python.wstring() + L"\" \"" + patcher.wstring() + L"\" --iso \"" + iso.wstring() +
                        L"\" --out \"" + (out / "overlay").wstring() + L"\" --permalink \"" +
                        fs::u8path(permalink).wstring() + L"\" --files all";
    STARTUPINFOW psi{};
    psi.cb = sizeof psi;
    psi.dwFlags = STARTF_USESTDHANDLES;
    psi.hStdOutput = GetStdHandle(STD_ERROR_HANDLE);
    psi.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION ppi{};
    std::vector<wchar_t> pline(pcmd.begin(), pcmd.end());
    pline.push_back(0);
    SetEnvironmentVariableW(L"RANDO_RUNTIME", runtime.c_str());
    if (!CreateProcessW(nullptr, pline.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &psi, &ppi))
        return "its game files could not be made: the patch tool did not start";
    WaitForSingleObject(ppi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(ppi.hProcess, &code);
    CloseHandle(ppi.hThread);
    CloseHandle(ppi.hProcess);
    return code != 0 ? "its game files could not be made: see the log" : "";
}

// wwrando's generator in its own process (the embedded Python under rando\), one line of JSON back.
void generate(std::string seed, std::string options_json) {
    const fs::path root = exe_folder();
    const fs::path runtime = root / "rando";
    const fs::path python = runtime / "python" / "python.exe";
    std::string message;
    if (!fs::exists(python)) {
        message = "The randomizer generator is not installed (rando\\ beside the game is missing).";
    } else {
        std::error_code ec;
        const fs::path seeds = seed_folder(root);
        const fs::path work = seeds / ".work";
        fs::create_directories(work, ec);
        const fs::path opts = work / "options.json", result = work / "result.json";
        std::ofstream(opts, std::ios::binary) << options_json;
        const fs::path out = seeds / fs::u8path(seed);
        std::wstring cmd = L"\"" + python.wstring() + L"\" \"" + (runtime / "generate_seed.py").wstring() + L"\" --out \"" +
                           out.wstring() + L"\" --options \"" + opts.wstring() + L"\"";
        cmd += L" --seed \"" + fs::u8path(seed).wstring() + L"\"";
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        HANDLE file = CreateFileW(result.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
        STARTUPINFOW si{};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = file;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION pi{};
        SetEnvironmentVariableW(L"RANDO_RUNTIME", runtime.c_str());
        std::vector<wchar_t> line(cmd.begin(), cmd.end());
        line.push_back(0);
        if (file != INVALID_HANDLE_VALUE &&
            CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            message = "The randomizer generator could not be started.";
        }
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        if (message.empty()) {
            std::ifstream in(result, std::ios::binary);
            std::string last, l;
            while (std::getline(in, l))
                if (!l.empty() && l[0] == '{')
                    last = l;
            picojson::value v;
            if (last.empty() || !picojson::parse(v, last).empty() || !v.is<picojson::object>()) {
                message = "The generator gave no result (see the log).";
            } else if (v.get("ok").is<bool>() && v.get("ok").get<bool>()) {
                const std::string name = v.get("seed").to_str();
                message = "Seed " + name + " (" + v.get("seed_hash").to_str() + ") in Randomizer\\" + name;
                CVarSetString("gRandomizer.LastSeed", name.c_str());
                CVarSetString("gRandomizer.PlaySeed", name.c_str());
                // The seed's files (stage, event and text files, the banner) as wwrando patches them, for the DVD
                // overlay: the same seed and options through patch_game.py, about 15 s more.
                const std::string why = rebuild_overlay(out, v.get("permalink").to_str());
                if (!why.empty())
                    message += " (" + why + ")";
                // The one-file seed: the pack beside its folder, what a player sends to a friend.
                std::error_code cec;
                fs::copy_file(out / "seed.json", seeds / fs::u8path(name + ".json"), fs::copy_options::overwrite_existing, cec);
                if (cec)
                    message += " (the shareable seed file could not be written)";
                CVarSetString("gRandomizer.Permalink", v.get("permalink").to_str().c_str());
                CVarSetString("gRandomizer.SpoilerLog",
                              v.get("spoiler_log").is<std::string>() ? v.get("spoiler_log").to_str().c_str() : "");
                CVarSaveNextFrame();
            } else {
                message = "Generation failed: " + v.get("error").to_str();
            }
        }
    }
    std::fprintf(stderr, "[randomizer] %s\n", message.c_str());
    std::lock_guard<std::mutex> lock(g_mutex);
    g_result = message;
    g_outcome = message.rfind("Seed ", 0) == 0 ? 1 : 2;
    g_generating = false;
}

void start_generation() {
    if (g_generating.exchange(true))
        return;
    std::string seed = CVarGetString("gRandomizer.Seed", "");
    if (seed.empty()) { // as SoH: a blank seed is ten random digits
        std::mt19937_64 rng(std::random_device{}());
        seed = std::to_string(rng() % 9000000000ull + 1000000000ull);
    }
    // Detached: the generator is its own process; quitting during it must not stop the app.
    std::thread(generate, seed, picojson::value(chosen_options()).serialize()).detach();
}

void randomize_all_settings() {
    std::mt19937 rng(std::random_device{}());
    for (const Option& o : g_options) {
        if (o.hidden || o.group.empty() || o.group == "Additional Advanced Options" || is_player_option(o.name))
            continue;
        const std::string cv = cvar_of(o.name);
        if (o.type == "bool")
            CVarSetInteger(cv.c_str(), static_cast<int>(rng() & 1));
        else if (o.type == "choice" && !o.choices.empty())
            CVarSetInteger(cv.c_str(), static_cast<int>(rng() % o.choices.size()));
    }
    CVarSaveNextFrame();
}

// --- pages ------------------------------------------------------------------------------------

void add_option(shipmenu::Menu& m, const char* page, int column, const Option& o) {
    const std::string cv = cvar_of(o.name);
    WidgetInfo* w = nullptr;
    if (o.type == "bool")
        w = &m.AddWidget(H, page, column, o.label, WidgetType::CVarCheckbox).CVar(cv).DefaultValue(o.def);
    else if (o.type == "int")
        w = &m.AddWidget(H, page, column, o.label + ": %d", WidgetType::CVarSliderInt).CVar(cv).DefaultValue(o.def).Range(o.min, o.max);
    else if (o.type == "choice")
        w = &m.AddWidget(H, page, column, o.label, WidgetType::CVarCombobox).CVar(cv).DefaultValue(o.def).Items(o.choices);
    if (w != nullptr) {
        w->Tooltip(o.description);
        w->PreFunc([](WidgetInfo& wi) {
            if (g_generating)
                wi.disabledReasons.push_back("A seed is being generated");
        });
    }
}

// Each option of `group`, in wwrando's order, over `columns` columns from `first`.
void add_group(shipmenu::Menu& m, const char* page, const char* group, int first, int columns, const char* section) {
    std::vector<const Option*> list;
    for (const Option& o : g_options)
        if (o.group == group && !o.hidden && o.type != "list" && !is_player_option(o.name))
            list.push_back(&o);
    if (section != nullptr && !list.empty())
        m.AddWidget(H, page, first, section, WidgetType::SeparatorText);
    const size_t per = (list.size() + columns - 1) / std::max(columns, 1);
    for (size_t i = 0; i < list.size(); ++i)
        add_option(m, page, first + static_cast<int>(per ? i / per : 0), *list[i]);
}

void draw_gear() {
    std::vector<std::string> start = starting_gear();
    std::vector<std::string> pool = gear_pool();
    for (const auto& s : start) // the randomized side: the pool without what is moved out
        if (const auto it = std::find(pool.begin(), pool.end(), s); it != pool.end())
            pool.erase(it);
    bool changed = false;
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const float height = ImGui::GetTextLineHeightWithSpacing() * 16;
    ImGui::BeginDisabled(g_generating);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Randomized Gear");
    if (ImGui::BeginListBox("##randomized", ImVec2(half, height))) {
        for (size_t i = 0; i < pool.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(pool[i].c_str())) {
                start.push_back(pool[i]);
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Starting Gear");
    if (ImGui::BeginListBox("##starting", ImVec2(half, height))) {
        for (size_t i = 0; i < start.size(); ++i) {
            ImGui::PushID(1000 + static_cast<int>(i));
            if (ImGui::Selectable(start[i].c_str())) {
                start.erase(start.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    ImGui::EndGroup();
    ImGui::EndDisabled();
    ImGui::TextDisabled("Click an item to move it to the other list.");
    if (ImGui::Button("Reset Gear")) {
        CVarClear(cvar_of("starting_gear").c_str());
        CVarSaveNextFrame();
    }
    if (changed) {
        std::sort(start.begin(), start.end());
        CVarSetString(cvar_of("starting_gear").c_str(), join(start).c_str());
        CVarSaveNextFrame();
    }
}

void draw_general() {
    std::string result;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        result = g_result;
    }
    ImGui::TextDisabled("Seeds are made by the Wind Waker Randomizer (LagoLunatic, MIT) with these settings.");
    ImGui::SeparatorText("Seed Entry");
    char seed[64];
    std::snprintf(seed, sizeof seed, "%s", CVarGetString("gRandomizer.Seed", ""));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 2 - ImGui::GetStyle().ItemSpacing.x * 2);
    if (ImGui::InputTextWithHint("##seed", "Leave blank for a random seed", seed, 43,
                                 ImGuiInputTextFlags_CallbackCharFilter, [](ImGuiInputTextCallbackData* d) {
                                     const ImWchar c = d->EventChar;
                                     return std::isalnum(c) || c == '-' || c == '_' || c == '\'' || c == '%' || c == '.' ? 0 : 1;
                                 })) {
        CVarSetString("gRandomizer.Seed", seed);
        CVarSaveNextFrame();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Letters, digits and - _ ' %% . (wwrando's rule), 42 characters at most.");
    ImGui::SameLine();
    if (ImGui::Button("?##dice", ImVec2(ImGui::GetFrameHeight(), 0))) {
        std::mt19937 rng(std::random_device{}());
        CVarSetString("gRandomizer.Seed", std::to_string(rng() % 9000000000ull + 1000000000ull).c_str());
        CVarSaveNextFrame();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A new random seed.");
    ImGui::SameLine();
    if (ImGui::Button("X##erase", ImVec2(ImGui::GetFrameHeight(), 0))) {
        CVarSetString("gRandomizer.Seed", "");
        CVarSaveNextFrame();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(g_generating || g_options.empty());
    if (ImGui::Button(g_generating ? "Generating..." : "Generate Randomizer", ImVec2(250 * ImGui::GetFontSize() / 16.f, 0)))
        start_generation();
    ImGui::SameLine();
    if (ImGui::Button("Randomize All Settings"))
        randomize_all_settings();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Every checkbox and choice set at random (not the starting items or the counts).");
    if (!result.empty())
        ImGui::TextWrapped("%s", result.c_str());
    const std::string spoiler = CVarGetString("gRandomizer.SpoilerLog", "");
    ImGui::TextWrapped("Spoiler File: %s", spoiler.empty() ? "(none)" : spoiler.c_str());
    if (!spoiler.empty() && ImGui::Button("Open Spoiler Log"))
        ShellExecuteW(nullptr, L"open", fs::u8path(spoiler).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    const std::string permalink = CVarGetString("gRandomizer.Permalink", "");
    if (!permalink.empty()) {
        ImGui::TextWrapped("Permalink: %s", permalink.c_str());
        if (ImGui::Button("Copy Permalink"))
            ImGui::SetClipboardText(permalink.c_str());
    }
    const std::string last = CVarGetString("gRandomizer.LastSeed", "");
    const fs::path file = last.empty() ? fs::path() : seed_file_path(last);
    std::error_code fec;
    ImGui::TextWrapped("Seed file: %s", !file.empty() && fs::exists(file, fec) ? utf8(file).c_str() : "(none yet: Generate one)");
    if (!file.empty() && fs::exists(file, fec)) {
        ImGui::TextWrapped("This one .json is the whole seed. Send it to a friend; they drop it on their game window at the file select.");
        if (ImGui::Button("Open Seed File Location")) {
            const std::wstring args = L"/select,\"" + file.wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy Seed File"))
            copy_file_to_clipboard(file);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Puts the file on the clipboard: paste it into a chat or a folder.");
    }
    ImGui::SeparatorText("Playing a Seed");
    if (ImGui::Button("Load Seed File..."))
        open_seed_dialog();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Choose a seed's .json (or a spoiler log) from a folder: the same as dropping it on the "
                          "window at the file select, for where drag and drop does not work (Proton, the Steam Deck).");
    ImGui::TextWrapped("Generated seeds are saved with their spoiler logs in the Randomizer folder. Playing one needs "
                       "the randomizer build of the game, which is not finished yet.");
}

bool g_loaded, g_load_tried;

bool ensure_loaded() {
    if (!g_load_tried) {
        g_load_tried = true;
        g_loaded = load_options();
    }
    return g_loaded;
}

} // namespace

bool EnsureLoaded() { return ensure_loaded(); }
void StartGeneration() {
    g_outcome = 0;
    start_generation();
}
void RandomizeAllSettings() { randomize_all_settings(); }
bool Generating() { return g_generating; }
std::string RebuildOverlay(const std::string& dir, const std::string& permalink) { return rebuild_overlay(fs::u8path(dir), permalink); }
std::string SeedFileOf(const std::string& seed) { return utf8(seed_file_path(seed)); }
std::string LastResult() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_result;
}
int LastOutcome() { return g_outcome; }

void Launch() {
    // BLUEWAKE_TEST_RANDO_GENERATE=1 (testing): generate a seed with the saved settings at launch, as the
    // General page's button does (the result goes to the log).
    if (const char* env = std::getenv("BLUEWAKE_TEST_RANDO_GENERATE"); env != nullptr && env[0] == '1' && ensure_loaded())
        start_generation();
}

void BuildMenu(shipmenu::Menu& m) {
    const bool loaded = ensure_loaded();
    m.AddSidebar(H, "General", 1);
    m.AddWidget(H, "General", 0, "General", WidgetType::Custom).HideInSearch().CustomFunction([loaded](WidgetInfo&) {
        if (!loaded)
            ImGui::TextWrapped("The randomizer's options were not found (assets\\randomizer\\options.json): the game "
                               "was built without the randomizer generator.");
        draw_general();
    });
    if (!loaded)
        return;
    for (const Option& o : g_options)
        if (o.name == "do_not_generate_spoiler_log")
            add_option(m, "General", 0, o);

    m.AddSidebar(H, "Locations", 3);
    m.AddWidget(H, "Locations", 0, "Where progress items can be placed. Unchecked places are still randomized, with "
                                   "items you do not need to beat the game.",
                WidgetType::Text);
    add_group(m, "Locations", "Progression Locations: Where Should Progress Items Be Placed?", 0, 3, nullptr);

    m.AddSidebar(H, "Item Modes", 2);
    add_group(m, "Item Modes", "Item Randomizer Modes", 0, 1, "Item Randomizer Modes");
    add_group(m, "Item Modes", "Required Bosses", 1, 1, "Required Bosses");
    add_group(m, "Item Modes", "Difficulty Options", 1, 1, "Difficulty");

    m.AddSidebar(H, "Entrances", 2);
    add_group(m, "Entrances", "Entrance Randomizer Options", 0, 1, "Entrance Randomizer");
    add_group(m, "Entrances", "Other Randomizers", 1, 1, "Other Randomizers");

    m.AddSidebar(H, "Hints", 1);
    add_group(m, "Hints", "Hint Options", 0, 1, "Hints");

    m.AddSidebar(H, "Tweaks", 2);
    m.AddWidget(H, "Tweaks", 0, "Invert Camera X-Axis, Invert Sea Compass, Remove Title and Ending Videos, Remove Music, "
                                "'Switch' Targeting, Hero Mode, Swift Sail, Instant Text and Reveal Sea Chart are your own "
                                "settings now: Controls, Audio, Quality of Life, Skips & Speed-ups and Items, in every game.",
                WidgetType::Text);
    add_group(m, "Tweaks", "Convenience Tweaks", 0, 1, nullptr);

    m.AddSidebar(H, "Starting Items", 2);
    m.AddWidget(H, "Starting Items", 0, "Starting Items", WidgetType::SeparatorText);
    for (const Option& o : g_options)
        if (o.group.empty() && o.type == "int" && !o.hidden && o.name != "num_starting_triforce_shards" &&
            (o.name.rfind("starting_", 0) == 0 || o.name.rfind("num_extra", 0) == 0))
            add_option(m, "Starting Items", 0, o);
    m.AddWidget(H, "Starting Items", 1, "Starting Gear", WidgetType::SeparatorText);
    m.AddWidget(H, "Starting Items", 1, "Gear", WidgetType::Custom).HideInSearch().CustomFunction([](WidgetInfo&) {
        draw_gear();
    });
}

} // namespace randomizer
