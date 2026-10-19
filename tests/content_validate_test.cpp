// Standalone test of windows/src/content_validate.cpp, player_parts.cpp and rarc.cpp: what the game accepts from
// other players' custom content, and what it refuses. Built and run by tests/content_validate_test.py.
//
//   content_validate_test                run the synthetic cases (malformed, truncated, oversized, traversal, bombs)
//   content_validate_test arc FILE...    each FILE (a Link.arc, Yaz0 or plain) must pass ExpandArchive
//   content_validate_test aaf FILE...    each must pass ValidateAaf
//   content_validate_test wave FILE...   each must pass ValidateWave
//   content_validate_test voice AAF AW...   each bank must parse and decode (voices are counted); corrupted copies must not crash
//   content_validate_test parts VANILLA MOD...   print Link/Equipment detection of each MOD Link.arc against VANILLA
//
// Exit status 0 when every check passes.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "../windows/src/content_validate.h"
#include "../windows/src/player_parts.h"
#include "../windows/src/rarc.h"
#include "../windows/src/voice_bank.h"

using Bytes = std::vector<uint8_t>;
namespace cv = content_validate;

static int failures, checks;
#define CHECK(cond, ...)                                     \
    do {                                                     \
        ++checks;                                            \
        if (!(cond)) {                                       \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            ++failures;                                      \
        }                                                    \
    } while (0)

static void put32(Bytes& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v >> 24);
    b[at + 1] = uint8_t(v >> 16);
    b[at + 2] = uint8_t(v >> 8);
    b[at + 3] = uint8_t(v);
}
static void put16(Bytes& b, size_t at, uint16_t v) {
    b[at] = uint8_t(v >> 8);
    b[at + 1] = uint8_t(v);
}

// A J3D file with `chunks` chunks of 0x20 bytes.
static Bytes j3d(const char* magic8, int chunks = 2) {
    Bytes b(0x20 + size_t(chunks) * 0x20, 0);
    std::memcpy(b.data(), magic8, 8);
    put32(b, 8, uint32_t(b.size()));
    put32(b, 12, uint32_t(chunks));
    for (int i = 0; i < chunks; ++i) {
        std::memcpy(&b[0x20 + size_t(i) * 0x20], "VTX1", 4);
        put32(b, 0x20 + size_t(i) * 0x20 + 4, 0x20);
    }
    return b;
}

// A BTI: format C8 (9), 8x4 pixels, one mip, 1 byte per pixel, palette of 16 colors after the image.
static Bytes bti(uint8_t byte0 = 9) {
    Bytes b(0x20 + 32 + 32, 0);
    b[0] = byte0;
    put16(b, 2, 8);
    put16(b, 4, 4);
    b[8] = 1;
    put16(b, 0x0A, 16);
    put32(b, 0x0C, 0x20 + 32);
    b[0x18] = 1;
    put32(b, 0x1C, 0x20);
    return b;
}

struct Member {
    std::string name;
    Bytes data;
    uint8_t type = 0x11;
};

// A RARC with one folder node and the members as its files.
static Bytes make_rarc(const std::vector<Member>& members) {
    std::string strings;
    std::vector<uint32_t> name_at;
    strings += "root";
    strings.push_back(0);
    for (const auto& m : members) {
        name_at.push_back(uint32_t(strings.size()));
        strings += m.name;
        strings.push_back(0);
    }
    const size_t node_off = 0x20, entry_off = node_off + 0x10;
    size_t strings_off = entry_off + members.size() * 0x14;
    strings_off = (strings_off + 31) & ~size_t(31);
    size_t data_off = (strings_off + strings.size() + 31) & ~size_t(31);
    Bytes data;
    std::vector<uint32_t> offsets;
    for (const auto& m : members) {
        offsets.push_back(uint32_t(data.size()));
        data.insert(data.end(), m.data.begin(), m.data.end());
        data.resize((data.size() + 31) & ~size_t(31));
    }
    Bytes b(0x20 + data_off + data.size(), 0);
    std::memcpy(b.data(), "RARC", 4);
    put32(b, 4, uint32_t(b.size()));
    put32(b, 8, 0x20);
    put32(b, 0x0C, uint32_t(data_off));
    put32(b, 0x10, uint32_t(data.size()));
    put32(b, 0x14, uint32_t(data.size()));
    put32(b, 0x20, 1);
    put32(b, 0x24, uint32_t(node_off));
    put32(b, 0x28, uint32_t(members.size()));
    put32(b, 0x2C, uint32_t(entry_off));
    put32(b, 0x30, uint32_t(strings.size()));
    put32(b, 0x34, uint32_t(strings_off));
    const size_t node = 0x20 + node_off;
    std::memcpy(&b[node], "ROOT", 4);
    put32(b, node + 4, 0);
    put16(b, node + 0x0A, uint16_t(members.size()));
    put32(b, node + 0x0C, 0);
    for (size_t i = 0; i < members.size(); ++i) {
        const size_t e = 0x20 + entry_off + i * 0x14;
        put16(b, e, uint16_t(i));
        b[e + 4] = members[i].type;
        put16(b, e + 6, uint16_t(name_at[i]));
        put32(b, e + 8, offsets[i]);
        put32(b, e + 12, uint32_t(members[i].data.size()));
    }
    std::memcpy(&b[0x20 + strings_off], strings.data(), strings.size());
    std::memcpy(&b[0x20 + data_off], data.data(), data.size());
    return b;
}

