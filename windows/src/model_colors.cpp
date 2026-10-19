// The colors of a player model: see model_colors.h.
//
// Ports of wwrando's customizer.py (get_model_metadata, change_player_custom_colors) and the color
// exchange of texture_utils.py (wwrando's gclib, MIT, Copyright (c) 2018 LagoLunatic; the exchange
// is read from the same code in BetterWW's wwlib/texture_utils.py:1161-1279). The textures and
// their keys follow scripts/windows/link_colors.py.
#include "model_colors.h"

#include "gx_texture.h"
#include "png_decode.h"
#include "rarc.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>

#if defined(__clang__)
// Exchange must round exactly as Python's floats do: no fused multiply-add.
#pragma clang fp contract(off)
#endif

namespace model_colors {
namespace {

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        --b;
    return s.substr(a, b - a);
}

// ---------------------------------------------------------------------------
// metadata.txt: the YAML subset wwrando's metadata uses (customizer.py:44-144) as a tree of
// "key: value" lines nested by indentation. Tab indentation counts as two spaces (:72-73).
// ---------------------------------------------------------------------------

struct Kid;
struct Node {
    std::string value;
    std::vector<Kid> kids;
    const Node* find(const std::string& key) const;
};
struct Kid { // a key and its node (std::pair<std::string, Node> does not take Node before it is complete)
    std::string first;
    Node second;
};
inline const Node* Node::find(const std::string& key) const {
    for (const auto& k : kids)
        if (k.first == key)
            return &k.second;
    return nullptr;
}

// A scalar as the value of a line: quotes removed, a trailing " # comment" cut from a plain one.
std::string scalar(const std::string& raw) {
    std::string v = trim(raw);
    if (!v.empty() && (v[0] == '"' || v[0] == '\'')) {
        const size_t end = v.find(v[0], 1);
        return end == std::string::npos ? v.substr(1) : v.substr(1, end - 1);
    }
    const size_t hash = v.find(" #");
    return hash == std::string::npos ? v : trim(v.substr(0, hash));
}

Node parse_yaml(const std::string& text) {
    Node root;
    std::vector<std::pair<int, Node*>> stack; // indent, node
    size_t at = 0;
    while (at <= text.size()) {
        size_t eol = text.find('\n', at);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(at, eol - at);
        at = eol + 1;
        std::string expanded;
        for (char c : line) {
            if (c == '\t')
                expanded += "  ";
            else if (c != '\r')
                expanded += c;
        }
        line = expanded;
        size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ')
            ++indent;
        const std::string body = trim(line);
        if (body.empty() || body[0] == '#' || body.rfind("- ", 0) == 0)
            continue;
        size_t colon = std::string::npos;
        std::string key;
        if (body[0] == '"' || body[0] == '\'') {
            const size_t end = body.find(body[0], 1);
            if (end != std::string::npos && end + 1 < body.size() && body[end + 1] == ':') {
                key = body.substr(1, end - 1);
                colon = end + 1;
            }
        } else {
            colon = body.find(':');
            if (colon != std::string::npos)
                key = trim(body.substr(0, colon));
        }
        while (!stack.empty() && stack.back().first >= int(indent))
            stack.pop_back();
        if (colon == std::string::npos) { // the continuation of a multi-line value
            if (!stack.empty()) {
                Node& n = *stack.back().second;
                n.value += (n.value.empty() ? "" : " ") + scalar(body);
            }
            continue;
        }
        Node& parent = stack.empty() ? root : *stack.back().second;
        parent.kids.emplace_back(key, Node{});
        Node& n = parent.kids.back().second;
        const std::string rest = trim(body.substr(colon + 1));
        n.value = (rest == "|" || rest == ">" || rest == "|-" || rest == ">-") ? "" : scalar(rest);
        stack.emplace_back(int(indent), &n);
    }
    return root;
}

// A base color or preset color: "0xRRGGBB", or the old format's bare "RRGGBB" with or without
// quotes (customizer.py:62-73 rewrites those to 0x form: YAML would read 633008 as a decimal).
bool parse_hex_color(const std::string& s, uint32_t& rgb) {
    std::string t = s;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        t = t.substr(2);
    else if (t.size() != 6)
        return false;
    if (t.empty() || t.size() > 6)
        return false;
    uint32_t v = 0;
    for (char c : t) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
        v = v << 4 | uint32_t(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(c) - 'a' + 10);
    }
    rgb = v;
    return true;
}

