// A small PNG reader: see png_decode.h.
#include "png_decode.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace png_decode {
namespace {

// ---------------------------------------------------------------------------
// Inflate (RFC 1951): canonical Huffman codes read one bit at a time. The
// pictures are small, so this is not tuned.
// ---------------------------------------------------------------------------

struct Huffman {
    uint16_t count[16] = {};
    uint16_t symbol[320] = {};
};

// False when the code lengths over-subscribe the code (an incomplete code is allowed).
bool build(Huffman& h, const uint8_t* lengths, int n) {
    std::memset(h.count, 0, sizeof h.count);
    for (int i = 0; i < n; ++i)
        ++h.count[lengths[i]];
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left = (left << 1) - h.count[len];
        if (left < 0)
            return false;
    }
    uint16_t offs[16] = {};
    for (int len = 1; len < 15; ++len)
        offs[len + 1] = uint16_t(offs[len] + h.count[len]);
    for (int i = 0; i < n; ++i)
        if (lengths[i] != 0)
            h.symbol[offs[lengths[i]]++] = uint16_t(i);
    return true;
}

// The fixed codes of block type 1.
struct FixedCodes {
    Huffman lit, dist;
    FixedCodes() {
        uint8_t l[288];
        for (int i = 0; i < 288; ++i)
            l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        build(lit, l, 288);
        uint8_t d[30];
        std::memset(d, 5, sizeof d);
        build(dist, d, 30);
    }
};

struct Inflater {
    const uint8_t* in;
    size_t size, pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool bad = false;
    std::vector<uint8_t>& out;
    size_t limit;

    Inflater(const uint8_t* d, size_t n, std::vector<uint8_t>& o, size_t max) : in(d), size(n), out(o), limit(max) {}

    uint32_t bits(int need) {
        uint32_t v = buf;
        while (cnt < need) {
            if (pos >= size) {
                bad = true;
                return 0;
            }
            v |= uint32_t(in[pos++]) << cnt;
            cnt += 8;
        }
        buf = v >> need;
        cnt -= need;
        return v & ((1u << need) - 1);
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= int(bits(1));
            if (bad)
                return -1;
            const int count = h.count[len];
            if (code - count < first)
                return h.symbol[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }

    bool codes(const Huffman& lit, const Huffman& dist) {
        static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                              35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                              3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                               193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                               4097, 6145, 8193, 12289, 16385, 24577};
        static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            const int sym = decode(lit);
            if (sym < 0)
                return false;
            if (sym < 256) {
                if (out.size() >= limit)
                    return false;
                out.push_back(uint8_t(sym));
            } else if (sym == 256) {
                return true;
            } else {
                const int li = sym - 257;
                if (li >= 29)
                    return false;
                const size_t len = len_base[li] + bits(len_extra[li]);
                const int ds = decode(dist);
                if (ds < 0 || ds >= 30)
                    return false;
                const size_t d = dist_base[ds] + bits(dist_extra[ds]);
                if (bad || d > out.size() || out.size() + len > limit)
                    return false;
                for (size_t i = 0; i < len; ++i)
                    out.push_back(out[out.size() - d]);
            }
        }
    }

    bool run() {
        for (bool last = false; !last;) {
            last = bits(1) != 0;
            const uint32_t type = bits(2);
            if (bad)
                return false;
            if (type == 0) {
                buf = 0;
                cnt = 0; // to a byte boundary
                if (size - std::min(size, pos) < 4)
                    return false;
                const size_t len = in[pos] | in[pos + 1] << 8, nlen = in[pos + 2] | in[pos + 3] << 8;
                pos += 4;
                if ((len ^ 0xFFFF) != nlen || size - pos < len || out.size() + len > limit)
                    return false;
                out.insert(out.end(), in + pos, in + pos + len);
                pos += len;
            } else if (type == 1) {
                static const FixedCodes fixed;
                if (!codes(fixed.lit, fixed.dist))
                    return false;
            } else if (type == 2) {
                const int nlen = int(bits(5)) + 257, ndist = int(bits(5)) + 1, ncode = int(bits(4)) + 4;
                if (bad || nlen > 286 || ndist > 30)
                    return false;
                static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                uint8_t lengths[320] = {};
                for (int i = 0; i < ncode; ++i)
                    lengths[order[i]] = uint8_t(bits(3));
                Huffman lencode;
                if (bad || !build(lencode, lengths, 19))
                    return false;
                std::memset(lengths, 0, sizeof lengths);
                for (int i = 0; i < nlen + ndist;) {
                    int sym = decode(lencode);
                    if (sym < 0)
                        return false;
                    if (sym < 16) {
                        lengths[i++] = uint8_t(sym);
                        continue;
                    }
                    uint8_t value = 0;
                    int repeat;
                    if (sym == 16) {
                        if (i == 0)
                            return false;
                        value = lengths[i - 1];
                        repeat = 3 + int(bits(2));
                    } else if (sym == 17) {
                        repeat = 3 + int(bits(3));
                    } else {
                        repeat = 11 + int(bits(7));
                    }
                    if (bad || i + repeat > nlen + ndist)
                        return false;
                    while (repeat--)
                        lengths[i++] = value;
                }
                Huffman lit, dist;
                if (!build(lit, lengths, nlen) || !build(dist, lengths + nlen, ndist))
                    return false;
                if (!codes(lit, dist))
                    return false;
            } else {
                return false;
            }
        }
        return !bad;
    }
};

