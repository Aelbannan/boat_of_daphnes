// GameCube texture images: see gx_texture.h.
#include "gx_texture.h"

#include <xxhash.h>

#include <algorithm>
#include <cstdio>

namespace gx_texture {
namespace {

struct Tile {
    uint32_t w, h, bytes;
};

// Tile size of each format: the renderer's base level size (GXGetTexBufferSize, no mipmaps).
bool tile_of(uint8_t format, Tile& t) {
    switch (format) {
    case I4: case C4: case CMPR: t = {8, 8, 32}; return true;
    case I8: case IA4: case C8: t = {8, 4, 32}; return true;
    case IA8: case RGB565: case RGB5A3: case C14X2: t = {4, 4, 32}; return true;
    case RGBA8: t = {4, 4, 64}; return true;
    default: return false;
    }
}

bool is_palette(uint8_t format) { return format == C4 || format == C8 || format == C14X2; }

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t be16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }

void fail(std::string* error, const char* msg) {
    if (error != nullptr)
        *error = msg;
}

// The GX color expansions (swift_sail.py _e3.._e6).
uint8_t e3(uint32_t v) { return uint8_t((v << 5) | (v << 2) | (v >> 1)); }
uint8_t e4(uint32_t v) { return uint8_t((v << 4) | v); }
uint8_t e5(uint32_t v) { return uint8_t((v << 3) | (v >> 2)); }
uint8_t e6(uint32_t v) { return uint8_t((v << 2) | (v >> 4)); }

struct Rgba {
    uint8_t r, g, b, a;
};

Rgba rgb565(uint32_t v) { return {e5((v >> 11) & 31), e6((v >> 5) & 63), e5(v & 31), 255}; }

Rgba rgb5a3(uint32_t v) {
    if (v & 0x8000)
        return {e5((v >> 10) & 31), e5((v >> 5) & 31), e5(v & 31), 255};
    return {e4((v >> 8) & 15), e4((v >> 4) & 15), e4(v & 15), e3((v >> 12) & 7)};
}

// palette_rgba of link_colors.py: format 0 is IA8, 1 RGB565, 2 RGB5A3.
bool palette_colors(const Texture& t, std::vector<Rgba>& out) {
    out.clear();
    for (size_t i = 0; i + 1 < t.palette.size(); i += 2) {
        const uint32_t v = be16(&t.palette[i]);
        switch (t.palette_format) {
        case 0: out.push_back({uint8_t(v & 255), uint8_t(v & 255), uint8_t(v & 255), uint8_t(v >> 8)}); break;
        case 1: out.push_back(rgb565(v)); break;
        case 2: out.push_back(rgb5a3(v)); break;
        default: return false;
        }
    }
    return true;
}

// The palette indices the image uses, lowest and highest (the renderer scans every byte of the base
// level, padding included).
void used_indices(const Texture& t, uint32_t& lo, uint32_t& hi) {
    lo = 0xFFFF;
    hi = 0;
    if (t.format == C4) {
        for (uint8_t b : t.image) {
            lo = std::min({lo, uint32_t(b & 15), uint32_t(b >> 4)});
            hi = std::max({hi, uint32_t(b & 15), uint32_t(b >> 4)});
        }
    } else if (t.format == C8) {
        for (uint8_t b : t.image) {
            lo = std::min(lo, uint32_t(b));
            hi = std::max(hi, uint32_t(b));
        }
    } else { // C14X2: a 14 bit index in each 16 bit word
        for (size_t i = 0; i + 1 < t.image.size(); i += 2) {
            const uint32_t v = be16(&t.image[i]) & 0x3FFF;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
}

// swift_sail.py cmpr_endpoints_colors / decode_cmpr: 8x8 tiles of four 4x4 sub-blocks.
void decode_cmpr(const Texture& t, std::vector<uint8_t>& out) {
    const uint32_t w = t.width, h = t.height;
    size_t pos = 0;
    for (uint32_t ty = 0; ty < h; ty += 8)
        for (uint32_t tx = 0; tx < w; tx += 8)
            for (uint32_t sub = 0; sub < 4; ++sub) {
                const uint32_t sx = tx + (sub % 2) * 4, sy = ty + (sub / 2) * 4;
                const uint32_t c0 = be16(&t.image[pos]), c1 = be16(&t.image[pos + 2]), bits = be32(&t.image[pos + 4]);
                pos += 8;
                const Rgba a = rgb565(c0), b = rgb565(c1);
                Rgba cols[4] = {a, b, {}, {}};
                if (c0 > c1) {
                    cols[2] = {uint8_t((2 * a.r + b.r) / 3), uint8_t((2 * a.g + b.g) / 3), uint8_t((2 * a.b + b.b) / 3), 255};
                    cols[3] = {uint8_t((a.r + 2 * b.r) / 3), uint8_t((a.g + 2 * b.g) / 3), uint8_t((a.b + 2 * b.b) / 3), 255};
                } else {
                    cols[2] = {uint8_t(a.r / 2 + b.r / 2), uint8_t(a.g / 2 + b.g / 2), uint8_t(a.b / 2 + b.b / 2), 255};
                    cols[3] = {0, 0, 0, 0};
                }
                for (uint32_t i = 0; i < 16; ++i) {
                    const uint32_t x = sx + i % 4, y = sy + i / 4;
                    if (x < w && y < h) {
                        const Rgba c = cols[(bits >> ((15 - i) * 2)) & 3];
                        uint8_t* o = &out[(size_t(y) * w + x) * 4];
                        o[0] = c.r;
                        o[1] = c.g;
                        o[2] = c.b;
                        o[3] = c.a;
                    }
                }
            }
}

} // namespace

bool Parse(const uint8_t* blob, size_t size, size_t at, Texture& out, std::string* error) {
    if (at > size || size - at < 32) {
        fail(error, "truncated BTI header");
        return false;
    }
    const uint8_t* h = blob + at;
    out = Texture{};
    out.format = h[0];
    out.width = be16(h + 2);
    out.height = be16(h + 4);
    out.palette_format = h[9];
    const uint32_t num_colors = be16(h + 10), pal_off = be32(h + 12), img_off = be32(h + 28);
    Tile t;
    if (!tile_of(out.format, t)) {
        fail(error, "unsupported GX format");
        return false;
    }
    if (out.width == 0 || out.height == 0) {
        fail(error, "empty texture");
        return false;
    }
    const size_t image_size = size_t((out.width + t.w - 1) / t.w) * ((out.height + t.h - 1) / t.h) * t.bytes;
    if (size_t(img_off) + at > size || size - at - img_off < image_size) {
        fail(error, "truncated BTI image");
        return false;
    }
    out.image.assign(blob + at + img_off, blob + at + img_off + image_size);
    if (is_palette(out.format)) {
        const size_t pal_size = size_t(num_colors) * 2;
        if (size_t(pal_off) + at > size || size - at - pal_off < pal_size) {
            fail(error, "truncated BTI palette");
            return false;
        }
        out.palette.assign(blob + at + pal_off, blob + at + pal_off + pal_size);
    }
    return true;
}

std::string Key(const Texture& t, std::string* error) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "tex1_%ux%u_%016llx", t.width, t.height,
                  static_cast<unsigned long long>(XXH64(t.image.data(), t.image.size(), 0)));
    std::string key = buf;
    if (is_palette(t.format)) {
        uint32_t lo, hi;
        used_indices(t, lo, hi);
        const size_t end = (size_t(hi) + 1) * 2;
        if (lo > hi || end > t.palette.size()) {
            fail(error, "palette too short for the indices the texture uses");
            return "";
        }
        std::snprintf(buf, sizeof buf, "_%016llx",
                      static_cast<unsigned long long>(XXH64(t.palette.data() + lo * 2, end - lo * 2, 0)));
        key += buf;
    }
    key += "_" + std::to_string(t.format);
    return key;
}