bool parse_bool(const std::string& s) {
    const std::string t = lower(s);
    return t == "true" || t == "yes" || t == "on";
}

// ---------------------------------------------------------------------------
// Link.arc: a RARC archive. Members by lower case path; a member may be Yaz0 compressed (flag 0x80).
// ---------------------------------------------------------------------------

struct Bytes {
    const uint8_t* d;
    size_t n;
    mutable bool bad = false;
    Bytes(const uint8_t* data, size_t size) : d(data), n(size) {}
    uint32_t u8(size_t at) const {
        if (at >= n) {
            bad = true;
            return 0;
        }
        return d[at];
    }
    uint32_t u16(size_t at) const { return u8(at) << 8 | u8(at + 1); }
    uint32_t u32(size_t at) const { return u16(at) << 16 | u16(at + 2); }
};

struct Member {
    size_t offset, size;
    bool yaz0;
};

bool index_rarc(const std::vector<uint8_t>& data, std::map<std::string, Member>& out, std::string* error) {
    const Bytes b(data.data(), data.size());
    if (data.size() < 0x40 || std::memcmp(data.data(), "RARC", 4) != 0) {
        *error = "Link.arc is not a RARC archive";
        return false;
    }
    const size_t base = b.u32(8), payload = base + b.u32(12);
    const size_t n_nodes = b.u32(base), node_off = b.u32(base + 4), file_off = b.u32(base + 12),
                 str_off = b.u32(base + 20);
    const size_t nodes = base + node_off, entries = base + file_off, strings = base + str_off;
    if (b.bad) {
        *error = "Link.arc has a bad RARC header";
        return false;
    }
    std::function<bool(size_t, const std::string&, int)> walk = [&](size_t index, const std::string& prefix, int depth) {
        if (depth > 16 || index >= n_nodes)
            return false;
        const size_t node = nodes + index * 16;
        const size_t count = b.u16(node + 10), first = b.u32(node + 12);
        for (size_t i = first; i < first + count; ++i) {
            const size_t e = entries + i * 20;
            const uint32_t flags = b.u8(e + 4);
            std::string name;
            for (size_t s = strings + b.u16(e + 6); b.u8(s) != 0 && !b.bad; ++s)
                name += char(data[s]);
            const size_t off = b.u32(e + 8), size = b.u32(e + 12);
            if (b.bad)
                return false;
            if (name == "." || name == "..")
                continue;
            const std::string path = prefix + lower(name);
            if (flags & 2) {
                if (!walk(off, path + "/", depth + 1))
                    return false;
            } else if (payload + off + size <= data.size()) {
                out[path] = {payload + off, size, (flags & 0x80) != 0};
            }
        }
        return true;
    };
    if (!walk(0, "", 0)) {
        *error = "Link.arc has a bad RARC directory";
        return false;
    }
    return true;
}

bool member_bytes(const std::vector<uint8_t>& arc, const std::map<std::string, Member>& index, const std::string& path,
                  std::vector<uint8_t>& out) {
    auto it = index.find(path);
    if (it == index.end()) { // the same file under another folder
        const std::string tail = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
        for (auto j = index.begin(); j != index.end(); ++j) {
            const std::string& p = j->first;
            if (p.size() >= tail.size() && p.compare(p.size() - tail.size(), tail.size(), tail) == 0 &&
                (p.size() == tail.size() || p[p.size() - tail.size() - 1] == '/')) {
                it = j;
                break;
            }
        }
        if (it == index.end())
            return false;
    }
    out.assign(arc.begin() + std::ptrdiff_t(it->second.offset), arc.begin() + std::ptrdiff_t(it->second.offset + it->second.size));
    if (it->second.yaz0 && out.size() >= 4 && std::memcmp(out.data(), "Yaz0", 4) == 0)
        rarc::Yaz0Expand(out);
    return true;
}

