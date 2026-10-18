// HUD button textures: see hud_colors.h.
// The Windows build force-includes a POSIX layer whose rename() macro breaks
// <filesystem> (which aurora's headers include).
#ifdef rename
#undef rename
#endif

#include "hud_colors.h"

#include "rarc.h"

#include <aurora/texture.hpp>
#include <dolphin/gx/GXEnum.h>
#include <xxhash.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include "gxruntime/dvd.h"
bool bluewake_cosmetics_get(const char* id, uint8_t rgba[4]);
}

namespace hud_colors {
namespace {

struct Button {
    const char* id;     // the cosmetics id (gCosmetics.<id>)
    const char* member; // in res/Msg/menures.arc
    uint32_t base;      // the texture's color (its saturated pixels' mean), the editor's default
    // Loaded:
    aurora::texture::TextureSourceKey key{};
    std::vector<uint8_t> rgba;
    bool loaded = false;
    // Applied:
    uint32_t applied = 0xFFFFFFFFu; // the color registered, or none
    std::vector<uint8_t> live;      // the registered pixels (the renderer keeps a view)
    std::optional<aurora::texture::ReplacementRegistration> registered;
};

Button g_buttons[] = {
    {"Hud.AButton", "ba_base_04.bti", 0x35C39Bu},
    {"Hud.BButton", "ba_base_02.bti", 0xFA3A1Cu},
    {"Hud.ZButton", "ba_base_z.bti", 0x9839D3u},
};
bool g_tried;

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

void palette_color(uint16_t v, uint8_t format, uint8_t* out) {
    switch (format) {
    case 0: // IA8
        out[0] = out[1] = out[2] = uint8_t(v);
        out[3] = uint8_t(v >> 8);
        break;
    case 1: // RGB565
        out[0] = uint8_t((v >> 11 & 31) << 3 | (v >> 13 & 7));
        out[1] = uint8_t((v >> 5 & 63) << 2 | (v >> 9 & 3));
        out[2] = uint8_t((v & 31) << 3 | (v >> 2 & 7));
        out[3] = 255;
        break;
    default: // RGB5A3
        if (v & 0x8000) {
            out[0] = uint8_t((v >> 10 & 31) << 3 | (v >> 12 & 7));
            out[1] = uint8_t((v >> 5 & 31) << 3 | (v >> 7 & 7));
            out[2] = uint8_t((v & 31) << 3 | (v >> 2 & 7));
            out[3] = 255;
        } else {
            out[0] = uint8_t((v >> 8 & 15) * 17);
            out[1] = uint8_t((v >> 4 & 15) * 17);
            out[2] = uint8_t((v & 15) * 17);
            const uint8_t a = uint8_t(v >> 12 & 7);
            out[3] = uint8_t(a << 5 | a << 2 | a >> 1);
        }
        break;
    }
}

// A C4 or C8 BTI: its RGBA pixels and the renderer's replacement key (the
// rules of find_replacement_for_guest, texture_replacement.cpp: XXH64 of the
// base level, and of the palette entries from the lowest index used to the
// highest).
bool load_bti(const uint8_t* bti, size_t size, Button& b) {
    if (size < 32)
        return false;
    const uint8_t format = bti[0];
    const uint32_t w = be16(bti + 2), h = be16(bti + 4);
    const uint8_t palette_format = bti[9];
    const uint32_t colors = be16(bti + 10), pal_off = be32(bti + 12), img_off = be32(bti + 28);
    if (format != GX_TF_C4 && format != GX_TF_C8)
        return false;
    const uint32_t tw = 8, th = format == GX_TF_C4 ? 8 : 4;
    const uint32_t tiles_x = (w + tw - 1) / tw, tiles_y = (h + th - 1) / th;
    const size_t image_size = size_t(tiles_x) * tiles_y * 32;
    if (img_off + image_size > size || pal_off + colors * 2u > size)
        return false;
    const uint8_t* image = bti + img_off;
    const uint8_t* palette = bti + pal_off;
    uint32_t lo = 0xFFFF, hi = 0;
    for (size_t i = 0; i < image_size; ++i) {
        const uint32_t a = format == GX_TF_C4 ? image[i] >> 4 : image[i], c = format == GX_TF_C4 ? image[i] & 15u : a;
        lo = std::min({lo, a, c});
        hi = std::max({hi, a, c});
    }
    if (hi >= colors)
        return false;
    b.key = {};
    b.key.textureHash = XXH64(image, image_size, 0);
    b.key.tlutHash = XXH64(palette + lo * 2, (hi + 1 - lo) * 2, 0);
    b.key.hasTlut = true;
    b.key.width = w;
    b.key.height = h;
    b.key.format = format;
    b.rgba.assign(size_t(w) * h * 4, 0);
    for (uint32_t ty = 0; ty < tiles_y; ++ty)
        for (uint32_t tx = 0; tx < tiles_x; ++tx) {
            const uint8_t* tile = image + (size_t(ty) * tiles_x + tx) * 32;
            for (uint32_t y = 0; y < th; ++y)
                for (uint32_t x = 0; x < tw; ++x) {
                    const uint32_t px = tx * tw + x, py = ty * th + y;
                    const uint32_t i = y * tw + x;
                    const uint32_t index = format == GX_TF_C4 ? (tile[i / 2] >> (i & 1 ? 0 : 4)) & 15u : tile[i];
                    if (px < w && py < h)
                        palette_color(be16(palette + index * 2), palette_format, &b.rgba[(size_t(py) * w + px) * 4]);
                }
        }
    return true;
}

void load() {
    u32 size = 0;
    u8* data = dvd_read_image_file("res/Msg/menures.arc", &size);
    if (data == nullptr)
        return; // the disc is not open yet: again later
    g_tried = true;
    std::vector<uint8_t> arc(data, data + size);
    std::free(data);
    rarc::Yaz0Expand(arc);
    std::vector<rarc::File> files;
    if (!rarc::Parse(arc, files)) {
        std::fprintf(stderr, "[hud-colors] menures.arc not readable: the A, B and Z buttons keep their colors\n");
        return;
    }
    const size_t data_start = 0x20u + be32(&arc[0x0C]);
    for (Button& b : g_buttons)
        for (const auto& f : files)
            if (f.name == b.member && load_bti(&arc[data_start + f.offset], f.size, b))
                b.loaded = true;
}

struct Hsv {
    float h, s, v;
};

Hsv to_hsv(float r, float g, float b) {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    float h = 0.f;
    if (d > 0.f) {
        h = mx == r ? std::fmod((g - b) / d, 6.f) : mx == g ? (b - r) / d + 2.f : (r - g) / d + 4.f;
        h *= 60.f;
        if (h < 0.f)
            h += 360.f;
    }
    return {h, mx > 0.f ? d / mx * 100.f : 0.f, mx * 100.f};
}

Hsv to_hsv(uint32_t rgb) { return to_hsv((rgb >> 16 & 255) / 255.f, (rgb >> 8 & 255) / 255.f, (rgb & 255) / 255.f); }

void from_hsv(Hsv c, uint8_t* out) {
    const float s = std::clamp(c.s, 0.f, 100.f) / 100.f, v = std::clamp(c.v, 0.f, 100.f) / 100.f;
    const float cc = v * s, x = cc * (1.f - std::fabs(std::fmod(c.h / 60.f, 2.f) - 1.f)), m = v - cc;
    float r = 0, g = 0, b = 0;
    switch (static_cast<int>(c.h / 60.f) % 6) {
    case 0: r = cc; g = x; break;
    case 1: r = x; g = cc; break;
    case 2: g = cc; b = x; break;
    case 3: g = x; b = cc; break;
    case 4: r = x; b = cc; break;
    default: r = cc; b = x; break;
    }
    out[0] = static_cast<uint8_t>(std::lround((r + m) * 255.f));
    out[1] = static_cast<uint8_t>(std::lround((g + m) * 255.f));
    out[2] = static_cast<uint8_t>(std::lround((b + m) * 255.f));
}

// wwrando's color exchange (texture_utils.color_exchange; MIT, LagoLunatic)
// on the button's colored pixels; the white letter and rim (low saturation)
// stay as they are.
void apply(Button& b, uint32_t chosen) {
    if (b.registered) {
        aurora::texture::unregister_replacement(*b.registered);
        b.registered.reset();
    }
    b.live.clear();
    b.applied = chosen;
    if (chosen == 0xFFFFFFFFu)
        return;
    b.live = b.rgba;
    const Hsv base = to_hsv(b.base), want = to_hsv(chosen);
    for (size_t i = 0; i + 3 < b.live.size(); i += 4) {
        uint8_t* p = &b.live[i];
        Hsv c = to_hsv(p[0] / 255.f, p[1] / 255.f, p[2] / 255.f);
        if (c.s < 25.f)
            continue;
        c.h = want.h;
        c.s += want.s - base.s;
        c.v += want.v - base.v;
        from_hsv(c, p);
    }
    aurora::texture::RawTextureReplacement raw{};
    raw.bytes = std::span<const uint8_t>(b.live.data(), b.live.size());
    raw.width = b.key.width;
    raw.height = b.key.height;
    raw.mipCount = 1;
    raw.gxFormat = GX_TF_RGBA8_PC;
    raw.label = b.member;
    b.registered = aurora::texture::register_replacement(b.key, raw, {.priority = 1});
}

} // namespace

void Frame() {
    uint32_t want[sizeof g_buttons / sizeof g_buttons[0]];
    bool any = false;
    for (size_t i = 0; i < sizeof g_buttons / sizeof g_buttons[0]; ++i) {
        uint8_t rgba[4];
        want[i] = bluewake_cosmetics_get(g_buttons[i].id, rgba)
                      ? uint32_t(rgba[0]) << 16 | uint32_t(rgba[1]) << 8 | rgba[2]
                      : 0xFFFFFFFFu;
        any |= want[i] != g_buttons[i].applied;
    }
    if (!any)
        return;
    if (!g_tried)
        load();
    for (size_t i = 0; i < sizeof g_buttons / sizeof g_buttons[0]; ++i)
        if (g_buttons[i].loaded && want[i] != g_buttons[i].applied)
            apply(g_buttons[i], want[i]);
}

} // namespace hud_colors
