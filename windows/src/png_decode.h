// A small PNG reader for a model's color masks (the pictures are red and white
// and a few KB; the app needs no image library for them). All PNG color types
// and bit depths, interlaced or not, to RGBA8 the way Pillow's convert("RGBA")
// reads them. The zlib stream is inflated by the code in png_decode.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace png_decode {

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba; // width * height * 4, top row first
};

// False (and *error) for anything that is not a PNG this reads; sizes above 8192 on a side are refused.
bool Decode(const uint8_t* data, size_t size, Image& out, std::string* error);

} // namespace png_decode