// Every TEX1 header called `name` in a J3D2 model (link_colors.py bdl_headers): the list can hold
// the same name more than once.
std::vector<gx_texture::Texture> bdl_textures(const std::vector<uint8_t>& blob, const std::string& name) {
    std::vector<gx_texture::Texture> found;
    const Bytes b(blob.data(), blob.size());
    if (blob.size() < 32 || std::memcmp(blob.data(), "J3D2", 4) != 0)
        return found;
    const size_t count = b.u32(12);
    size_t at = 32;
    for (size_t c = 0; c < count && at + 8 <= blob.size(); ++c) {
        const size_t size = b.u32(at + 4);
        if (size < 8)
            break;
        if (std::memcmp(&blob[at], "TEX1", 4) == 0) {
            const size_t hdr = b.u32(at + 12), strs = b.u32(at + 16), tbl = at + strs;
            const size_t n = b.u16(tbl);
            for (size_t i = 0; i < n && !b.bad; ++i) {
                std::string s;
                for (size_t p = tbl + b.u16(tbl + 6 + i * 4); b.u8(p) != 0 && !b.bad; ++p)
                    s += char(blob[p]);
                if (s == name) {
                    gx_texture::Texture t;
                    if (gx_texture::Parse(blob.data(), blob.size(), at + hdr + i * 32, t, nullptr))
                        found.push_back(std::move(t));
                }
            }
        }
        at += size;
    }
    return found;
}

// ---------------------------------------------------------------------------
// Where each texture lives in Link.arc (customizer.py:404-421, 460-533).
// ---------------------------------------------------------------------------

struct Source {
    std::string name;   // the texture's name
    std::string member; // the archive member
    bool bare;          // a bare BTI file instead of a TEX1 entry of a model
};

std::vector<Source> texture_sources(bool eyebrows) {
    std::vector<Source> s;
    s.push_back({"linktexS3TC", "bdl/cl.bdl", false});
    s.push_back({"hitomi", "bdl/cl.bdl", false});
    for (int i = 1; i <= 9; ++i)
        s.push_back({"mouthS3TC." + std::to_string(i), "bdl/cl.bdl", false});
    if (eyebrows)
        for (int i = 1; i <= 6; ++i)
            s.push_back({"mayuh." + std::to_string(i), "bdl/cl.bdl", false});
    s.push_back({"handsS3TC", "bdl/hands.bdl", false});
    s.push_back({"katsuraS3TC", "bdl/katsura.bdl", false});
    s.push_back({"linktexbci4", "tex/linktexbci4.bti", true});
    return s;
}

const Texture* find_texture(const Set& set, const std::string& name) {
    for (const Texture& t : set.textures)
        if (t.name == name)
            return &t;
    return nullptr;
}

const Mask* find_mask(const Set& set, bool casual, const std::string& texture, const std::string& color) {
    for (const Mask& m : set.masks)
        if (m.casual == casual && m.texture == texture && m.color == color)
            return &m;
    return nullptr;
}

std::string safe_part(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == ' ')
            continue;
        out += (c == '.' || static_cast<unsigned char>(c) < 0x20) ? '_' : c;
    }
    return out;
}

} // namespace

std::string ColorId(const Set& set, bool casual, const std::string& color) {
    return "Link." + set.id_name + (casual ? ".Casual." : ".Hero.") + safe_part(color);
}

