// Two game modules in one session, switched while the game runs: Boat of Daphnes' in-place randomizer switch on this
// runtime. The game's module and the randomizer's (wwrando's patched program, recompiled) are both loaded; one is in
// force (g_game: dispatch, hooks, patches, REL registration). The two share guest memory and the REL load bases.
//
// Code of the module no longer in force keeps running where a guest thread's stack holds its frames (the game's main loop
// calls straight down into its own functions). Most of it is the same in both programs; for the functions that differ
// (tools/rando/module_diff.py) and the ones the interpreter patches, its override flag is set to 2 and the call goes to the
// module in force's function at the same address (or the interpreter, where that module has none). Each module enters
// the runtime through its own runOverride entry point, so a slot number is always read in the module it belongs to.
//
// A switch writes the other program's main.dol bytes where the two differ (keeping what was there, for the way back) and
// its added section, re-registers main.dol's and the linked RELs' functions from the new module, moves the overrides, and
// points the disc's changed files at the other image (hle_dvd.cpp). The game's own state (the module controls, resident
// archives) is the caller's: bod_rando.cpp, from the file select.
#include "runtime/modswitch.h"

#include "runtime/interp.h"
#include "runtime/runtime.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rt {

extern std::atomic<bool> g_relLinked[kMaxModules];  // dispatch.cpp
GuestFn dispatchLookup(u32 addr);
void registerDol();
void reregisterLinkedModules();      // dispatch.cpp
void dvdUnionMode(bool rando);       // hle_dvd.cpp
bool dvdUnionActive();

const GameModule* g_mods[2] = {nullptr, nullptr};
int g_modCurrent = 0;
namespace {
volatile u32 s_irqDummy = 0;
u32 s_relBaseDummy[kMaxModules][32];
}  // namespace
volatile u32* g_irqPendingPtr2 = &s_irqDummy;
u32 (*g_relBasePtr2)[32] = s_relBaseDummy;