static Bytes good_link() {
    return make_rarc({{"cl.bdl", j3d("J3D2bdl4")}, {"swa.bdl", j3d("J3D2bdl3", 3)}, {"sha.bdl", j3d("J3D2bdl4")},
                 {"linktexbci4.bti", bti()}, {"bomb.bck", j3d("J3D1bck1", 1)}});
}

static bool expand_ok(Bytes b, std::string* why = nullptr) {
    std::string e;
    const bool ok = cv::ExpandArchive(b, &e);
    if (why)
        *why = e;
    return ok;
}

// A Yaz0 header declaring `size` output bytes with a few literal bytes.
static Bytes yaz0(uint32_t size, size_t payload) {
    Bytes b(16 + payload, 0);
    std::memcpy(b.data(), "Yaz0", 4);
    put32(b, 4, size);
    for (size_t i = 0; i < payload; ++i)
        b[16 + i] = i % 9 == 0 ? 0xFF : uint8_t(i);
    return b;
}

static void test_archive() {
    const Bytes good = good_link();
    std::string why;
    CHECK(expand_ok(good, &why), "a well-formed Link.arc is accepted (%s)", why.c_str());
    Bytes body;
    CHECK(cv::ExtractBody(good, body, nullptr) && !body.empty(), "the body is found");
    std::map<std::string, Bytes> eq;
    CHECK(cv::ExtractEquipment(good, eq, nullptr) && eq.count("swa.bdl") && eq.count("sha.bdl") && !eq.count("cl.bdl"), "equipment models found");

    // Every truncation is refused without crashing.
    int accepted = 0;
    for (size_t n = 0; n < good.size(); n += 3) {
        Bytes cut(good.begin(), good.begin() + std::ptrdiff_t(n));
        accepted += expand_ok(cut);
    }
    CHECK(accepted == 0, "no truncated archive is accepted (%d were)", accepted);

    // Random corruption never crashes (the result is either way fine, but must not run wild).
    std::mt19937 rng(12345);
    int survived = 0;
    for (int i = 0; i < 4000; ++i) {
        Bytes b = good;
        const int flips = 1 + int(rng() % 6);
        for (int k = 0; k < flips; ++k)
            b[rng() % b.size()] = uint8_t(rng());
        survived += expand_ok(b) ? 1 : 0;
        Bytes c = good;
        const size_t start = rng() % 0x60;
        for (size_t k = start; k < start + 4 && k < c.size(); ++k)
            c[k] = 0xFF; // huge counts, offsets and sizes in the header tables
        expand_ok(c);
    }
    std::printf("fuzz: %d of 4000 corrupted archives still validate (flips outside the checked fields)\n", survived);

    // Oversized: more than the file limit, and a member over its limit.
    {
        Bytes big(cv::kMaxFile + 1, 0);
        std::memcpy(big.data(), "RARC", 4);
        CHECK(!expand_ok(big), "an archive over the file limit is refused");
        Bytes huge = good;
        put32(huge, 0x2C, 0x7FFFFFF0u); // entry table far outside the file
        CHECK(!expand_ok(huge), "an entry table outside the file is refused");
    }
    // Path traversal and odd names in member names.
    for (const char* name : {"../evil.bdl", "..\\evil.bdl", "a/b.bdl", "C:evil.bdl", "evil.exe", "evil.dll", "run.bat", "x.py", "a b.bdl"}) {
        Bytes b = make_rarc({{"cl.bdl", j3d("J3D2bdl4")}, {name, j3d("J3D2bdl4")}});
        CHECK(!expand_ok(b), "member name '%s' is refused", name);
    }
    CHECK(!expand_ok(make_rarc({{"cl.bdl", j3d("J3D2bdl4")}, {"CL.BDL", j3d("J3D2bdl4")}})), "a duplicate member name is refused");
    // Members with a compressed / ARAM / DVD flag.
    for (const uint8_t type : {uint8_t(0x11 | 0x04), uint8_t(0x11 | 0x20), uint8_t(0x11 | 0x40), uint8_t(0x11 | 0x80)})
        CHECK(!expand_ok(make_rarc({{"cl.bdl", j3d("J3D2bdl4"), type}})), "member flag %02X is refused", type);
    // Members that are not what their extension says.
    CHECK(!expand_ok(make_rarc({{"cl.bdl", Bytes(0x80, 0x41)}})), "a bdl without the J3D magic is refused");
    CHECK(!expand_ok(make_rarc({{"cl.bdl", j3d("J3D1bck1")}})), "a bdl with a bck magic is refused");
    {
        Bytes t = j3d("J3D2bdl4");
        put32(t, 0x24, 0x7FFFFFFFu); // a chunk size past the file
        CHECK(!expand_ok(make_rarc({{"cl.bdl", t}})), "a J3D chunk past the file is refused");
        Bytes t2 = j3d("J3D2bdl4");
        put32(t2, 12, 5); // more chunks than the file holds
        CHECK(!expand_ok(make_rarc({{"cl.bdl", t2}})), "a J3D chunk count past the file is refused");
        Bytes t3 = j3d("J3D2bdl4");
        put32(t3, 0x24, 0); // a zero-size chunk (an endless walk)
        CHECK(!expand_ok(make_rarc({{"cl.bdl", t3}})), "a zero-size J3D chunk is refused");
    }
    CHECK(!expand_ok(make_rarc({{"cl.bdl", j3d("J3D2bdl4")}, {"notes.txt", Bytes(40, 'a')}})), "a text file in the archive is refused");
    CHECK(!expand_ok(make_rarc({{"cl.bdl", j3d("J3D2bdl4")}, {"sound.aw", Bytes(64, 0)}})), "an audio file inside Link.arc is refused");
    for (const uint8_t fmt : {uint8_t(7), uint8_t(11), uint8_t(0x55)})
        CHECK(!expand_ok(make_rarc({{"cl.bdl", j3d("J3D2bdl4")}, {"t.bti", bti(fmt)}})), "a texture of format %u is refused", fmt);
    {
        Bytes t = bti();
        put16(t, 2, 0xFFFF);
        CHECK(!expand_ok(make_rarc({{"t.bti", t}})), "a texture 65535 wide is refused");
        Bytes t2 = bti();
        put32(t2, 0x1C, 0xFFFFFF00u);
        CHECK(!expand_ok(make_rarc({{"t.bti", t2}})), "a texture image offset past the file is refused");
    }
    // Yaz0 bombs: declared sizes beyond the limit, and beyond what the input can make.
    CHECK(!expand_ok(yaz0(0xFFFFFFFFu, 64)), "a Yaz0 declaring 4 GB is refused");
    CHECK(!expand_ok(yaz0(cv::kMaxArchive + 1, 4000)), "a Yaz0 declaring more than the archive limit is refused");
    CHECK(!expand_ok(yaz0(8u * 1024 * 1024, 100)), "a Yaz0 declaring 80000 times its size is refused");
    CHECK(!expand_ok(yaz0(1000, 16)), "a Yaz0 whose data ends early is refused");
    {
        // Yaz0 wrapped around a good archive made of literal bytes round-trips.
        Bytes lit(16, 0);
        std::memcpy(lit.data(), "Yaz0", 4);
        put32(lit, 4, uint32_t(good.size()));
        for (size_t i = 0; i < good.size(); i += 8) {
            lit.push_back(0xFF);
            for (size_t k = 0; k < 8 && i + k < good.size(); ++k)
                lit.push_back(good[i + k]);
        }
        CHECK(expand_ok(lit, &why), "a Yaz0 of a good archive is accepted (%s)", why.c_str());
    }
    {   // the same through rarc::Yaz0Expand directly
        Bytes bomb = yaz0(0xFFFFFFF0u, 100);
        CHECK(!rarc::Yaz0Expand(bomb), "rarc::Yaz0Expand refuses a bomb");
        Bytes small = yaz0(16, 20);
        rarc::Yaz0Expand(small, 8);
        CHECK(small.size() == 36, "a refused Yaz0 is left unchanged");
    }
    // Not archives at all.
    CHECK(!expand_ok(Bytes{}), "empty data is refused");
    CHECK(!expand_ok(Bytes(100, 0)), "zeros are refused");
    CHECK(!expand_ok(Bytes{'M', 'Z', 0x90, 0, 3, 0, 0, 0, 4, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0x40}), "an executable is refused");
    CHECK(!expand_ok(Bytes{'P', 'K', 3, 4, 20, 0, 0, 0, 8, 0}), "a zip is refused");
}