bool Build(const Files& files, Set& out, std::string* error) {
    std::string local;
    std::string& err = error != nullptr ? *error : local;
    out = Set{};
    out.name = files.name;
    out.id_name = safe_part(files.name);
    if (out.id_name.empty())
        out.id_name = "Model";

    // --- metadata.txt (customizer.py:44-144) ---
    const Node meta = parse_yaml(files.metadata);
    const auto text = [&](const char* key, const char* fallback) {
        const Node* n = meta.find(key);
        return n != nullptr && !n->value.empty() ? n->value : std::string(fallback);
    };
    out.author = text("author", "");
    out.comment = text("comment", "");
    out.casual_text = text("casual_clothes_option_text", "");
    if (out.casual_text.size() > 28) // cosmetic_tab.py:505-509
        out.casual_text.resize(28);
    if (const Node* n = meta.find("disable_casual_clothes"))
        out.disable_casual = parse_bool(n->value);
    if (const Node* n = meta.find("has_colored_eyebrows"))
        out.colored_eyebrows = parse_bool(n->value);
    out.casual_hair_color = text("casual_hair_color_name", "Hair");
    for (int o = 0; o < 2; ++o) {
        const std::string prefix = o ? "casual" : "hero";
        Outfit& of = out.outfits[o];
        if (const Node* colors = meta.find(prefix + "_custom_colors"))
            for (const auto& [name, n] : colors->kids) {
                uint32_t rgb;
                if (name.empty() || std::any_of(of.colors.begin(), of.colors.end(), [&](const Color& c) { return c.name == name; }))
                    continue;
                if (parse_hex_color(n.value, rgb))
                    of.colors.push_back({name, rgb});
                else
                    out.warnings.push_back("custom color \"" + name + "\" has an invalid base color: \"" + n.value + "\"");
            }
        if (const Node* presets = meta.find(prefix + "_color_presets"))
            for (const auto& [pname, pn] : presets->kids) {
                Preset p;
                p.name = pname;
                for (const auto& [cname, cn] : pn.kids) {
                    uint32_t rgb;
                    if (parse_hex_color(cn.value, rgb))
                        p.colors.emplace_back(cname, rgb);
                    else
                        out.warnings.push_back("color preset \"" + pname + "\"'s color \"" + cname + "\" is invalid: \"" + cn.value + "\"");
                }
                of.presets.push_back(std::move(p));
            }
        of.hands_color = text((prefix + "_hands_color_name").c_str(), "Skin");
        of.mouth_color = text((prefix + "_mouth_color_name").c_str(), "Skin");
        of.hitomi_color = text((prefix + "_hitomi_color_name").c_str(), "Eyes");
        of.eyebrow_color = text((prefix + "_eyebrow_color_name").c_str(), "Hair");
    }

    // --- Link.arc ---
    std::vector<uint8_t> arc = files.link_arc;
    if (arc.size() >= 4 && std::memcmp(arc.data(), "Yaz0", 4) == 0)
        rarc::Yaz0Expand(arc);
    std::map<std::string, Member> index;
    if (!index_rarc(arc, index, &err))
        return false;
    if (out.outfits[0].colors.empty() && out.outfits[1].colors.empty())
        return true; // nothing to color: no textures needed

    std::map<std::string, std::vector<uint8_t>> members;
    for (const Source& src : texture_sources(out.colored_eyebrows)) {
        std::vector<gx_texture::Texture> headers;
        if (!members.count(src.member)) {
            std::vector<uint8_t> blob;
            if (member_bytes(arc, index, src.member, blob))
                members[src.member] = std::move(blob);
        }
        const auto m = members.find(src.member);
        if (m == members.end()) {
            out.warnings.push_back("Link.arc has no " + src.member + ": " + src.name + " not recolored");
            continue;
        }
        if (src.bare) {
            gx_texture::Texture t;
            if (gx_texture::Parse(m->second.data(), m->second.size(), 0, t, nullptr))
                headers.push_back(std::move(t));
        } else {
            headers = bdl_textures(m->second, src.name);
        }
        if (headers.empty()) {
            out.warnings.push_back("texture " + src.name + " not found in " + src.member);
            continue;
        }
        Texture tex;
        tex.name = src.name;
        std::string why;
        for (size_t i = 0; i < headers.size(); ++i) {
            const gx_texture::Texture& h = headers[i];
            const std::string key = gx_texture::Key(h, &why);
            if (key.empty()) {
                out.warnings.push_back(src.name + ": " + why);
                continue;
            }
            if (i == 0 || tex.rgba.bytes.empty()) {
                if (!gx_texture::DecodeRgba(h, tex.rgba.bytes, &why)) {
                    out.warnings.push_back(src.name + ": " + why);
                    tex.rgba.bytes.clear();
                    continue;
                }
                tex.rgba.width = h.width;
                tex.rgba.height = h.height;
            } else if (h.width != tex.rgba.width || h.height != tex.rgba.height) {
                out.warnings.push_back(src.name + " has headers of different sizes; one is not recolored");
                continue;
            }
            if (std::find(tex.keys.begin(), tex.keys.end(), key) == tex.keys.end())
                tex.keys.push_back(key);
        }
        if (!tex.keys.empty())
            out.textures.push_back(std::move(tex));
    }

    // --- masks (customizer.py:98-122, 436-449) ---
    // In a mask, pure red FF0000FF marks the pixels to change; pure white and fully transparent
    // are left alone and any other color is an error in wwrando (texture_utils.py:1181-1202). Here
    // such pixels are left alone and counted in a warning.
    struct Slot {
        std::string path, texture;
    };
    bool masked[2][3] = {}; // per outfit: hands, mouth, hitomi
    for (int o = 0; o < 2; ++o) {
        const std::string prefix = o ? "casual" : "hero";
        Outfit& of = out.outfits[o];
        for (const Color& c : of.colors) {
            const std::string lc = lower(c.name);
            std::vector<Slot> slots;
            slots.push_back({"color_masks/" + prefix + "_" + lc + ".png", o ? "linktexbci4" : "linktexS3TC"});
            slots.push_back({"color_masks/hands_" + prefix + "_" + lc + ".png", "handsS3TC"});
            slots.push_back({"color_masks/hitomi_" + prefix + "_" + lc + ".png", "hitomi"});
            for (int i = 1; i <= 9; ++i)
                slots.push_back({"color_masks/mouths/mouths3tc." + std::to_string(i) + "_" + lc + ".png",
                                 "mouthS3TC." + std::to_string(i)});
            for (size_t si = 0; si < slots.size(); ++si) {
                const auto f = files.masks.find(slots[si].path);
                if (f == files.masks.end())
                    continue;
                if (si == 1)
                    masked[o][0] = true;
                else if (si >= 3)
                    masked[o][1] = true;
                else if (si == 2)
                    masked[o][2] = true;
                if (std::find(out.mask_paths.begin(), out.mask_paths.end(), slots[si].path) == out.mask_paths.end())
                    out.mask_paths.push_back(slots[si].path);
                const Texture* tex = find_texture(out, slots[si].texture);
                if (tex == nullptr) {
                    out.warnings.push_back(slots[si].path + ": texture " + slots[si].texture + " is not in Link.arc; skipped");
                    continue;
                }
                png_decode::Image png;
                std::string why;
                if (!png_decode::Decode(f->second.data(), f->second.size(), png, &why)) {
                    out.warnings.push_back(slots[si].path + ": " + why + "; skipped");
                    continue;
                }
                if (png.width != tex->rgba.width || png.height != tex->rgba.height) {
                    char buf[160];
                    std::snprintf(buf, sizeof buf, ": %ux%u but %s is %ux%u; skipped", png.width, png.height,
                                  slots[si].texture.c_str(), tex->rgba.width, tex->rgba.height);
                    out.warnings.push_back(slots[si].path + buf);
                    continue;
                }
                Mask m;
                m.casual = o == 1;
                m.color = c.name;
                m.texture = slots[si].texture;
                m.path = slots[si].path;
                m.image.width = png.width;
                m.image.height = png.height;
                m.image.bytes.assign(size_t(png.width) * png.height, 0);
                size_t invalid = 0;
                for (size_t p = 0; p < m.image.bytes.size(); ++p) {
                    const uint8_t* px = &png.rgba[p * 4];
                    if (px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255)
                        m.image.bytes[p] = 1;
                    else if (!(px[3] == 0 || (px[0] == 255 && px[1] == 255 && px[2] == 255 && px[3] == 255)))
                        ++invalid;
                }
                if (invalid)
                    out.warnings.push_back(slots[si].path + ": " + std::to_string(invalid) +
                                           " pixels are neither pure red, pure white nor transparent");
                out.masks.push_back(std::move(m));
            }
        }
    }
    // The "_color_name" fields are ignored once any mask of that type exists (customizer.py:436-449).
    for (int o = 0; o < 2; ++o) {
        Outfit& of = out.outfits[o];
        if (masked[o][0])
            of.hands_color.clear();
        if (masked[o][1])
            of.mouth_color.clear();
        if (masked[o][2])
            of.hitomi_color.clear();
        // A color with no mask on the body is an error in wwrando unless it is an eye color (:479-483).
        const std::string body = o ? "linktexbci4" : "linktexS3TC";
        if (find_texture(out, body) == nullptr)
            continue;
        for (const Color& c : of.colors)
            if (find_mask(out, o == 1, body, c.name) == nullptr && find_mask(out, o == 1, "hitomi", c.name) == nullptr &&
                c.name != of.hitomi_color)
                out.warnings.push_back(std::string(o ? "casual" : "hero") + " color \"" + c.name + "\" has no mask on " + body);
    }
    return true;
}

