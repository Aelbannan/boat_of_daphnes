// The mods folder: see mods.h. SoH publishes no license; this follows the
// behaviour of its mod menu (docs/fork/research/player-models-colors.md
// section 4.1) with code of its own.
#undef rename // bw_posix_compat.h's, which <filesystem> must not see

#include "mods.h"
#include "rando_flow.h"

#include "rarc.h"
#include "ship_menu.h"
#include "ship/cvar.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>

extern "C" {
#include "gxruntime/dvd.h"
#include "mod_archive.h"
#include "mods_host.h"
#include "player_model.h"
#include "game_state.h"
unsigned long long bluewake_host_retrace_count(void); // main.c
}

namespace mods {
namespace {

namespace fs = std::filesystem;
using shipmenu::WidgetInfo;
using shipmenu::WidgetType;

constexpr const char* kOrderCVar = "gSettings.Mods.Order";       // every entry, lowest priority first, '|'
constexpr const char* kDisabledCVar = "gSettings.Mods.Disabled"; // the entries switched off, '|'
constexpr const char* kEnabledCVar = "gSettings.Mods.Enabled";   // the master switch

// One file an entry gives the disc.
struct Mapped {
    std::string disc;   // the disc path ("res/Object/Link.arc")
    std::string member; // the path in the entry (archive member or relative file)
};

struct Entry {
    std::string name; // the folder's or archive's name in mods\ (the list key)
    fs::path path;
    bool archive = false;
    std::string kind;  // "Player Model", "Disc Files", "Textures", "Empty"
    std::string model; // a player model's name
    std::vector<Mapped> files;
    std::string note;  // why parts are not used, shown in the list
    bool enabled = true;
};

std::mutex g_mutex; // the list, against the host's disc-ready call
fs::path g_folder;
bool g_read_only; // a test folder: nothing written there
std::vector<Entry> g_entries; // lowest priority first
std::set<std::string> g_applied; // disc paths the last Apply set
std::map<std::string, std::string> g_winner; // disc path -> the entry that gives it
bool g_disc_open;
bool g_dirty; // the list changed since the last Apply
std::string g_status;
std::string g_link_from = "-"; // the entry Link.arc came from at the last Apply ("" the disc's)
std::string g_color_model;     // the player model entry whose Link.arc the last Apply put in effect ("" none)
void (*g_model_listener)();

// Link.arc's room in the archive heap. With a player model in the list at
// start, every Link.arc is served at the game's expanded size plus wwrando's
// 0.202 MiB allowance (customizer.py:23-30), zero-padded, so a model swapped
// in during play fits where the last one was. Without one, the game's own
// file is left alone and a model fits only within its expanded size.
constexpr size_t kLinkGame = 1308608u;
constexpr size_t kLinkReserved = (kLinkGame + 211812u + 31u) & ~size_t{31};
constexpr const char* kLinkArc = "res/Object/Link.arc";
size_t g_link_slot; // set at the first Apply

bool is_player_archive(const std::string& disc) {
    return disc == kLinkArc || disc == "res/Object/LkAnm.arc" || disc == "res/Object/LkD00.arc" ||
           disc == "res/Object/LkD01.arc" || disc == "res/Object/Ship.arc";
}

// A disc file as the game reads it (Yaz0 expanded).
std::vector<unsigned char> disc_file(const char* path) {
    u32 size = 0;
    std::vector<unsigned char> out;
    if (u8* data = dvd_read_image_file(path, &size)) {
        out.assign(data, data + size);
        std::free(data);
    }
    rarc::Yaz0Expand(out);
    return out;
}

// A model's archive as wwrando installs it: its bone animations (.bck) are
// the game's, since they can change gameplay (customizer.py:313-320); Link.arc
// padded to its slot. *fits: it fits the slot loaded now (a live swap).
void prepare_player_archive(const std::string& disc, std::vector<unsigned char>& bytes, std::string& note,
                            bool* fits) {
    rarc::Yaz0Expand(bytes);
    const std::vector<unsigned char> vanilla = disc_file(disc.c_str());
    int reverted = 0;
    if (!vanilla.empty() &&
        !rarc::ReplaceMembers(bytes, vanilla, [](const std::string& n) {
            return n.size() > 4 && (n.compare(n.size() - 4, 4, ".bck") == 0 || n.compare(n.size() - 4, 4, ".BCK") == 0);
        }, &reverted))
        note = disc + ": its animations could not be checked (not a plain RARC); used as is.";
    if (reverted)
        std::fprintf(stderr, "[mods] %s: %d bone animations are the game's\n", disc.c_str(), reverted);
    if (disc == kLinkArc) {
        *fits = bytes.size() <= g_link_slot;
        if (*fits)
            bytes.resize(g_link_slot, 0);
        else if (bytes.size() > kLinkReserved)
            note = "Link.arc is more than 0.2 MiB larger than the game's, which wwrando does not allow; it may not fit.";
    }
}

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= s.size()) {
        const size_t bar = s.find('|', at);
        const std::string item = s.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
        if (!item.empty())
            out.push_back(item);
        if (bar == std::string::npos)
            break;
        at = bar + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& items) {
    std::string s;
    for (const auto& i : items)
        s += (s.empty() ? "" : "|") + i;
    return s;
}

bool is_archive(const fs::path& p) {
    const std::string ext = lower(p.extension().string());
    return ext == ".zip" || ext == ".rar" || ext == ".7z";
}

fs::path from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string utf8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// The files of an entry, relative with '/'.
std::vector<std::string> list_files(const Entry& e) {
    std::vector<std::string> out;
    if (e.archive) {
        if (ModArchive* a = mod_archive_open(utf8(e.path).c_str())) {
            for (unsigned i = 0; i < mod_archive_count(a); ++i)
                out.push_back(mod_archive_name(a, i));
            mod_archive_close(a);
        }
        return out;
    }
    std::error_code ec;
    for (fs::recursive_directory_iterator it(e.path, fs::directory_options::follow_directory_symlink, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            std::string rel = utf8(fs::relative(it->path(), e.path, ec));
            std::replace(rel.begin(), rel.end(), '\\', '/');
            out.push_back(rel);
        }
    }
    return out;
}

std::string dir_of(const std::string& p) {
    const size_t slash = p.rfind('/');
    return slash == std::string::npos ? "" : p.substr(0, slash + 1);
}

std::string base_of(const std::string& p) {
    const size_t slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

// What an entry gives: a wwrando player model (the files wwrando's installer
// writes, customizer.py:198-311), or disc files under res/ or Audiores/.
void classify(Entry& e) {
    const std::vector<std::string> files = list_files(e);
    std::map<std::string, std::string> by_lower; // lower path -> path
    for (const auto& f : files)
        by_lower[lower(f)] = f;
    // A model: the shallowest folder with Link.arc.
    std::string root;
    bool found = false;
    for (const auto& [low, f] : by_lower) {
        if (base_of(low) != "link.arc")
            continue;
        const std::string dir = dir_of(f);
        if (!found || std::count(dir.begin(), dir.end(), '/') < std::count(root.begin(), root.end(), '/'))
            root = dir;
        found = true;
    }
    if (found) {
        e.kind = "Player Model";
        e.model = root.empty() ? fs::path(e.name).stem().string() : base_of(root.substr(0, root.size() - 1));
        static const std::pair<const char*, const char*> kModelFiles[] = {
            {"Link.arc", "res/Object/Link.arc"},
            {"LkAnm.arc", "res/Object/LkAnm.arc"},
            {"LkD00.arc", "res/Object/LkD00.arc"},
            {"LkD01.arc", "res/Object/LkD01.arc"},
            {"Ship.arc", "res/Object/Ship.arc"},
            {"sound/JaiInit.aaf", "Audiores/JaiInit.aaf"},
            {"sound/voice_0.aw", "Audiores/Banks/voice_0.aw"},
            {"sound/GanonT_0.aw", "Audiores/Banks/GanonT_0.aw"},
        };
        for (const auto& [rel, disc] : kModelFiles) {
            const auto it = by_lower.find(lower(root + rel));
            if (it != by_lower.end())
                e.files.push_back({disc, it->second});
        }
        // wwrando needs both voice files together (customizer.py:296-311).
        const auto has = [&](const char* disc) {
            return std::any_of(e.files.begin(), e.files.end(), [&](const Mapped& m) { return m.disc == disc; });
        };
        if (has("Audiores/JaiInit.aaf") != has("Audiores/Banks/voice_0.aw")) {
            e.files.erase(std::remove_if(e.files.begin(), e.files.end(),
                                         [](const Mapped& m) { return m.disc.rfind("Audiores/", 0) == 0; }),
                          e.files.end());
            e.note = "Voice skipped: it needs both JaiInit.aaf and voice_0.aw.";
        }
        return;
    }
    // Disc files: from a res/ or Audiores/ folder at any depth (an optional
    // files/ above it, as in an extracted disc).
    bool textures = false;
    for (const auto& f : files) {
        const std::string low = lower(f);
        size_t at = std::string::npos;
        for (const char* top : {"res/", "audiores/"}) {
            const size_t p = low.rfind(top) == 0 ? 0 : low.find(std::string("/") + top);
            if (p != std::string::npos) {
                at = p == 0 ? 0 : p + 1;
                break;
            }
        }
        if (at != std::string::npos) {
            std::string disc = f.substr(at);
            if (lower(disc).rfind("audiores/", 0) == 0)
                disc = "Audiores/" + disc.substr(9);
            e.files.push_back({disc, f});
        } else if (base_of(low).rfind("tex1_", 0) == 0) {
            textures = true;
        }
    }
    if (!e.files.empty())
        e.kind = "Disc Files";
    else if (textures) {
        e.kind = "Textures";
        e.note = "Texture packs go in mods\\textures.";
    } else {
        e.kind = "Empty";
        e.note = "No disc files or player model found.";
    }
}

void save_order() {
    std::vector<std::string> order, disabled;
    for (const auto& e : g_entries) {
        order.push_back(e.name);
        if (!e.enabled)
            disabled.push_back(e.name);
    }
    CVarSetString(kOrderCVar, join(order).c_str());
    CVarSetString(kDisabledCVar, join(disabled).c_str());
    CVarSaveNextFrame();
}

// mods\ again: entries in the saved order, new ones on top (enabled, as in
// SoH), vanished ones dropped.
void scan() {
    std::error_code ec;
    const fs::path note = g_folder / "custom_mod_files_go_here.txt";
    if (!g_read_only) {
        fs::create_directories(g_folder, ec);
        if (!fs::exists(note, ec))
            std::ofstream(note) << "Folders and .zip, .rar or .7z archives here are mods (Settings > Mod Menu).\n"
                                   "Dolphin-format texture packs go in the textures folder.\n";
    }
    std::vector<Entry> found;
    for (fs::directory_iterator it(g_folder, ec), end; !ec && it != end; it.increment(ec)) {
        Entry e;
        e.path = it->path();
        e.name = utf8(it->path().filename());
        if (it->is_directory(ec)) {
            if (lower(e.name) == "textures")
                continue;
        } else if (!is_archive(e.path)) {
            continue;
        }
        e.archive = !it->is_directory(ec);
        found.push_back(std::move(e));
    }
    const std::vector<std::string> order = split(CVarGetString(kOrderCVar, ""));
    const std::vector<std::string> off = split(CVarGetString(kDisabledCVar, ""));
    const auto rank = [&](const std::string& name) {
        const auto it = std::find(order.begin(), order.end(), name);
        return it == order.end() ? order.size() : static_cast<size_t>(it - order.begin());
    };
    std::stable_sort(found.begin(), found.end(), [&](const Entry& a, const Entry& b) {
        const size_t ra = rank(a.name), rb = rank(b.name);
        return ra != rb ? ra < rb : lower(a.name) < lower(b.name);
    });
    for (auto& e : found) {
        e.enabled = std::find(off.begin(), off.end(), e.name) == off.end();
        classify(e);
    }
    g_entries = std::move(found);
    save_order();
}

// The overlay as the list says: each disc path from the highest enabled
// entry that has it. Paths no longer given return to the disc's.
void apply() {
    std::map<std::string, std::pair<const Entry*, std::string>> want; // disc path -> entry, member
    if (g_link_slot == 0) // the first Apply, before the game runs: Link's room for this session
        g_link_slot = std::any_of(g_entries.begin(), g_entries.end(),
                                  [](const Entry& e) { return e.kind == "Player Model"; })
                          ? kLinkReserved
                          : kLinkGame;
    bool link_fits = true;
    // One player model at a time: the highest enabled one (its files go
    // together; a lower model's voice must not mix with it).
    const Entry* model = nullptr;
    for (const auto& e : g_entries)
        if (e.enabled && e.kind == "Player Model")
            model = &e;
    if (CVarGetInteger(kEnabledCVar, 1))
        for (const auto& e : g_entries)
            if (e.enabled) {
                if (e.kind == "Player Model" && &e != model) {
                    std::fprintf(stderr, "[mods] %s: not used, %s is the player model above it\n", e.name.c_str(),
                                 model->name.c_str());
                    continue;
                }
                for (const auto& f : e.files) {
                    const auto old = want.find(f.disc);
                    if (old != want.end())
                        std::fprintf(stderr, "[mods] %s: %s overrides %s\n", f.disc.c_str(), e.name.c_str(),
                                     old->second.first->name.c_str());
                    want[f.disc] = {&e, f.member};
                }
            }
    for (const auto& path : g_applied)
        if (!want.count(path))
            dvd_overlay_clear(path.c_str());
    g_applied.clear();
    g_winner.clear();
    int ok = 0, failed = 0;
    std::map<const Entry*, ModArchive*> open;
    for (const auto& [disc, source] : want) {
        const Entry* e = source.first;
        std::vector<unsigned char> bytes;
        if (e->archive) {
            ModArchive*& a = open[e];
            if (a == nullptr)
                a = mod_archive_open(utf8(e->path).c_str());
            const int index = a ? mod_archive_find(a, source.second.c_str()) : -1;
            unsigned long long size = 0;
            if (unsigned char* data = index >= 0 ? mod_archive_read(a, static_cast<unsigned>(index), &size) : nullptr) {
                bytes.assign(data, data + size);
                std::free(data);
            }
        } else {
            std::ifstream in(e->path / from_utf8(source.second), std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::string note;
        bool unused_fits = true;
        if (!bytes.empty() && e->kind == "Player Model" &&
            is_player_archive(disc))
            prepare_player_archive(disc, bytes, note, disc == kLinkArc ? &link_fits : &unused_fits);
        if (!note.empty())
            std::fprintf(stderr, "[mods] %s: %s\n", e->name.c_str(), note.c_str());
        if (!bytes.empty() && dvd_overlay_set(disc.c_str(), bytes.data(), static_cast<u32>(bytes.size()))) {
            g_applied.insert(disc);
            g_winner[disc] = e->name;
            ++ok;
        } else {
            std::fprintf(stderr, "[mods] %s from %s: not applied (not on the disc, or unreadable)\n", disc.c_str(),
                         e->name.c_str());
            ++failed;
        }
    }
    for (auto& [e, a] : open)
        if (a != nullptr)
            mod_archive_close(a);
    // Without a model, the game's Link.arc in the reserved slot (when there is one).
    if (!g_applied.count(kLinkArc) && g_link_slot == kLinkReserved) {
        std::vector<unsigned char> link = disc_file(kLinkArc);
        if (!link.empty() && link.size() <= g_link_slot) {
            link.resize(g_link_slot, 0);
            if (dvd_overlay_set(kLinkArc, link.data(), static_cast<u32>(link.size())))
                g_applied.insert(kLinkArc);
        }
    }
    const auto w = g_winner.find(kLinkArc);
    const std::string link_from = w != g_winner.end() ? w->second : "";
    std::string model_note;
    if (g_link_from != "-" && link_from != g_link_from) {
        if (bluewake_player_model_live() && link_fits) {
            bluewake_player_model_swap();
            model_note = "; Link changes as the area reloads";
        } else {
            model_note = "; Link's model changes at the next start";
        }
    }
    g_link_from = link_from;
    // The colors follow the model whose Link.arc is in effect.
    const auto lw = std::find_if(g_entries.begin(), g_entries.end(),
                                 [&](const Entry& e) { return e.name == link_from && e.kind == "Player Model"; });
    const std::string color_model = lw != g_entries.end() ? lw->name : "";
    if (color_model != g_color_model) {
        g_color_model = color_model;
        if (g_model_listener)
            g_model_listener();
    }
    char buf[200];
    std::snprintf(buf, sizeof buf, "%d file%s from mods in use%s%s", ok, ok == 1 ? "" : "s",
                  failed ? "; some files were not applied (see the log)" : "", model_note.c_str());
    g_status = buf;
    g_dirty = false;
    std::fprintf(stderr, "[mods] %s\n", g_status.c_str());
}

void on_disc_ready() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_disc_open = true;
    apply();
    randoflow::OnDiscReady();
}

void draw_list() {
    std::lock_guard<std::mutex> lock(g_mutex);
    ImGui::TextWrapped("Mod priority is top to bottom: a mod overrides the ones below it.");
    if (g_entries.empty()) {
        ImGui::TextDisabled("No mods. Put folders or .zip, .rar or .7z archives in the mods folder, then Rescan.");
        return;
    }
    int move_from = -1, move_to = -1;
    if (ImGui::BeginTable("##mods", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Mod");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        const int n = static_cast<int>(g_entries.size());
        for (int row = 0; row < n; ++row) {
            const int i = n - 1 - row; // shown highest first
            Entry& e = g_entries[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##on", &e.enabled)) {
                save_order();
                g_dirty = true;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.name.c_str());
            if (ImGui::IsItemHovered()) {
                std::string tip = e.model.empty() ? e.name : e.name + " (" + e.model + ")";
                for (const auto& f : e.files) {
                    const auto w = g_winner.find(f.disc);
                    tip += "\n" + f.disc;
                    if (w != g_winner.end() && w->second != e.name)
                        tip += "  (overridden by " + w->second + ")";
                }
                if (!e.note.empty())
                    tip += "\n" + e.note;
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.kind.c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(row == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up))
                move_from = i, move_to = i + 1;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(row == n - 1);
            if (ImGui::ArrowButton("##down", ImGuiDir_Down))
                move_from = i, move_to = i - 1;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (move_from >= 0) {
        std::swap(g_entries[static_cast<size_t>(move_from)], g_entries[static_cast<size_t>(move_to)]);
        save_order();
        g_dirty = true;
    }
}

} // namespace

void Launch(const std::string& folder, bool read_only) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_folder = from_utf8(folder);
        g_read_only = read_only;
        scan();
    }
    bluewake_mods_set_disc_ready(on_disc_ready);
    // BLUEWAKE_TEST_MODS_TOP=<retrace>:<entry> (testing): at that retrace, the entry goes on
    // top of the list and Apply runs, as the Mod Menu does (the player model swap without a restart).
    if (const char* env = std::getenv("BLUEWAKE_TEST_MODS_TOP"); env != nullptr && std::strchr(env, ':') != nullptr) {
        const std::string spec = env;
        const unsigned long long retrace = std::strtoull(spec.c_str(), nullptr, 10);
        const std::string name = spec.substr(spec.find(':') + 1);
        std::thread([retrace, name] {
            while (bluewake_host_retrace_count() < retrace) // a retrace number (the scenarios' clock)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::lock_guard<std::mutex> lock(g_mutex);
            const auto it = std::find_if(g_entries.begin(), g_entries.end(), [&](const Entry& e) { return e.name == name; });
            if (it == g_entries.end() || !g_disc_open)
                return;
            std::rotate(it, it + 1, g_entries.end());
            std::fprintf(stderr, "[mods] test: %s on top, Apply\n", name.c_str());
            apply();
        }).detach();
    }
}

std::string ActiveModel() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!CVarGetInteger(kEnabledCVar, 1))
        return "";
    for (auto it = g_entries.rbegin(); it != g_entries.rend(); ++it)
        if (it->enabled && it->kind == "Player Model")
            return it->model;
    return "";
}

// A model's files for its colors: Link.arc, metadata.txt and color_masks\*.png, next to the Link.arc in use.
bool ActiveModelFiles(model_colors::Files& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = std::find_if(g_entries.begin(), g_entries.end(),
                                 [](const Entry& e) { return e.name == g_color_model && e.kind == "Player Model"; });
    if (g_color_model.empty() || it == g_entries.end())
        return false;
    const Entry& e = *it;
    const auto link = std::find_if(e.files.begin(), e.files.end(), [](const Mapped& m) { return m.disc == kLinkArc; });
    if (link == e.files.end())
        return false;
    const std::string root = lower(dir_of(link->member));
    std::vector<std::string> wanted; // the entry's files, in its own spelling
    for (const std::string& f : list_files(e)) {
        const std::string low = lower(f);
        if (low == lower(link->member) || low == root + "metadata.txt" ||
            (low.rfind(root + "color_masks/", 0) == 0 && low.size() > 4 && low.compare(low.size() - 4, 4, ".png") == 0))
            wanted.push_back(f);
    }
    ModArchive* a = e.archive ? mod_archive_open(utf8(e.path).c_str()) : nullptr;
    if (e.archive && a == nullptr)
        return false;
    // Archives are read in the order of their entries (a RAR or 7z is one stream).
    std::vector<std::pair<int, std::string>> order;
    for (const std::string& f : wanted)
        order.emplace_back(a ? mod_archive_find(a, f.c_str()) : 0, f);
    std::sort(order.begin(), order.end());
    out = model_colors::Files{};
    out.name = e.model;
    for (const auto& [index, f] : order) {
        std::vector<unsigned char> bytes;
        if (a) {
            unsigned long long size = 0;
            if (unsigned char* data = index >= 0 ? mod_archive_read(a, static_cast<unsigned>(index), &size) : nullptr) {
                bytes.assign(data, data + size);
                std::free(data);
            }
        } else {
            std::ifstream in(e.path / from_utf8(f), std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const std::string low = lower(f);
        if (low == lower(link->member))
            out.link_arc = std::move(bytes);
        else if (low == root + "metadata.txt")
            out.metadata.assign(bytes.begin(), bytes.end());
        else
            out.masks[low.substr(root.size())] = std::move(bytes);
    }
    if (a)
        mod_archive_close(a);
    return !out.link_arc.empty();
}

bool ActivePlayerPack(std::string& name, std::vector<PackFile>& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = std::find_if(g_entries.begin(), g_entries.end(),
                                 [](const Entry& e) { return e.name == g_color_model && e.kind == "Player Model"; });
    if (g_color_model.empty() || it == g_entries.end())
        return false;
    const Entry& e = *it;
    const auto link = std::find_if(e.files.begin(), e.files.end(), [](const Mapped& m) { return m.disc == kLinkArc; });
    if (link == e.files.end())
        return false;
    const std::string root = lower(dir_of(link->member));
    std::vector<std::pair<std::string, std::string>> wanted; // role, the file in its own spelling
    for (const std::string& f : list_files(e)) {
        const std::string low = lower(f);
        if (low.rfind(root, 0) != 0)
            continue;
        const std::string rel = low.substr(root.size());
        if (low == lower(link->member))
            wanted.emplace_back("model", f);
        else if (rel == "metadata.txt")
            wanted.emplace_back("metadata", f);
        else if (rel.rfind("color_masks/", 0) == 0 && rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".png") == 0)
            wanted.emplace_back("colors", f);
        else if (rel.rfind("sound/", 0) == 0 && rel.size() > 7)
            wanted.emplace_back("voice", f);
        else if (rel.rfind("equipment/", 0) == 0 && rel.size() > 11)
            wanted.emplace_back("equipment", f);
    }
    ModArchive* a = e.archive ? mod_archive_open(utf8(e.path).c_str()) : nullptr;
    if (e.archive && a == nullptr)
        return false;
    std::vector<std::tuple<int, std::string, std::string>> order; // archives are read in their own order
    for (const auto& [role, f] : wanted)
        order.emplace_back(a ? mod_archive_find(a, f.c_str()) : 0, role, f);
    std::sort(order.begin(), order.end());
    out.clear();
    name = e.model;
    for (const auto& [index, role, f] : order) {
        PackFile file;
        file.role = role;
        file.path = f.substr(root.size());
        if (a) {
            unsigned long long size = 0;
            if (unsigned char* data = index >= 0 ? mod_archive_read(a, static_cast<unsigned>(index), &size) : nullptr) {
                if (size <= 20ull * 1024 * 1024)
                    file.bytes.assign(data, data + size);
                std::free(data);
            }
        } else {
            std::error_code ec;
            if (fs::file_size(e.path / from_utf8(f), ec) <= 20ull * 1024 * 1024) {
                std::ifstream in(e.path / from_utf8(f), std::ios::binary);
                file.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            }
        }
        if (!file.bytes.empty())
            out.push_back(std::move(file));
    }
    if (a)
        mod_archive_close(a);
    return std::any_of(out.begin(), out.end(), [](const PackFile& f) { return f.role == "model"; });
}

void SetModelListener(void (*fn)()) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_model_listener = fn;
}

void BuildMenu(shipmenu::Menu& m, const char* H) {
    m.AddWidget(H, "Mod Menu", 0, "Mods", WidgetType::SeparatorText);
    m.AddWidget(H, "Mod Menu", 0, "Enable Mods", WidgetType::CVarCheckbox)
        .CVar(kEnabledCVar)
        .DefaultValue(1)
        .Tooltip("Uses the enabled mods in the list. Off, the game reads only its disc.")
        .Callback([](WidgetInfo&) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_dirty = true;
        });
    m.AddWidget(H, "Mod Menu", 0, "Mod List", WidgetType::Custom).HideInSearch().CustomFunction([](WidgetInfo&) {
        draw_list();
        std::lock_guard<std::mutex> lock(g_mutex);
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Rescan", ImVec2(half, 0))) {
            scan();
            g_dirty = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!g_disc_open);
        if (ImGui::Button(g_dirty ? "Apply *" : "Apply", ImVec2(half, 0)))
            apply();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Puts the list in effect. A file the game has loaded already changes when it loads it "
                              "again (the next area for most; Link's model at the next game start).");
        if (!g_status.empty())
            ImGui::TextDisabled("%s", g_status.c_str());
    });
}

} // namespace mods