static void test_manifest() {
    const std::string h(64, 'a');
    const auto m = [&](const std::string& files, const std::string& extra = "") {
        return "{\"v\":1,\"name\":\"x\"," + extra + "\"files\":[" + files + "]}";
    };
    const auto f = [&](const std::string& path, const std::string& role, long long size = 100, const std::string& hash = "") {
        return "{\"path\":\"" + path + "\",\"role\":\"" + role + "\",\"sha256\":\"" + (hash.empty() ? h : hash) + "\",\"size\":" + std::to_string(size) + "}";
    };
    cv::Manifest out;
    std::string why;
    CHECK(cv::ParseManifest(m(f("Link.arc", "model")), out, &why) && out.files.size() == 1 && !out.has_parts, "a plain v1 manifest parses (%s)", why.c_str());
    CHECK(cv::ParseManifest(m(f("Link.arc", "model") + "," + f("equipment/Link.arc", "equipment") + "," + f("sound/JaiInit.aaf", "voice") + "," + f("sound/voice_0.aw", "voice"),
                              "\"parts\":{\"link\":true,\"equipment\":\"equipment\",\"voice\":true},"), out, &why) &&
              out.files.size() == 4 && out.has_parts && out.link && out.equipment == "equipment" && out.voice,
          "a manifest with parts parses (%s)", why.c_str());
    CHECK(cv::ParseManifest(m(f("Link.arc", "model") + "," + f("equipment/old.arc", "equipment")), out, &why) && out.files.size() == 1,
          "a path an older client lists is skipped, not fetched");
    for (const char* bad : {"../Link.arc", "/Link.arc", "..", "a/../Link.arc", "a\\\\b", "C:/x", "", ".", "a//b", "a/./b", "Link.arc\\u0000x"})
        CHECK(!cv::ParseManifest(m(f(bad, "model")), out, &why), "path '%s' is refused", bad);
    CHECK(!cv::ParseManifest(m(f("Link.arc", "model", -1)), out, &why), "a negative size is refused");
    CHECK(!cv::ParseManifest(m(f("Link.arc", "model", 21 * 1024 * 1024)), out, &why), "a file over the limit is refused");
    CHECK(!cv::ParseManifest(m(f("Link.arc", "model", 12 * 1024 * 1024) + "," + f("sound/voice_0.aw", "voice", 12 * 1024 * 1024)), out, &why), "a pack over the total limit is refused");
    CHECK(!cv::ParseManifest(m(f("Link.arc", "model", 100, "ABCD")), out, &why), "a short hash is refused");
    CHECK(!cv::ParseManifest(m(f("Link.arc", "model", 100, std::string(64, 'A'))), out, &why), "an upper-case hash is refused");
    CHECK(!cv::ParseManifest(m(f("Link.arc", "model") + "," + f("link.arc", "model")), out, &why), "a path listed twice is refused");
    CHECK(!cv::ParseManifest("{\"v\":2,\"files\":[]}", out, &why), "version 2 is refused");
    CHECK(!cv::ParseManifest("not json", out, &why), "text is refused");
    CHECK(!cv::ParseManifest("[]", out, &why), "an array is refused");
    CHECK(!cv::ParseManifest(m(""), out, &why), "no files is refused");
    CHECK(!cv::ParseManifest(std::string(cv::kMaxManifest + 1, ' '), out, &why), "an oversized manifest is refused");
    {
        std::string many;
        for (int i = 0; i < 70; ++i)
            many += (i ? "," : "") + f("f" + std::to_string(i), "model");
        CHECK(!cv::ParseManifest(m(many), out, &why), "70 files are refused");
    }
    CHECK(!cv::ParseManifest(m(f("Link.arc", "model"), "\"parts\":{\"equipment\":\"../x\"},"), out, &why), "an unknown equipment part is refused");
    CHECK(cv::ParseManifest("{\"v\":1,\"name\":\"<script>\\u0007x\",\"files\":[" + f("Link.arc", "model") + "]}", out, &why) && out.name == "scriptx",
          "a name is cleaned to plain characters ('%s')", out.name.c_str());
    CHECK(cv::SafeRelPath("sound/voice_0.aw") && !cv::SafeRelPath("sound/../x") && !cv::SafeRelPath("sound\\x"), "SafeRelPath");
    CHECK(cv::AllowedPath("Link.arc", "model") && !cv::AllowedPath("Link.arc", "voice") && !cv::AllowedPath("sound/x.exe", "voice") &&
              !cv::AllowedPath("equipment/../Link.arc", "equipment") && !cv::AllowedPath("link.arc", "model"),
          "AllowedPath is an exact list");
}