// ---------------------------------------------------------------------------
// The color exchange (texture_utils.py:1161-1279). Python's colorsys on doubles, with the
// truncating int() of its code, so the pixels are the ones wwrando writes.
// ---------------------------------------------------------------------------

namespace {

double py_mod1(double x) { // Python's x % 1.0
    double m = std::fmod(x, 1.0);
    if (m != 0.0 && m < 0.0)
        m += 1.0;
    return m;
}

void rgb_to_hsv(double r, double g, double b, double& h, double& s, double& v) {
    const double maxc = std::max({r, g, b}), minc = std::min({r, g, b});
    v = maxc;
    if (minc == maxc) {
        h = 0.0;
        s = 0.0;
        return;
    }
    const double range = maxc - minc;
    s = range / maxc;
    const double rc = (maxc - r) / range, gc = (maxc - g) / range, bc = (maxc - b) / range;
    if (r == maxc)
        h = bc - gc;
    else if (g == maxc)
        h = 2.0 + rc - bc;
    else
        h = 4.0 + gc - rc;
    h = py_mod1(h / 6.0);
}

void hsv_to_rgb(double h, double s, double v, double& r, double& g, double& b) {
    if (s == 0.0) {
        r = g = b = v;
        return;
    }
    int i = int(h * 6.0);
    const double f = h * 6.0 - i, p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
    i %= 6;
    switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
}

struct IntHsv {
    int h, s, v; // 0-359, 0-100, 0-100 as wwrando keeps them
};

IntHsv int_hsv(int r, int g, int b) {
    double h, s, v;
    rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0, h, s, v);
    return {int(h * 360), int(s * 100), int(v * 100)};
}

} // namespace

