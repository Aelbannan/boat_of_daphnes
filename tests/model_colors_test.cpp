// Standalone test of windows/src/model_colors.cpp (and gx_texture.cpp, png_decode.cpp): the
// colors of a player model built from its files. Run through tests/model_colors_test.py, which
// compiles this with the Visual Studio clang and feeds it the files (see that file's docstring).
//
//   model_colors_test vanilla <Link.arc, Yaz0 expanded> <link_colors.py output folder>
//                             <wwrando link_metadata.txt> <wwrando link_color_masks folder>
//       The vanilla Link built here has the same keys and RGBA as scripts/windows/link_colors.py.
//   model_colors_test model <model folder> <hero colors> <casual colors> <hero presets> <casual presets> <eyebrows 0|1>
//       metadata parsed, every picture in color_masks found, every mask the size of its texture.
//   model_colors_test recolor <model folder> <scenario: both|casual> <output file>
//       Recolors with fixed colors and writes the result for tests/model_colors_reference.py.
//
// Exit status 0 when every check passes.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "../windows/src/model_colors.h"
#include "../windows/src/png_decode.h"
#include "picojson.h"

namespace fs = std::filesystem;

static int failures;
#define CHECK(cond, ...)                                      \
    do {                                                      \
        if (!(cond)) {                                        \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);  \
            std::printf(__VA_ARGS__);                         \
            std::printf("\n");                                \
            ++failures;                                       \
        }                                                     \
    } while (0)

static std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Every .png under <folder>\color_masks, keyed as model_colors::Files wants (lower case, relative, '/').
static void read_masks(const fs::path& root, const fs::path& masks_dir, const std::string& prefix,
                       std::map<std::string, std::vector<uint8_t>>& out) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(masks_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || lower(it->path().extension().string()) != ".png")
            continue;
        std::string rel = fs::relative(it->path(), masks_dir).generic_string();
        out[prefix + lower(rel)] = read_file(it->path());
    }
    (void)root;
}

static const model_colors::Texture* tex_named(const model_colors::Set& s, const std::string& name) {
    for (const auto& t : s.textures)
        if (t.name == name)
            return &t;
    return nullptr;
}

// --- vanilla: against the files tests/../scripts/windows/link_colors.py wrote -------------------

