// Widescreen, done in the game the way Dolphin users get it: the community 16:9 Gecko code (and Boat
// of Daphnes' 16:10 derivation of it). It widens the camera's aspect - the projection and the view
// culling both, so nothing at the sides of the wide picture is culled as off screen - and moves the
// HUD to the edges through the game's own 2D bounds and layout values. The picture it renders is
// anamorphic: Aurora stretches it over the window (src/runtime/frontend.cpp) instead of widening.
//
// The code's parts, as applied here:
//   writes into main.dol's code   -> interpreter patches (the patched functions run interpreted)
//   code written to free memory   -> written there once, and run by the interpreter (its caves)
//   writes to data                -> each frame, as a Gecko handler does (the HUD tuning structures
//                                    are set by constructors and menus), the 4:3 values kept to
//                                    restore when widescreen goes off
// Which code: from the window's aspect (g_wideAspect): 16:9 and wider, 16:10, or none below.
#include "runtime/interp.h"
#include "runtime/runtime.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace rt {

std::atomic<float> g_wideAspect{4.f / 3.f};

namespace {
#include "runtime/twwwide.inc"

constexpr u32 kCodeBegin = 0x80003100, kCodeEnd = 0x80338620;  // main.dol .init and .text

struct Write {
    u32 addr, value, size;  // size 1, 2 or 4
};
struct Code {
    std::vector<std::pair<u32, u32>> patches;  // instruction words
    std::vector<std::pair<u32, std::vector<u8>>> caves;
    std::vector<Write> data;
};

Code parse(const char* text) {
    std::vector<u32> w;
    for (const char* p = text; *p;) {
        char* end;
        const u32 v = u32(std::strtoul(p, &end, 16));
        if (end == p) break;
        w.push_back(v);
        p = end;
        while (*p == ' ') p++;
    }
    Code code;
    for (size_t i = 0; i + 1 < w.size();) {
        const u32 type = w[i] >> 24, addr = 0x80000000u | (w[i] & 0x00FFFFFF), v = w[i + 1];
        i += 2;
        if (type == 0x04) {
            if (addr >= kCodeBegin && addr < kCodeEnd) code.patches.push_back({addr, v});
            else code.data.push_back({addr, v, 4});
        } else if (type == 0x02) {
            for (u32 n = 0; n <= v >> 16; n++) code.data.push_back({addr + 2 * n, v & 0xFFFF, 2});
        } else if (type == 0x06) {
            std::vector<u8> bytes;
            for (u32 n = 0; n < v; n++) bytes.push_back(u8(w[i + n / 4] >> (24 - 8 * (n % 4))));
            i += (v + 7) / 8 * 2;
            if (addr >= kCodeBegin && addr < kCodeEnd) code.caves.push_back({addr, std::move(bytes)});
            else
                for (u32 n = 0; n < v; n++) code.data.push_back({addr + n, bytes[n], 1});
        } else {
            log("[wide] unknown Gecko code type %02X\n", type);
            break;
        }
    }
    return code;
}

u32 readSized(u32 a, u32 size) { return size == 4 ? rd32(a) : size == 2 ? rd16(a) : rd8(a); }
void writeSized(u32 a, u32 v, u32 size) {
    if (size == 4) wr32(a, v);
    else if (size == 2) wr16(a, v);
    else wr8(a, v);
}

const Code& codeFor(int mode) {
    static const Code s_169 = parse(kWide16x9), s_1610 = parse(kWide16x10);
    return mode == 2 ? s_169 : s_1610;
}
int s_mode = 0;  // 0 none, 1 16:10, 2 16:9
std::vector<Write> s_original;  // the 4:3 values the data writes replaced

void apply(int mode) {
    if (s_mode) {  // the current code off
        const Code& old = codeFor(s_mode);
        for (const auto& patch : old.patches) interpUnpatch(patch.first);
    }
    if (!mode) {
        for (const Write& o : s_original) writeSized(o.addr, o.value, o.size);
    } else {
        const Code& code = codeFor(mode);
        if (s_original.empty())
            for (const Write& d : code.data) s_original.push_back({d.addr, readSized(d.addr, d.size), d.size});
        for (const auto& [a, bytes] : code.caves) {
            for (size_t n = 0; n < bytes.size(); n++) wr8(a + u32(n), bytes[n]);
            interpStubRange(a, a + u32(bytes.size()));
        }
        for (const auto& [a, word] : code.patches) {
            interpPatch(a, word);
            interpOverrideAt(a);
        }
    }
    log("[wide] %s\n", mode == 2 ? "16:9 widescreen code on" : mode == 1 ? "16:10 widescreen code on" : "widescreen code off (4:3)");
    s_mode = mode;
}
}  // namespace

void twwWideFrame() {
    const float aspect = g_wideAspect.load();
    const int mode = aspect >= 1.7f ? 2 : aspect >= 1.5f ? 1 : 0;
    if (mode != s_mode) apply(mode);
    if (s_mode)
        for (const Write& d : codeFor(s_mode).data) writeSized(d.addr, d.value, d.size);
}

}  // namespace rt
