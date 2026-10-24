// GameCube texture images (the BTI header and the image and palette bytes it
// points to) decoded to RGBA8, and the renderer's replacement key for them.
// A port of scripts/windows/link_colors.py (Tex, decode_rgba, key) and the
// decoders of scripts/windows/swift_sail.py; used for Link's textures in a
// model's Link.arc (model_colors.h).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gx_texture {

enum Format : uint8_t {
    I4 = 0, I8 = 1, IA4 = 2, IA8 = 3, RGB565 = 4, RGB5A3 = 5, RGBA8 = 6,
    C4 = 8, C8 = 9, C14X2 = 10, CMPR = 14,
};

struct Texture {
    uint32_t width = 0, height = 0;
    uint8_t format = 0;
    uint8_t palette_format = 0;
    std::vector<uint8_t> image;   // the base level, tiles padded as the GPU reads them
    std::vector<uint8_t> palette; // big endian 16 bit entries (palette formats only)
};

// The 32 byte BTI header at blob[at] and what it points to (offsets are from the header).
bool Parse(const uint8_t* blob, size_t size, size_t at, Texture& out, std::string* error);

// The renderer's replacement key (the stem of a Dolphin-format file name):
// tex1_<W>x<H>_<xxh64 of the base level>[_<xxh64 of the palette entries the image uses>]_<format>.
// See find_replacement_for_guest in aurora's texture_replacement.cpp. Empty (and *error) when the
// palette is shorter than the indices the image uses.
std::string Key(const Texture& t, std::string* error);

// RGBA8, rows top to bottom (width * height * 4 bytes).
bool DecodeRgba(const Texture& t, std::vector<uint8_t>& rgba, std::string* error);

} // namespace gx_texture