static int vanilla(int argc, char** argv) {
    if (argc < 6) {
        std::printf("usage: vanilla <Link.arc> <index folder> <link_metadata.txt> <link_color_masks>\n");
        return 2;
    }
    const fs::path index_dir = argv[3];
    model_colors::Files f;
    f.name = "Link";
    f.link_arc = read_file(argv[2]);
    const auto meta = read_file(argv[4]);
    f.metadata.assign(meta.begin(), meta.end());
    read_masks(argv[5], argv[5], "color_masks/", f.masks);
    model_colors::Set set;
    std::string err;
    CHECK(model_colors::Build(f, set, &err), "Build: %s", err.c_str());
    for (const auto& w : set.warnings)
        std::printf("warning: %s\n", w.c_str());

    const auto index_bytes = read_file(index_dir / "index.json");
    picojson::value doc;
    const std::string text(index_bytes.begin(), index_bytes.end());
    const std::string perr = picojson::parse(doc, text);
    CHECK(perr.empty() && doc.is<picojson::object>(), "index.json: %s", perr.c_str());
    if (!perr.empty())
        return 1;
    const auto& root = doc.get<picojson::object>();

    int tex_total = 0, key_ok = 0, rgba_ok = 0;
    for (const auto& v : root.at("textures").get<picojson::array>()) {
        const auto& e = v.get<picojson::object>();
        const std::string name = e.at("name").get<std::string>(), key = e.at("key").get<std::string>();
        ++tex_total;
        const auto* t = tex_named(set, name);
        CHECK(t != nullptr, "texture %s missing", name.c_str());
        if (t == nullptr)
            continue;
        if (!t->keys.empty() && t->keys[0] == key)
            ++key_ok;
        else
            CHECK(false, "%s: key %s, python %s", name.c_str(), t->keys.empty() ? "-" : t->keys[0].c_str(), key.c_str());
        const auto bytes = read_file(index_dir / e.at("file").get<std::string>());
        uint32_t wh[2] = {};
        if (bytes.size() >= 8)
            std::memcpy(wh, bytes.data(), 8);
        const bool same = bytes.size() == 8 + t->rgba.bytes.size() && wh[0] == t->rgba.width && wh[1] == t->rgba.height &&
                          std::memcmp(bytes.data() + 8, t->rgba.bytes.data(), t->rgba.bytes.size()) == 0;
        if (same)
            ++rgba_ok;
        else
            CHECK(false, "%s: RGBA differs from python's", name.c_str());
    }
    std::printf("vanilla textures: %d in python's index, %d keys equal, %d RGBA images equal\n", tex_total, key_ok, rgba_ok);

    int mask_total = 0, mask_ok = 0, mask_all = 0;
    for (const auto& v : root.at("masks").get<picojson::array>()) {
        const auto& e = v.get<picojson::object>();
        const std::string file = e.at("file").get<std::string>();
        if (file.size() > 9 && file.compare(file.size() - 9, 9, "_all.mask") == 0) {
            ++mask_all; // python's stand-in for "the whole texture takes Skin/Hair": Recolor's _color_name rule
            continue;
        }
        ++mask_total;
        const bool casual = e.at("outfit").get<std::string>() == "casual";
        const std::string color = e.at("color").get<std::string>(), texture = e.at("texture").get<std::string>();
        const model_colors::Mask* found = nullptr;
        for (const auto& m : set.masks)
            if (m.casual == casual && m.color == color && m.texture == texture)
                found = &m;
        CHECK(found != nullptr, "mask %s %s %s missing", casual ? "casual" : "hero", color.c_str(), texture.c_str());
        if (found == nullptr)
            continue;
        const auto bytes = read_file(index_dir / file);
        uint32_t wh[2] = {};
        if (bytes.size() >= 8)
            std::memcpy(wh, bytes.data(), 8);
        if (bytes.size() == 8 + found->image.bytes.size() && wh[0] == found->image.width && wh[1] == found->image.height &&
            std::memcmp(bytes.data() + 8, found->image.bytes.data(), found->image.bytes.size()) == 0)
            ++mask_ok;
        else
            CHECK(false, "mask %s %s: differs from python's", color.c_str(), texture.c_str());
    }
    // Nothing built here that python did not write.
    int extra = 0;
    for (const auto& m : set.masks) {
        bool in_index = false;
        for (const auto& v : root.at("masks").get<picojson::array>()) {
            const auto& e = v.get<picojson::object>();
            if ((e.at("outfit").get<std::string>() == "casual") == m.casual && e.at("color").get<std::string>() == m.color &&
                e.at("texture").get<std::string>() == m.texture)
                in_index = true;
        }
        if (!in_index) {
            ++extra;
            CHECK(false, "mask %s %s %s is not in python's index", m.casual ? "casual" : "hero", m.color.c_str(), m.texture.c_str());
        }
    }
    std::printf("vanilla masks: %d in python's index (plus %d whole-texture stand-ins), %d equal, %d extra here\n", mask_total,
                mask_all, mask_ok, extra);
    CHECK(set.warnings.empty(), "%zu warnings", set.warnings.size());
    std::printf("vanilla colors: hero %zu, casual %zu, presets %zu/%zu\n", set.outfits[0].colors.size(),
                set.outfits[1].colors.size(), set.outfits[0].presets.size(), set.outfits[1].presets.size());
    CHECK(set.outfits[0].colors.size() == 12 && set.outfits[1].colors.size() == 11, "link_metadata.txt has 12 and 11 colors");
    CHECK(tex_total > 0 && key_ok == tex_total && rgba_ok == tex_total, "texture mismatch");
    CHECK(mask_total > 0 && mask_ok == mask_total, "mask mismatch");
    return failures ? 1 : 0;
}