static void test_audio() {
    // A JaiInit.aaf of two records: type 1 (offset, size, flag) and type 2 with one triple, then the end.
    Bytes aaf(0x80, 0);
    const uint32_t words[] = {1, 0x40, 0x20, 0, 2, 0x40, 0x10, 0, 0, 0};
    for (size_t i = 0; i < sizeof words / 4; ++i)
        put32(aaf, i * 4, words[i]);
    std::string why;
    CHECK(cv::ValidateAaf(aaf, &why), "a good JaiInit.aaf is accepted (%s)", why.c_str());
    for (size_t n = 0; n < aaf.size(); n += 4) {
        Bytes cut(aaf.begin(), aaf.begin() + std::ptrdiff_t(n));
        if (n >= 40)
            continue; // past the terminator the rest is only padding
        CHECK(!cv::ValidateAaf(cut, &why), "a JaiInit.aaf cut to %zu bytes is refused", n);
    }
    Bytes out_of_range = aaf;
    put32(out_of_range, 4, 0x7FFFFFF0u);
    CHECK(!cv::ValidateAaf(out_of_range, &why), "a record pointing outside the file is refused");
    Bytes endless(0x100, 0xFF); // unknown record type, no zero word to end it
    CHECK(!cv::ValidateAaf(endless, &why), "an unterminated record is refused");
    Bytes loop(0x100, 0);
    for (size_t i = 0; i < 0x100; i += 8) { // many unknown records and no end
        put32(loop, i, 99);
        put32(loop, i + 4, 0);
    }
    CHECK(!cv::ValidateAaf(loop, &why), "a file of endless records is refused");
    CHECK(!cv::ValidateAaf(Bytes(cv::kMaxVoiceInit + 4, 0), &why), "an oversized JaiInit.aaf is refused");
    CHECK(cv::ValidateWave(Bytes(64, 7), &why) && !cv::ValidateWave(Bytes(63, 7), &why) && !cv::ValidateWave(Bytes{}, &why) &&
              !cv::ValidateWave(Bytes(cv::kMaxVoiceWave + 32, 0), &why),
          "wave bank size rules");
}