void Exchange(Image& img, const Image* mask, uint32_t base, uint32_t chosen, bool ignore_bright) {
    const IntHsv bc = int_hsv(int(base >> 16 & 255), int(base >> 8 & 255), int(base & 255));
    const IntHsv nc = int_hsv(int(chosen >> 16 & 255), int(chosen >> 8 & 255), int(chosen & 255));
    const int s_change = nc.s - bc.s, v_change = nc.v - bc.v;
    const size_t count = size_t(img.width) * img.height;
    for (size_t i = 0; i < count; ++i) {
        if (mask != nullptr && mask->bytes[i] == 0)
            continue;
        uint8_t* p = &img.bytes[i * 4];
        if (ignore_bright && p[0] > 128 && p[1] > 128 && p[2] > 128 && p[3] == 0xFF)
            continue;
        IntHsv c = int_hsv(p[0], p[1], p[2]);
        if (c.s == 0) // grey pixels take the base color's saturation first
            c.s = bc.s;
        const int new_h = ((nc.h % 360) + 360) % 360;
        const int new_s = std::max(0, std::min(100, c.s + s_change));
        const int new_v = std::max(0, std::min(100, c.v + v_change));
        double r, g, b;
        hsv_to_rgb(new_h / 360.0, new_s / 100.0, new_v / 100.0, r, g, b);
        p[0] = uint8_t(int(r * 255));
        p[1] = uint8_t(int(g * 255));
        p[2] = uint8_t(int(b * 255));
    }
}