bool DecodeRgba(const Texture& t, std::vector<uint8_t>& rgba, std::string* error) {
    Tile tile;
    if (!tile_of(t.format, tile)) {
        fail(error, "unsupported GX format");
        return false;
    }
    const uint32_t w = t.width, h = t.height;
    rgba.assign(size_t(w) * h * 4, 0);
    if (t.format == CMPR) {
        decode_cmpr(t, rgba);
        return true;
    }
    std::vector<Rgba> pal;
    if (is_palette(t.format) && !palette_colors(t, pal)) {
        fail(error, "unsupported palette format");
        return false;
    }
    size_t pos = 0;
    for (uint32_t ty = 0; ty < h; ty += tile.h)
        for (uint32_t tx = 0; tx < w; tx += tile.w) {
            const uint8_t* blk = &t.image[pos];
            pos += tile.bytes;
            for (uint32_t i = 0; i < tile.w * tile.h; ++i) {
                const uint32_t x = tx + i % tile.w, y = ty + i / tile.w;
                if (x >= w || y >= h)
                    continue;
                Rgba px{};
                const uint32_t nib = (i & 1) ? (blk[i >> 1] & 15u) : (blk[i >> 1] >> 4); // 4 bit: high nibble first
                switch (t.format) {
                case I4: px = {e4(nib), e4(nib), e4(nib), e4(nib)}; break;
                case I8: px = {blk[i], blk[i], blk[i], blk[i]}; break;
                case IA4: px = {e4(blk[i] & 15u), e4(blk[i] & 15u), e4(blk[i] & 15u), e4(blk[i] >> 4)}; break;
                case IA8: px = {blk[2 * i + 1], blk[2 * i + 1], blk[2 * i + 1], blk[2 * i]}; break;
                case RGB565: px = rgb565(be16(&blk[2 * i])); break;
                case RGB5A3: px = rgb5a3(be16(&blk[2 * i])); break;
                case RGBA8: px = {blk[2 * i + 1], blk[32 + 2 * i], blk[33 + 2 * i], blk[2 * i]}; break;
                case C4:
                case C8:
                case C14X2: {
                    const uint32_t index = t.format == C4 ? nib : t.format == C8 ? blk[i] : be16(&blk[2 * i]) & 0x3FFFu;
                    if (index >= pal.size()) {
                        fail(error, "palette index past the palette");
                        return false;
                    }
                    px = pal[index];
                    break;
                }
                }
                uint8_t* o = &rgba[(size_t(y) * w + x) * 4];
                o[0] = px.r;
                o[1] = px.g;
                o[2] = px.b;
                o[3] = px.a;
            }
        }
    return true;
}

} // namespace gx_texture