static void test_parts() {
    using namespace playerparts;
    CHECK(IsLinkMember("cl.bdl") && IsLinkMember("CL.BDL") && IsLinkMember("katsura.bdl") && IsLinkMember("hands.bdl") && IsLinkMember("yamu.bdl") &&
              IsLinkMember("pring.bdl") && IsLinkMember("hboots.bdl") && IsLinkMember("ymgcs00.bdl") && IsLinkMember("linktexbci4.bti") &&
              IsLinkMember("ymgcs00_ms.brk") && IsLinkMember("ymgcs00_ts.brk") && IsLinkMember("ymgcs00.btk"),
          "the Link members are wwrando's player list");
    CHECK(!IsLinkMember("swa.bdl") && !IsLinkMember("bow.bdl") && !IsLinkMember("bomb.bck") && !IsLinkMember("cl.bck"), "equipment and animations are not Link");
    CHECK(IsEquipmentMember("swa.bdl") && IsEquipmentMember("sha.bdl") && IsEquipmentMember("rock_mark.bti") && IsEquipmentMember("tswgripmsab.brk") &&
              IsEquipmentMember("bomb.brk") && IsEquipmentMember("tshms.btk") && !IsEquipmentMember("cl.bdl") && !IsEquipmentMember("bomb.bck") &&
              !IsEquipmentMember("ymgcs00.btk") && !IsEquipmentMember("readme.txt"),
          "the Equipment members are the other models, textures and texture animations");
    CHECK(IsPuppetEquipmentMember("SWA.BDL") && IsPuppetEquipmentMember("shms.bdl") && !IsPuppetEquipmentMember("bow.bdl"), "puppet equipment");

    const Bytes vanilla = make_rarc({{"cl.bdl", j3d("J3D2bdl4")}, {"swa.bdl", j3d("J3D2bdl3", 3)}, {"hands.bdl", j3d("J3D2bdl4", 3)}, {"bomb.bck", j3d("J3D1bck1", 1)}});
    const auto with = [&](bool body, bool equip, bool anim) {
        return make_rarc({{"cl.bdl", body ? j3d("J3D2bdl4", 4) : j3d("J3D2bdl4")},
                     {"swa.bdl", equip ? j3d("J3D2bdl3", 5) : j3d("J3D2bdl3", 3)},
                     {"hands.bdl", j3d("J3D2bdl4", 3)},
                     {"bomb.bck", anim ? j3d("J3D1bck1", 2) : j3d("J3D1bck1", 1)}});
    };
    bool readable = false;
    CHECK(DetectArc(with(true, false, false), vanilla, &readable) == kLink && readable, "body only is Link");
    CHECK(DetectArc(with(false, true, false), vanilla, &readable) == kEquipment, "equipment only is Equipment");
    CHECK(DetectArc(with(true, true, false), vanilla, &readable) == (kLink | kEquipment), "both");
    CHECK(DetectArc(with(false, false, true), vanilla, &readable) == 0, "only a changed animation is nothing (the game's are kept)");
    CHECK(DetectArc(vanilla, vanilla, &readable) == 0 && readable, "identical is nothing");
    CHECK(DetectArc(Bytes(200, 1), vanilla, &readable) == 0 && !readable, "unreadable is flagged");

    // Compose: Link members of A and Equipment members of B in the vanilla archive.
    Bytes base = vanilla;
    const Bytes a = with(true, false, true), b = with(false, true, true);
    CHECK(Compose(base, &a, &b), "compose");
    Bytes cl, swa, bomb, cl_a, swa_b;
    rarc::Extract(base, "cl.bdl", cl);
    rarc::Extract(base, "swa.bdl", swa);
    rarc::Extract(base, "bomb.bck", bomb);
    rarc::Extract(a, "cl.bdl", cl_a);
    rarc::Extract(b, "swa.bdl", swa_b);
    Bytes bomb_v;
    rarc::Extract(vanilla, "bomb.bck", bomb_v);
    CHECK(cl == cl_a && swa == swa_b && bomb == bomb_v, "composed: body from A, equipment from B, the game's animation");
    std::string why;
    CHECK(expand_ok(base, &why), "the composed archive is a valid archive (%s)", why.c_str());
    CHECK(DetectArc(base, vanilla, &readable) == (kLink | kEquipment), "and detects as both");
    CHECK(Describe(kLink | kVoice) == "Link + Voice" && Describe(0) == "Nothing new", "Describe");
}