namespace {
bool s_ready = false;
std::unordered_set<u32> s_diff[2];                  // per module: the slots of the functions that differ
std::unordered_map<u32, GuestFn> s_dolFn[2];        // per module: main.dol entry -> function
struct Section {
    u32 addr;
    std::vector<u8> bytes;
};
std::vector<Section> s_image[2];                    // each program's initialised main.dol sections
std::vector<std::pair<u32, u32>> s_differ;          // [addr, end) runs where the two programs' bytes differ
std::vector<u8> s_saved[2];                         // what memory held there when that program was last in force

struct Fn {
    u32 entry, body, end;
};
bool slotFn(const GameModule* g, u32 slot, Fn& out) {
    if (slot < g->dolFuncCount) {
        const FuncEntry& e = g->dolFuncs[slot];
        out = {e.addr, e.body, e.end};
        return true;
    }
    for (u32 i = 0; i < g->relModuleCount; i++) {
        const RelModuleCode& m = g->relModules[i];
        if (slot < m.slotBase || slot >= m.slotBase + m.count) continue;
        if (!g_relLinked[m.id]) return false;
        const FuncEntry& e = m.funcs[slot - m.slotBase];
        auto real = [&](u32 a) { return g_relBase[m.id][a >> 24] + (a & 0xFFFFFF); };
        out = {real(e.addr), real(e.body), real(e.end)};
        return true;
    }
    return false;
}
template <typename F>
void forEachSlot(const GameModule* g, u32 addr, F&& fn) {
    for (u32 i = 0; i < g->dolFuncCount; i++)
        if (addr >= g->dolFuncs[i].body && addr < g->dolFuncs[i].end) fn(i);
    for (u32 i = 0; i < g->relModuleCount; i++) {
        const RelModuleCode& m = g->relModules[i];
        if (!g_relLinked[m.id]) continue;
        for (u32 k = 0; k < m.count; k++) {
            const FuncEntry& e = m.funcs[k];
            const u32 base = g_relBase[m.id][e.body >> 24];
            if (base && addr >= base + (e.body & 0xFFFFFF) && addr < base + (e.end & 0xFFFFFF)) fn(m.slotBase + k);
        }
    }
}

// The module in force's function entered at `entry` (null if it has none there).
GuestFn currentFnAt(u32 entry) {
    const int cur = g_modCurrent;
    if (const auto it = s_dolFn[cur].find(entry); it != s_dolFn[cur].end()) return it->second;
    const GameModule* g = g_mods[cur];
    for (u32 i = 0; i < g->relModuleCount; i++) {
        const RelModuleCode& m = g->relModules[i];
        if (!g_relLinked[m.id]) continue;
        for (u32 k = 0; k < m.count; k++) {
            const u32 a = m.funcs[k].addr, base = g_relBase[m.id][a >> 24];
            if (base && base + (a & 0xFFFFFF) == entry) return m.funcs[k].fn;
        }
    }
    return nullptr;
}

std::vector<Section> imageOf(const std::vector<u8>& d) {
    std::vector<Section> out;
    if (d.size() < 0x100) return out;
    auto be = [&](u32 o) { return u32(d[o]) << 24 | u32(d[o + 1]) << 16 | u32(d[o + 2]) << 8 | d[o + 3]; };
    for (u32 i = 0; i < 18; i++) {
        const u32 off = be(i * 4), addr = be(0x48 + i * 4), size = be(0x90 + i * 4);
        if (!size || off + u64(size) > d.size()) continue;
        out.push_back({addr, std::vector<u8>(d.begin() + off, d.begin() + off + size)});
    }
    return out;
}
const u8* byteAt(const std::vector<Section>& img, u32 a) {
    for (const Section& s : img)
        if (a >= s.addr && a - s.addr < s.bytes.size()) return &s.bytes[a - s.addr];
    return nullptr;
}

std::unordered_set<u32> slotsOf(const GameModule* g, const std::vector<std::pair<u32, u32>>& funcs) {
    std::unordered_map<u32, u32> dol;
    for (u32 i = 0; i < g->dolFuncCount; i++) dol.emplace(g->dolFuncs[i].addr, i);
    std::unordered_map<u32, const RelModuleCode*> rel;
    for (u32 i = 0; i < g->relModuleCount; i++) rel[g->relModules[i].id] = &g->relModules[i];
    std::unordered_set<u32> out;
    for (const auto& [module, addr] : funcs) {
        if (module == 0) {
            if (const auto it = dol.find(addr); it != dol.end()) out.insert(it->second);
            continue;
        }
        const auto it = rel.find(module);
        if (it == rel.end()) continue;
        for (u32 k = 0; k < it->second->count; k++)
            if (it->second->funcs[k].addr == addr) out.insert(it->second->slotBase + k);
    }
    return out;
}
}  // namespace

bool modswitchAvailable() { return s_ready; }

bool modswitchInit(const std::string& diffPath, const std::vector<u8>& dol0, const std::vector<u8>& dol1) {
    if (!g_mods[0] || !g_mods[1]) return false;
    std::ifstream f(diffPath, std::ios::binary);
    char magic[4] = {};
    u32 count = 0;
    if (!f.read(magic, 4) || std::memcmp(magic, "MDIF", 4) || !f.read(reinterpret_cast<char*>(&count), 4)) {
        log("[modswitch] no function table at %s: the randomizer cannot be switched to while the game runs\n", diffPath.c_str());
        return false;
    }
    std::vector<std::pair<u32, u32>> funcs(count);
    for (auto& [m, a] : funcs) f.read(reinterpret_cast<char*>(&m), 4), f.read(reinterpret_cast<char*>(&a), 4);
    if (!f) return log("[modswitch] %s is cut short\n", diffPath.c_str()), false;
    for (int m = 0; m < 2; m++) {
        s_diff[m] = slotsOf(g_mods[m], funcs);
        for (u32 i = 0; i < g_mods[m]->dolFuncCount; i++) s_dolFn[m].emplace(g_mods[m]->dolFuncs[i].addr, g_mods[m]->dolFuncs[i].fn);
    }
    s_image[0] = imageOf(dol0), s_image[1] = imageOf(dol1);
    // Where the two programs' bytes differ: within the sections both have, and every byte of a section only the
    // randomizer's has (its added code and data, in room the game's boot left free: main.cpp).
    for (const Section& s : s_image[1]) {
        u32 runStart = 0;
        bool in = false;
        for (u32 i = 0; i <= s.bytes.size(); i++) {
            const u32 a = s.addr + i;
            bool differs = false;
            if (i < s.bytes.size()) {
                const u8* v = byteAt(s_image[0], a);
                differs = !v || *v != s.bytes[i];
            }
            if (differs && !in) runStart = a, in = true;
            if (!differs && in) s_differ.push_back({runStart, a}), in = false;
        }
    }
    u32 bytes = 0;
    for (const auto& [a, e] : s_differ) bytes += e - a;
    // The randomizer's module is not in force: its differing functions go to the game's.
    for (u32 slot : s_diff[1]) g_mods[1]->fnOverride[slot] = 2;
    g_irqPendingPtr2 = g_mods[1]->irqPending;
    g_relBasePtr2 = g_mods[1]->relBase;
    std::memcpy(g_mods[1]->relBase, g_mods[0]->relBase, sizeof(u32) * kMaxModules * 32);
    s_ready = true;
    log("[modswitch] two programs: %u functions differ (%zu and %zu slots), %u main.dol bytes in %zu runs\n", count,
        s_diff[0].size(), s_diff[1].size(), bytes, s_differ.size());
    return true;
}