// ---------------------------------------------------------------------------
// PNG.
// ---------------------------------------------------------------------------

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

void fail(std::string* error, const char* msg) {
    if (error != nullptr)
        *error = msg;
}

uint32_t sample(const uint8_t* row, size_t index, int depth) {
    if (depth == 8)
        return row[index];
    if (depth == 16)
        return uint32_t(row[2 * index]) << 8 | row[2 * index + 1];
    const size_t bit = index * size_t(depth);
    return (row[bit >> 3] >> (8 - depth - int(bit & 7))) & ((1u << depth) - 1);
}

uint8_t scale8(uint32_t v, int depth) { // a sample of any depth as 0-255
    if (depth == 8)
        return uint8_t(v);
    if (depth == 16)
        return uint8_t(v >> 8);
    return uint8_t(v * 255 / ((1u << depth) - 1));
}

int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

} // namespace

bool Decode(const uint8_t* data, size_t size, Image& out, std::string* error) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 8 || std::memcmp(data, sig, 8) != 0) {
        fail(error, "not a PNG");
        return false;
    }
    uint32_t width = 0, height = 0;
    int depth = 0, type = -1, interlace = 0;
    std::vector<uint8_t> palette, trns, idat;
    for (size_t at = 8; at + 12 <= size;) {
        const uint32_t len = be32(data + at);
        const char* name = reinterpret_cast<const char*>(data + at + 4);
        if (size_t(len) > size - at - 12) {
            fail(error, "truncated PNG chunk");
            return false;
        }
        const uint8_t* body = data + at + 8;
        if (std::memcmp(name, "IHDR", 4) == 0 && len >= 13) {
            width = be32(body);
            height = be32(body + 4);
            depth = body[8];
            type = body[9];
            interlace = body[12];
        } else if (std::memcmp(name, "PLTE", 4) == 0) {
            palette.assign(body, body + len);
        } else if (std::memcmp(name, "tRNS", 4) == 0) {
            trns.assign(body, body + len);
        } else if (std::memcmp(name, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(name, "IEND", 4) == 0) {
            break;
        }
        at += size_t(len) + 12;
    }
    int channels = 0;
    switch (type) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: fail(error, "bad PNG header"); return false;
    }
    const bool depth_ok = type == 0   ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)
                          : type == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8)
                                      : (depth == 8 || depth == 16);
    if (!depth_ok || width == 0 || height == 0 || width > 8192 || height > 8192 || interlace > 1) {
        fail(error, "unsupported PNG");
        return false;
    }
    if (type == 3 && palette.size() < 3) {
        fail(error, "PNG has no palette");
        return false;
    }
    if (idat.size() < 2 || (idat[0] & 15) != 8) {
        fail(error, "bad PNG data");
        return false;
    }
    const int bits_pp = depth * channels;
    const size_t bpp = size_t(std::max(1, bits_pp / 8));
    const size_t max_raw = (size_t(width) * bits_pp / 8 + 2) * height * 2 + 1024;
    std::vector<uint8_t> raw;
    Inflater inf(idat.data() + 2, idat.size() - 2, raw, max_raw);
    if (!inf.run()) {
        fail(error, "bad PNG compressed data");
        return false;
    }

    out.width = width;
    out.height = height;
    out.rgba.assign(size_t(width) * height * 4, 0);

    static const int xs[7] = {0, 4, 0, 2, 0, 1, 0}, ys[7] = {0, 0, 4, 0, 2, 0, 1};
    static const int dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
    const int passes = interlace ? 7 : 1;
    size_t pos = 0;
    for (int pass = 0; pass < passes; ++pass) {
        const uint32_t x0 = interlace ? xs[pass] : 0, y0 = interlace ? ys[pass] : 0;
        const uint32_t sx = interlace ? dx[pass] : 1, sy = interlace ? dy[pass] : 1;
        if (x0 >= width || y0 >= height)
            continue;
        const uint32_t pw = (width - x0 + sx - 1) / sx, ph = (height - y0 + sy - 1) / sy;
        const size_t stride = (size_t(pw) * bits_pp + 7) / 8;
        std::vector<uint8_t> prev(stride, 0), cur(stride);
        for (uint32_t row = 0; row < ph; ++row) {
            if (raw.size() - pos < stride + 1) {
                fail(error, "PNG data too short");
                return false;
            }
            const uint8_t filter = raw[pos++];
            std::memcpy(cur.data(), &raw[pos], stride);
            pos += stride;
            for (size_t i = 0; i < stride; ++i) {
                const int a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
                int v = cur[i];
                switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
                default: fail(error, "bad PNG filter"); return false;
                }
                cur[i] = uint8_t(v);
            }
            for (uint32_t px = 0; px < pw; ++px) {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                if (type == 0) {
                    const uint32_t v = sample(cur.data(), px, depth);
                    r = g = b = scale8(v, depth);
                    if (trns.size() >= 2 && v == (uint32_t(trns[0]) << 8 | trns[1]))
                        a = 0;
                } else if (type == 2) {
                    const uint32_t vr = sample(cur.data(), px * 3, depth), vg = sample(cur.data(), px * 3 + 1, depth),
                                   vb = sample(cur.data(), px * 3 + 2, depth);
                    r = scale8(vr, depth);
                    g = scale8(vg, depth);
                    b = scale8(vb, depth);
                    if (trns.size() >= 6 && vr == (uint32_t(trns[0]) << 8 | trns[1]) &&
                        vg == (uint32_t(trns[2]) << 8 | trns[3]) && vb == (uint32_t(trns[4]) << 8 | trns[5]))
                        a = 0;
                } else if (type == 3) {
                    const uint32_t v = sample(cur.data(), px, depth);
                    if (size_t(v) * 3 + 2 >= palette.size()) {
                        fail(error, "PNG palette index past the palette");
                        return false;
                    }
                    r = palette[v * 3];
                    g = palette[v * 3 + 1];
                    b = palette[v * 3 + 2];
                    if (v < trns.size())
                        a = trns[v];
                } else if (type == 4) {
                    r = g = b = scale8(sample(cur.data(), px * 2, depth), depth);
                    a = scale8(sample(cur.data(), px * 2 + 1, depth), depth);
                } else {
                    r = scale8(sample(cur.data(), px * 4, depth), depth);
                    g = scale8(sample(cur.data(), px * 4 + 1, depth), depth);
                    b = scale8(sample(cur.data(), px * 4 + 2, depth), depth);
                    a = scale8(sample(cur.data(), px * 4 + 3, depth), depth);
                }
                uint8_t* o = &out.rgba[((size_t(y0) + size_t(row) * sy) * width + x0 + size_t(px) * sx) * 4];
                o[0] = r;
                o[1] = g;
                o[2] = b;
                o[3] = a;
            }
            prev.swap(cur);
        }
    }
    return true;
}

} // namespace png_decode