std::vector<Recolored> Recolor(const Set& set, const Chosen& chosen) {
    struct Pick {
        const Color* color;
        uint32_t rgb;
    };
    std::vector<Pick> picks[2];
    for (int o = 0; o < 2; ++o) {
        if (o == 1 && set.disable_casual)
            continue;
        for (const Color& c : set.outfits[o].colors)
            if (const auto rgb = chosen(o == 1, c.name))
                if (*rgb != c.base)
                    picks[o].push_back({&c, *rgb});
    }
    // Hands, eyes, mouths and eyebrows are one texture for both outfits (the base game swaps only
    // the body): the Hero's colors unless only the Outset ones were changed.
    const int shared = picks[1].size() && picks[0].empty() ? 1 : 0;
    std::vector<Recolored> result;
    for (const Texture& t : set.textures) {
        const std::string& n = t.name;
        const bool body_hero = n == "linktexS3TC", body_casual = n == "linktexbci4", hair = n == "katsuraS3TC";
        const bool hitomi = n == "hitomi", hands = n == "handsS3TC";
        const bool mouth = n.rfind("mouthS3TC.", 0) == 0, brow = n.rfind("mayuh.", 0) == 0;
        const int o = body_hero ? 0 : (body_casual || hair) ? 1 : shared;
        const Outfit& of = set.outfits[o];
        Image img = t.rgba;
        bool any = false;
        for (const Pick& p : picks[o]) {
            const Mask* mask = find_mask(set, o == 1, n, p.color->name);
            const std::string& cn = p.color->name;
            if (body_hero || body_casual) {
                // No mask here is an eye color (the pupils take it) or a fault wwrando stops on.
                if (mask != nullptr) {
                    Exchange(img, &mask->image, p.color->base, p.rgb, false);
                    any = true;
                }
            } else if (hitomi || mouth || hands) {
                const std::string& name = hitomi ? of.hitomi_color : mouth ? of.mouth_color : of.hands_color;
                if (mask != nullptr) {
                    Exchange(img, &mask->image, p.color->base, p.rgb, false);
                    any = true;
                } else if (!name.empty() && cn == name) {
                    Exchange(img, nullptr, p.color->base, p.rgb, hitomi); // the eyes keep their whites
                    any = true;
                }
            } else if (brow) {
                if (set.colored_eyebrows && cn == of.eyebrow_color) {
                    Exchange(img, nullptr, p.color->base, p.rgb, false);
                    any = true;
                }
            } else if (hair) {
                // The Outset clothes' back hair: its top-left 8x8 is the hair color (:498-509).
                if (cn == set.casual_hair_color) {
                    for (uint32_t y = 0; y < std::min(8u, img.height); ++y)
                        for (uint32_t x = 0; x < std::min(8u, img.width); ++x) {
                            uint8_t* px = &img.bytes[(size_t(y) * img.width + x) * 4];
                            px[0] = uint8_t(p.rgb >> 16);
                            px[1] = uint8_t(p.rgb >> 8);
                            px[2] = uint8_t(p.rgb);
                            px[3] = 255;
                        }
                    any = true;
                }
            }
        }
        if (any)
            result.push_back({t.name, t.keys, std::move(img)});
    }
    return result;
}

} // namespace model_colors