// --- a model folder --------------------------------------------------------------------------

static bool load_model(const fs::path& dir, model_colors::Files& f) {
    f.name = dir.filename().string();
    f.link_arc = read_file(dir / "Link.arc");
    const auto meta = read_file(dir / "metadata.txt");
    f.metadata.assign(meta.begin(), meta.end());
    read_masks(dir, dir / "color_masks", "color_masks/", f.masks);
    return !f.link_arc.empty();
}

static int model(int argc, char** argv) {
    if (argc < 8) {
        std::printf("usage: model <folder> <hero colors> <casual colors> <hero presets> <casual presets> <eyebrows>\n");
        return 2;
    }
    const fs::path dir = argv[2];
    model_colors::Files f;
    CHECK(load_model(dir, f), "no Link.arc in %s", dir.string().c_str());
    model_colors::Set set;
    std::string err;
    CHECK(model_colors::Build(f, set, &err), "Build: %s", err.c_str());
    std::printf("%s: hero colors %zu, casual colors %zu, presets %zu/%zu, eyebrows %d, author \"%s\"\n", f.name.c_str(),
                set.outfits[0].colors.size(), set.outfits[1].colors.size(), set.outfits[0].presets.size(),
                set.outfits[1].presets.size(), set.colored_eyebrows, set.author.c_str());
    for (const auto& w : set.warnings)
        std::printf("warning: %s\n", w.c_str());
    CHECK(set.warnings.empty(), "%zu warnings", set.warnings.size());
    CHECK(set.outfits[0].colors.size() == size_t(std::atoi(argv[3])), "hero colors");
    CHECK(set.outfits[1].colors.size() == size_t(std::atoi(argv[4])), "casual colors");
    CHECK(set.outfits[0].presets.size() == size_t(std::atoi(argv[5])), "hero presets");
    CHECK(set.outfits[1].presets.size() == size_t(std::atoi(argv[6])), "casual presets");
    CHECK(set.colored_eyebrows == (std::atoi(argv[7]) != 0), "has_colored_eyebrows");
    // The bare hex of the old format reads as hex: the first color of each outfit and a spot check.
    for (int o = 0; o < 2; ++o)
        for (const auto& c : set.outfits[o].colors)
            CHECK(c.base <= 0xFFFFFF, "base color %s", c.name.c_str());

    // Every picture in color_masks\ matched a color of the metadata.
    int pngs = 0, found = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir / "color_masks", ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || lower(it->path().extension().string()) != ".png")
            continue;
        ++pngs;
        const std::string rel = "color_masks/" + lower(fs::relative(it->path(), dir / "color_masks").generic_string());
        bool hit = false;
        for (const auto& p : set.mask_paths)
            hit = hit || p == rel;
        if (hit)
            ++found;
        else
            CHECK(false, "no color of metadata.txt uses %s", rel.c_str());
    }
    std::printf("  %d mask pictures, %d used by a color, %zu masks built\n", pngs, found, set.masks.size());
    CHECK(pngs > 0 && found == pngs, "masks");

    // Every mask is the size of its texture: the size is read from the picture's own header (IHDR),
    // not from the decoder.
    int sized = 0;
    for (const auto& m : set.masks) {
        const auto* t = tex_named(set, m.texture);
        CHECK(t != nullptr, "mask %s has no texture %s", m.color.c_str(), m.texture.c_str());
        const auto& bytes = f.masks[m.path];
        CHECK(bytes.size() > 24, "%s: not a PNG", m.path.c_str());
        if (t == nullptr || bytes.size() <= 24)
            continue;
        const uint32_t w = uint32_t(bytes[16]) << 24 | uint32_t(bytes[17]) << 16 | uint32_t(bytes[18]) << 8 | bytes[19];
        const uint32_t h = uint32_t(bytes[20]) << 24 | uint32_t(bytes[21]) << 16 | uint32_t(bytes[22]) << 8 | bytes[23];
        CHECK(w == t->rgba.width && h == t->rgba.height, "%s is %ux%u, %s is %ux%u", m.path.c_str(), w, h, m.texture.c_str(),
              t->rgba.width, t->rgba.height);
        CHECK(m.image.width == w && m.image.height == h, "%s: the decoder read %ux%u", m.path.c_str(), m.image.width, m.image.height);
        ++sized;
    }
    std::printf("  %d masks the size of their textures; textures:", sized);
    for (const auto& t : set.textures)
        std::printf(" %s(%ux%u)", t.name.c_str(), t.rgba.width, t.rgba.height);
    std::printf("\n");
    CHECK(sized == int(set.masks.size()), "mask sizes");
    CHECK(tex_named(set, "linktexS3TC") && tex_named(set, "linktexbci4") && tex_named(set, "hitomi") &&
              tex_named(set, "handsS3TC") && tex_named(set, "mouthS3TC.1") && tex_named(set, "katsuraS3TC"),
          "a texture is missing from Link.arc");
    CHECK(!set.colored_eyebrows || tex_named(set, "mayuh.6") != nullptr, "eyebrow textures");
    // The Cosmetics Editor's ids.
    if (!set.outfits[0].colors.empty())
        std::printf("  id of the first hero color: %s\n", model_colors::ColorId(set, false, set.outfits[0].colors[0].name).c_str());
    return failures ? 1 : 0;
}