void modswitchOverride(int module, Ctx& c, u32 slot) {
    if (module == g_modCurrent || !s_ready) return runOverrideCurrent(c, slot);
    // Code of the module not in force: the function as the module in force has it.
    Fn f;
    if (!slotFn(g_mods[module], slot, f)) fatal("override slot %u of module %d: no such function loaded", slot, module);
    if (GuestFn fn = currentFnAt(f.entry)) return fn(c);
    interpRun(c, f.entry, f.body, f.end);  // (no function starts there in the program in force: its code, interpreted)
}

void modswitchMark(u32 addr) {
    if (!s_ready) return;
    const GameModule* other = g_mods[1 - g_modCurrent];
    forEachSlot(other, addr, [&](u32 slot) { other->fnOverride[slot] = 2; });
}

void modswitchUnmark(u32 addr) {
    if (!s_ready) return;
    const int o = 1 - g_modCurrent;
    const GameModule* other = g_mods[o];
    forEachSlot(other, addr, [&](u32 slot) {
        Fn f;
        if (!s_diff[o].count(slot) && slotFn(other, slot, f) && !interpPatchedIn(f.body, f.end)) other->fnOverride[slot] = 0;
    });
}

bool modswitchTo(int target) {
    if (!s_ready || target == g_modCurrent || target < 0 || target > 1) return false;
    const int old = g_modCurrent;
    // 1. main.dol: what the old program had in the differing bytes is kept; the new one's are written (its own from the
    //    last time it was in force, or its file's).
    std::vector<u8>& keep = s_saved[old];
    keep.clear();
    for (const auto& [a, e] : s_differ)
        for (u32 x = a; x < e; x++) keep.push_back(u8(rd8(x)));
    size_t k = 0;
    for (const auto& [a, e] : s_differ)
        for (u32 x = a; x < e; x++, k++) {
            if (!s_saved[target].empty()) {
                wr8(x, s_saved[target][k]);
            } else if (const u8* v = byteAt(s_image[target], x)) {
                wr8(x, *v);
            }
        }
    // 2. The module in force.
    const GameModule* from = g_mods[old];
    const GameModule* to = g_mods[target];
    for (u32 slot = 0; slot < from->fnSlotCount; slot++)
        if (from->fnOverride[slot]) from->fnOverride[slot] = 2;
    for (u32 slot : s_diff[old]) from->fnOverride[slot] = 2;
    std::memset(to->fnOverride, 0, to->fnSlotCount);
    g_modCurrent = target;
    g_game = to;
    g_irqPendingPtr = to->irqPending, g_irqPendingPtr2 = from->irqPending;
    g_relBasePtr = to->relBase, g_relBasePtr2 = from->relBase;
    g_traceOnPtr = to->traceOn;
    // 3. Its functions registered, its patched functions interpreted.
    registerDol();
    reregisterLinkedModules();
    for (u32 a : interpPatchedAddresses()) interpOverrideAt(a);
    // 4. The disc: the files the two programs' discs have differently.
    dvdUnionMode(target == 1);
    log("[modswitch] the %s program is in force\n", target ? "randomizer's" : "game's");
    return true;
}

}  // namespace rt
