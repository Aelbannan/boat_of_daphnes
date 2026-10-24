// See fs_art.h.
#ifdef rename
#undef rename  // bw_posix_compat.h's, which <filesystem> must not see
#endif

#include "fs_art.h"

#include "gx_texture.h"
#include "item_found.h"
#include "rarc.h"

#include <aurora/imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

extern "C" {
#include "gxruntime/dvd.h"
}

namespace fsart {
namespace {

namespace fs = std::filesystem;

constexpr const char* kLogoArc = "res/Object/TlogoE.arc";
constexpr const char* kMainMember = "logo_zelda_main.bti";
constexpr const char* kSubMember = "subtitle_start_anim_e.bdl";

std::vector<uint8_t> g_stock;                 // the disc's TlogoE.arc, expanded
std::vector<fs::path> g_rando_sources;
std::vector<uint8_t> g_rando;                 // wwrando's, expanded ("" until found)
Logo g_logo[2];
bool g_made[2];

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

// The image of one texture header (a BTI, or a TEX1 entry: the same 32 bytes) at `at` in blob.
Image upload(const std::vector<uint8_t>& blob, size_t at, const char* what) {
    gx_texture::Texture t;
    std::string error;
    std::vector<uint8_t> rgba;
    if (!gx_texture::Parse(blob.data(), blob.size(), at, t, &error) || !gx_texture::DecodeRgba(t, rgba, &error)) {
        std::fprintf(stderr, "[fileselect] %s: %s\n", what, error.c_str());
        return {};
    }
    Image image;
    image.tex = aurora_imgui_add_texture(t.width, t.height, rgba.data());
    image.w = static_cast<int>(t.width);
    image.h = static_cast<int>(t.height);
    return image;
}

// The texture `index` of a J3D model's TEX1 chunk: where its header sits in the model's bytes, or 0.
size_t tex1_header(const std::vector<uint8_t>& bdl, unsigned index) {
    if (bdl.size() < 0x40 || bdl[0] != 'J' || bdl[1] != '3' || bdl[2] != 'D')
        return 0;
    const uint32_t chunks = be32(&bdl[0x0C]);
    size_t at = 0x20;
    for (uint32_t i = 0; i < chunks && at + 0x14 <= bdl.size(); ++i) {
        const uint32_t size = be32(&bdl[at + 4]);
        if (size < 8)
            return 0;
        if (std::memcmp(&bdl[at], "TEX1", 4) == 0) {
            const unsigned count = be16(&bdl[at + 8]);
            const uint32_t headers = be32(&bdl[at + 0x0C]);
            if (index >= count)
                return 0;
            return at + headers + size_t(index) * 0x20;
        }
        at += size;
    }
    return 0;
}

bool read_file(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

// A member of an expanded archive, itself expanded when it was compressed.
bool member(const std::vector<uint8_t>& arc, const char* name, std::vector<uint8_t>& out) {
    if (!rarc::Extract(arc, name, out))
        return false;
    rarc::Yaz0Expand(out);
    return !out.empty();
}

bool make(int quest) {
    const std::vector<uint8_t>* arc = nullptr;
    if (quest == 0) {
        arc = &g_stock;
    } else {
        if (g_rando.empty())
            for (const fs::path& p : g_rando_sources) {
                std::error_code ec;
                if (fs::exists(p, ec) && read_file(p, g_rando)) {
                    rarc::Yaz0Expand(g_rando);
                    std::fprintf(stderr, "[fileselect] wwrando's logo from %s\n", p.string().c_str());
                    break;
                }
            }
        arc = &g_rando;
    }
    if (arc->empty())
        return false;
    std::vector<uint8_t> main, sub;
    if (!member(*arc, kMainMember, main) || !member(*arc, kSubMember, sub)) {
        std::fprintf(stderr, "[fileselect] the %s title archive has no logo members\n", quest ? "randomizer" : "stock");
        return false;
    }
    Logo logo;
    logo.main = upload(main, 0, "logo_zelda_main.bti");
    logo.sub = upload(sub, tex1_header(sub, 1), "subtitle");
    g_logo[quest] = logo;
    return logo.ready();
}

} // namespace

void CaptureStock() {
    if (!g_stock.empty())
        return;
    u32 size = 0;
    u8* data = dvd_read_image_file(kLogoArc, &size);
    if (data == nullptr)
        return;
    g_stock.assign(data, data + size);
    std::free(data);
    rarc::Yaz0Expand(g_stock);
}

void SetRandoSources(const std::vector<fs::path>& candidates) {
    g_rando_sources = candidates;
    g_rando.clear();
    g_made[1] = false;
}

const Logo& GetLogo(int quest) {
    quest = quest != 0 ? 1 : 0;
    if (!g_made[quest]) {
        if (quest == 0 && g_stock.empty())
            CaptureStock();
        g_made[quest] = make(quest);
        if (!g_made[quest] && !g_logo[quest].ready())
            return g_logo[quest];  // tried again next frame (the files may not be there yet)
    }
    return g_logo[quest];
}

Image HashIcon(const std::string& word) {
    // Item icons the hash can show (all in the disc's itemicon.arc): the word picks one, the same every time.
    static const char* const kPool[] = {
        "sword_01.bti", "shield_01.bti", "boomerang.bti", "hookshot.bti", "bow_01.bti", "bomb_00.bti", "hammer_01.bti",
        "telescope.bti", "whistle.bti", "baton.bti", "camera.bti", "boots_00.bti", "gloves_00.bti", "fan.bti",
        "bottle_01.bti", "bottle_03.bti", "bottle_05.bti", "bottle_07.bti", "heart_up_01.bti", "heart_up_02.bti",
        "get_key.bti", "boss_key.bti", "compass.bti", "dungeon_map.bti", "sail_00.bti", "rope.bti", "amulet_00.bti",
        "triforce_03.bti", "god_symbol_00.bti", "god_symbol_01.bti", "god_symbol_02.bti", "arrowcase_2.bti",
    };
    unsigned h = 2166136261u;
    for (unsigned char c : word)
        h = (h ^ c) * 16777619u;
    const char* file = kPool[h % (sizeof kPool / sizeof kPool[0])];
    Image image;
    image.tex = item_found::IconFile(file, false);
    image.w = image.h = 32;
    return image;
}

} // namespace fsart