// A JaiInit.aaf holding one wave system with a voice_0.aw group of `waves` waves (offset, size) at the given rate.
static Bytes make_aaf(const std::vector<std::pair<uint32_t, uint32_t>>& waves, float rate = 16000.0f) {
    Bytes b(0x400 + waves.size() * 0x30 + 0x200, 0);
    const size_t wsys = 0x100;
    put32(b, 0, 3);
    put32(b, 4, uint32_t(wsys));
    put32(b, 8, uint32_t(b.size() - wsys));
    put32(b, 12, 1);
    put32(b, 16, 0); // end of the table
    put32(b, 20, 0); // end of the records
    std::memcpy(&b[wsys], "WSYS", 4);
    const size_t winf = 0x20, group = 0x40;
    put32(b, wsys + 0x10, uint32_t(winf));
    std::memcpy(&b[wsys + winf], "WINF", 4);
    put32(b, wsys + winf + 4, 1);
    put32(b, wsys + winf + 8, uint32_t(group));
    std::memcpy(&b[wsys + group], "voice_0.aw", 10);
    put32(b, wsys + group + 0x70, uint32_t(waves.size()));
    for (size_t k = 0; k < waves.size(); ++k) {
        const size_t rec = 0x200 + k * 0x2c;
        put32(b, wsys + group + 0x74 + 4 * k, uint32_t(rec));
        uint32_t f;
        std::memcpy(&f, &rate, 4);
        put32(b, wsys + rec + 4, f);
        put32(b, wsys + rec + 8, waves[k].first);
        put32(b, wsys + rec + 12, waves[k].second);
    }
    b.resize(wsys + 0x200 + waves.size() * 0x2c + 0x20);
    put32(b, 8, uint32_t(b.size() - wsys));
    return b;
}