// --- recolor: fixed colors, written for the Python reference -----------------------------------

// The same formula as tests/model_colors_reference.py.
static uint32_t pick_color(int outfit, size_t index) {
    return uint32_t((uint64_t(outfit) * 131 + index * 977 + 12345) * 0x9E3779B1u) & 0xFFFFFFu;
}

static int recolor(int argc, char** argv) {
    if (argc < 5) {
        std::printf("usage: recolor <folder> <both|casual> <output>\n");
        return 2;
    }
    model_colors::Files f;
    CHECK(load_model(argv[2], f), "no Link.arc");
    model_colors::Set set;
    std::string err;
    CHECK(model_colors::Build(f, set, &err), "Build: %s", err.c_str());
    const bool both = std::strcmp(argv[3], "both") == 0;
    const auto result = model_colors::Recolor(set, [&](bool casual, const std::string& color) -> std::optional<uint32_t> {
        if (!casual && !both)
            return std::nullopt;
        const auto& colors = set.outfits[casual ? 1 : 0].colors;
        for (size_t i = 0; i < colors.size(); ++i)
            if (colors[i].name == color)
                return pick_color(casual ? 1 : 0, i);
        return std::nullopt;
    });
    std::ofstream out(argv[4], std::ios::binary);
    const uint32_t n = uint32_t(result.size());
    out.write(reinterpret_cast<const char*>(&n), 4);
    for (const auto& r : result) {
        const uint32_t len = uint32_t(r.texture.size()), w = r.image.width, h = r.image.height;
        out.write(reinterpret_cast<const char*>(&len), 4);
        out.write(r.texture.data(), len);
        out.write(reinterpret_cast<const char*>(&w), 4);
        out.write(reinterpret_cast<const char*>(&h), 4);
        out.write(reinterpret_cast<const char*>(r.image.bytes.data()), std::streamsize(r.image.bytes.size()));
    }
    std::printf("%s (%s): %zu textures recolored:", f.name.c_str(), argv[3], result.size());
    for (const auto& r : result)
        std::printf(" %s", r.texture.c_str());
    std::printf("\n");
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "vanilla") == 0)
        return vanilla(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "model") == 0)
        return model(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "recolor") == 0)
        return recolor(argc, argv);
    std::printf("usage: model_colors_test vanilla|model|recolor ... (see the file's header)\n");
    return 2;
}