static void test_voice() {
    using namespace voice_bank;
    std::string why;
    // AFC: a frame of zero nibbles decodes to silence; a nonzero nibble with filter 0 is delta * n.
    Bytes silent(9, 0);
    std::vector<int16_t> pcm;
    CHECK(DecodeAfc(silent.data(), silent.size(), pcm, 100) && pcm.size() == 16 && pcm[0] == 0 && pcm[15] == 0, "AFC of silence");
    Bytes frame(9, 0);
    frame[0] = 0x20;  // scale 2 (delta 4), filter 0
    frame[1] = 0x31;  // nibbles 3 and 1
    CHECK(DecodeAfc(frame.data(), frame.size(), pcm, 100) && pcm[0] == 12 && pcm[1] == 4, "AFC scale and nibbles");
    frame[1] = 0x80; // -8
    DecodeAfc(frame.data(), frame.size(), pcm, 100);
    CHECK(pcm[0] == -32, "AFC sign (%d)", pcm[0]);
    CHECK(!DecodeAfc(silent.data(), 8, pcm, 100), "AFC of a partial frame is refused");
    CHECK(!DecodeAfc(silent.data(), 9, pcm, 15), "AFC over the sample cap is refused");
    CHECK(!DecodeAfc(nullptr, 9, pcm, 100) && !DecodeAfc(silent.data(), 0, pcm, 100), "AFC of nothing is refused");
    {   // loud frames with every filter: nothing crashes and the output stays 16 bit
        Bytes loud(9 * 64, 0xFF);
        for (size_t f = 0; f < 64; ++f)
            loud[f * 9] = uint8_t(0xF0 | (f & 15));
        CHECK(DecodeAfc(loud.data(), loud.size(), pcm, 100000) && pcm.size() == 64 * 16, "AFC extremes");
    }
    // The wave table.
    const Bytes aaf = make_aaf({{0, 90}, {90, 45}, {135, 9}});
    std::vector<Wave> waves;
    CHECK(ParseWaves(aaf, 144, waves, &why) && waves.size() == 3 && waves[1].offset == 90 && waves[1].size == 45 && waves[0].rate == 16000.0f, "a good wave table parses (%s)", why.c_str());
    CHECK(!ParseWaves(aaf, 140, waves, &why), "a wave past the end of voice_0.aw is refused");
    CHECK(!ParseWaves(make_aaf({{0, 90}}, 100.0f), 1000, waves, &why), "an impossible sample rate is refused");
    CHECK(!ParseWaves(make_aaf({{0xFFFFFFF0u, 90}}), 1000, waves, &why), "an offset that wraps is refused");
    CHECK(!ParseWaves(make_aaf({{0, 0xFFFFFFFFu}}), 1000, waves, &why), "a size over the cap is refused");
    CHECK(!ParseWaves(Bytes(), 100, waves, &why) && !ParseWaves(aaf, 0, waves, &why) && !ParseWaves(aaf, 100u << 20, waves, &why), "empty and oversized inputs are refused");
    // Every truncation and 3000 corruptions: no crash, and whatever parses stays inside the files.
    std::mt19937 rng(777);
    for (size_t n = 0; n < aaf.size(); n += 7) {
        Bytes cut(aaf.begin(), aaf.begin() + std::ptrdiff_t(n));
        ParseWaves(cut, 144, waves, &why);
    }
    int parsed = 0;
    for (int i = 0; i < 3000; ++i) {
        Bytes b = aaf;
        for (int k = 0; k < 1 + int(rng() % 8); ++k)
            b[rng() % b.size()] = uint8_t(rng());
        if (ParseWaves(b, 144, waves, &why)) {
            ++parsed;
            for (const Wave& w : waves)
                CHECK(uint64_t(w.offset) + w.size <= 144 && w.rate >= 4000.0f && w.rate <= 48000.0f, "a parsed wave is inside the file");
        }
    }
    std::printf("voice fuzz: %d of 3000 corrupted wave tables still parse\n", parsed);
    // Resampling and the voice table.
    CHECK(Resample({}, 16000.0f).empty() && Resample({1, 2, 3}, 0.0f).empty() && Resample({1, 2, 3}, -5.0f).empty(), "resampling nothing");
    CHECK(Resample(std::vector<int16_t>(100, 1000), 16000.0f).size() == 200, "16 kHz doubles");
    CHECK(WaveForVoice(-1) == -1 && WaveForVoice(kVoiceIds) == -1 && WaveForVoice(0) >= 0 && PitchForVoice(0) > 0.5f, "the voice table");
}

static Bytes read_file(const char* path);

static int voice_files(const char* aaf_path, const char* aw_path) {
    using namespace voice_bank;
    const Bytes aaf = read_file(aaf_path), aw = read_file(aw_path);
    Bank bank;
    std::string why;
    const bool ok = Build(aaf, aw, bank, &why);
    int voices = 0;
    for (const auto& v : bank.voice)
        voices += v.empty() ? 0 : 1;
    CHECK(ok && voices >= 40, "%s: bank built (%s), %d voices, %zu bytes", aaf_path, why.c_str(), voices, bank.Bytes());
    std::printf("%s: %d voices decoded, %zu bytes of PCM\n", aaf_path, voices, bank.Bytes());
    // Corrupted copies of the real files never crash the decoder, and the PCM stays capped.
    std::mt19937 rng(4242);
    for (int i = 0; i < 40; ++i) {
        Bytes a = aaf, w = aw;
        for (int k = 0; k < 6; ++k) {
            a[rng() % a.size()] = uint8_t(rng());
            w[rng() % w.size()] = uint8_t(rng());
        }
        Bank b2;
        Build(a, w, b2, nullptr);
        Bank b3;
        Build(aaf, Bytes(w.begin(), w.begin() + std::ptrdiff_t(rng() % w.size())), b3, nullptr);
        CHECK(b2.Bytes() <= kMaxBankBytes + kMaxWaveSamples * 2 && b3.Bytes() <= kMaxBankBytes + kMaxWaveSamples * 2, "bank memory stays capped");
    }
    return 0;
}

static Bytes read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc == 6 && std::strcmp(argv[1], "voicedump") == 0) { // AAF AW ID OUT: the PCM of one voice (s16 mono at 32 kHz)
        voice_bank::Bank bank;
        std::string why;
        if (!voice_bank::Build(read_file(argv[2]), read_file(argv[3]), bank, &why)) {
            std::printf("build failed: %s\n", why.c_str());
            return 1;
        }
        const auto& pcm = bank.voice[static_cast<size_t>(std::atoi(argv[4]))];
        std::ofstream(argv[5], std::ios::binary).write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(pcm.size() * 2));
        std::printf("%zu samples\n", pcm.size());
        return 0;
    }
    if (argc >= 4 && std::strcmp(argv[1], "voice") == 0) {
        for (int i = 2; i + 1 < argc; i += 2)
            voice_files(argv[i], argv[i + 1]);
        std::printf("%d checks, %d failures\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }
    if (argc >= 3 && std::strcmp(argv[1], "parts") == 0) {
        Bytes vanilla = read_file(argv[2]);
        rarc::Yaz0Expand(vanilla);
        for (int i = 3; i < argc; ++i) {
            Bytes mod = read_file(argv[i]);
            rarc::Yaz0Expand(mod);
            bool readable = false;
            const unsigned p = playerparts::DetectArc(mod, vanilla, &readable);
            std::printf("%s: %s%s\n", argv[i], readable ? playerparts::Describe(p).c_str() : "unreadable", "");
        }
        return 0;
    }
    if (argc >= 3) {
        const std::string mode = argv[1];
        for (int i = 2; i < argc; ++i) {
            Bytes b = read_file(argv[i]);
            std::string why;
            bool ok = false;
            if (mode == "arc")
                ok = cv::ExpandArchive(b, &why);
            else if (mode == "aaf")
                ok = cv::ValidateAaf(b, &why);
            else if (mode == "wave")
                ok = cv::ValidateWave(b, &why);
            CHECK(ok, "%s %s: %s", mode.c_str(), argv[i], why.c_str());
            std::printf("%s %s: %s\n", mode.c_str(), argv[i], ok ? "ok" : why.c_str());
        }
    } else {
        test_archive();
        test_manifest();
        test_audio();
        test_parts();
        test_voice();
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
