// The game's own message font, drawn glyph by glyph as JUTResFont does (see fs_art.h), and images out of the disc's archives.
#ifdef rename
#undef rename  // bw_posix_compat.h's, which <filesystem> must not see
#endif

#include "fs_art.h"

#include "gx_texture.h"
#include "rarc.h"

#include <aurora/imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include "gxruntime/dvd.h"
}

namespace fsart {
namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

std::map<std::string, std::vector<uint8_t>> g_archives;  // expanded archives by disc path
std::map<std::string, Image> g_arc_images;

// An archive of the disc, expanded and kept; empty while the disc is not open.
const std::vector<uint8_t>& archive(const char* path) {
    static const std::vector<uint8_t> none;
    auto it = g_archives.find(path);
    if (it != g_archives.end())
        return it->second;
    u32 size = 0;
    u8* data = dvd_read_image_file(path, &size);
    if (data == nullptr)
        return none;
    std::vector<uint8_t> bytes(data, data + size);
    std::free(data);
    rarc::Yaz0Expand(bytes);
    return g_archives.emplace(path, std::move(bytes)).first->second;
}

struct WidBlock {
    uint16_t start = 0, end = 0;
    size_t at = 0;
};
struct MapBlock {
    uint16_t method = 0, start = 0, end = 0;
    std::vector<uint16_t> table;
};
struct GlyBlock {
    uint16_t start = 0, end = 0, cell_w = 0, cell_h = 0, format = 0, rows = 0, cols = 0, tex_w = 0, tex_h = 0;
    uint32_t tex_size = 0;
    size_t data = 0;
    std::vector<Image> pages;
};
struct Font {
    bool tried = false, ok = false;
    std::vector<uint8_t> bytes;
    int ascent = 0, descent = 0, width = 0, default_code = 0;
    std::vector<WidBlock> wid;
    std::vector<MapBlock> map;
    std::vector<GlyBlock> gly;
};
// The two fonts of the file select: the message font (intensity glyphs, in the Msg archive: the buttons' and the messages'
// text) and the title font of file_select.arc (intensity and alpha: the letters come with their dark outline).
Font g_fonts[2];

void load_font(int which) {
    Font& f = g_fonts[which];
    if (which == 0) {
        const std::vector<uint8_t>& arc = archive("res/Msg/fontres.arc");
        if (arc.empty())
            return;  // the disc is not open: again next frame
        f.tried = true;
        if (!rarc::Extract(arc, "rock_24_20_4i_usa.bfn", f.bytes) || f.bytes.size() < 0x40)
            return;
        rarc::Yaz0Expand(f.bytes);
    } else {
        if (FileSelectArc().empty())
            return;
        f.tried = true;
        if (!FileSelectMember("rock_24_20_ia4_e.bfn", f.bytes) || f.bytes.size() < 0x40)
            return;
    }
    const std::vector<uint8_t>& b = f.bytes;
    if (std::memcmp(b.data(), "FONTbfn1", 8) != 0)
        return;
    const uint32_t blocks = be32(&b[0x0C]);
    size_t at = 0x20;
    for (uint32_t i = 0; i < blocks && at + 8 <= b.size(); ++i) {
        const uint32_t size = be32(&b[at + 4]);
        if (size < 8 || at + size > b.size())
            return;
        if (std::memcmp(&b[at], "INF1", 4) == 0) {
            f.ascent = be16(&b[at + 0x0A]);
            f.descent = be16(&b[at + 0x0C]);
            f.width = be16(&b[at + 0x0E]);
            f.default_code = be16(&b[at + 0x12]);
        } else if (std::memcmp(&b[at], "WID1", 4) == 0) {
            WidBlock w;
            w.start = be16(&b[at + 8]);
            w.end = be16(&b[at + 0x0A]);
            w.at = at + 0x0C;
            f.wid.push_back(w);
        } else if (std::memcmp(&b[at], "MAP1", 4) == 0) {
            MapBlock m;
            m.method = be16(&b[at + 8]);
            m.start = be16(&b[at + 0x0A]);
            m.end = be16(&b[at + 0x0C]);
            if (m.method == 2)
                for (uint32_t c = m.start; c <= m.end && at + 0x10 + (size_t(c) - m.start + 1) * 2 <= at + size; ++c)
                    m.table.push_back(be16(&b[at + 0x10 + (size_t(c) - m.start) * 2]));
            f.map.push_back(m);
        } else if (std::memcmp(&b[at], "GLY1", 4) == 0) {
            GlyBlock g;
            g.start = be16(&b[at + 8]);
            g.end = be16(&b[at + 0x0A]);
            g.cell_w = be16(&b[at + 0x0C]);
            g.cell_h = be16(&b[at + 0x0E]);
            g.tex_size = be32(&b[at + 0x10]);
            g.format = be16(&b[at + 0x14]);
            g.rows = be16(&b[at + 0x16]);
            g.cols = be16(&b[at + 0x18]);
            g.tex_w = be16(&b[at + 0x1A]);
            g.tex_h = be16(&b[at + 0x1C]);
            g.data = at + 0x20;
            f.gly.push_back(g);
        }
        at += size;
    }
    f.ok = !f.gly.empty() && f.ascent + f.descent > 0;
    std::fprintf(stderr, "[fileselect] the game's font %d: %s (ascent %d, descent %d, width %d, %zu glyph blocks, %zu map blocks)\n",
                 which, f.ok ? "ok" : "unreadable", f.ascent, f.descent, f.width, f.gly.size(), f.map.size());
}

// JUTResFont::getFontCode
int font_code(const Font& font, int c) {
    int ret = font.default_code;
    for (const MapBlock& m : font.map)
        if (m.start <= c && c <= m.end) {
            if (m.method == 0)
                ret = c - m.start;
            else if (m.method == 2 && size_t(c - m.start) < m.table.size())
                ret = m.table[size_t(c - m.start)];
            break;
        }
    return ret;
}

// JUTResFont::getWidthEntry: the glyph's left offset and its advance
void font_width(const Font& font, int code, int& left, int& adv) {
    left = 0;
    adv = font.width;
    for (const WidBlock& w : font.wid)
        if (w.start <= code && code <= w.end) {
            const size_t at = w.at + size_t(code - w.start) * 2;
            if (at + 1 < font.bytes.size()) {
                left = font.bytes[at];
                adv = font.bytes[at + 1];
            }
            break;
        }
}

// The glyph's page image and the origin of its cell in the page (JUTResFont::loadImage).
bool glyph(Font& font, int code, Image& page, int& cx, int& cy, const GlyBlock*& block) {
    for (GlyBlock& g : font.gly) {
        if (g.start > code || code > g.end)
            continue;
        const int local = code - g.start;
        const int per_page = g.rows * g.cols;
        if (per_page <= 0)
            return false;
        const int page_index = local / per_page, in_page = local % per_page;
        cx = (in_page % g.rows) * g.cell_w;
        cy = (in_page / g.rows) * g.cell_h;
        if (g.pages.size() <= size_t(page_index))
            g.pages.resize(size_t(page_index) + 1);
        Image& image = g.pages[size_t(page_index)];
        if (image.tex == 0) {
            gx_texture::Texture t;
            t.format = static_cast<uint8_t>(g.format);
            t.width = g.tex_w;
            t.height = g.tex_h;
            const size_t from = g.data + size_t(page_index) * g.tex_size;
            if (from + g.tex_size > font.bytes.size())
                return false;
            t.image.assign(font.bytes.begin() + long(from), font.bytes.begin() + long(from + g.tex_size));
            std::vector<uint8_t> rgba;
            std::string error;
            if (!gx_texture::DecodeRgba(t, rgba, &error)) {
                std::fprintf(stderr, "[fileselect] font page: %s\n", error.c_str());
                return false;
            }
            // An intensity texture is the glyph's coverage: white, the intensity as alpha. An intensity and alpha one keeps its
            // intensity (the outline is dark) and its alpha.
            if (g.format == 0 || g.format == 1) {
                for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
                    const uint8_t a = rgba[i];
                    rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;
                    rgba[i + 3] = a;
                }
            }
            image.tex = aurora_imgui_add_texture(g.tex_w, g.tex_h, rgba.data());
            image.w = g.tex_w;
            image.h = g.tex_h;
        }
        page = image;
        block = &g;
        return page.tex != 0;
    }
    return false;
}

} // namespace

bool FontReady(int which) {
    Font& f = g_fonts[which != 0 ? 1 : 0];
    if (!f.tried)
        load_font(which != 0 ? 1 : 0);
    return f.ok;
}

float TextWidth(float size, const char* s, int which, float xratio) {
    if (!FontReady(which))
        return 0.f;
    const Font& font = g_fonts[which != 0 ? 1 : 0];
    const float f = size / float(font.ascent + font.descent) * xratio;
    float width = 0.f;
    bool first = true;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p != 0; ++p) {
        int left, adv;
        font_width(font, font_code(font, *p), left, adv);
        width += float(first ? left + adv : adv) * f;
        first = false;
    }
    return width;
}

float DrawStringGradient(ImDrawList* dl, ImVec2 pos, float size, ImU32 top, ImU32 bottom, const char* s, int which, float xratio) {
    if (!FontReady(which))
        return 0.f;
    Font& font = g_fonts[which != 0 ? 1 : 0];
    // JUTResFont::drawChar_scale with the line `size` tall: each glyph's cell is drawn on a quad of its own size, the pen
    // moves by the glyph's advance (the first glyph is not pulled back by its left offset, the others are).
    const float k = size / float(font.ascent + font.descent);
    const float kx = k * xratio;  // the box's font size is wider or narrower than it is tall
    float pen = pos.x;
    bool first = true;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p != 0; ++p) {
        const int code = font_code(font, *p);
        int left, adv;
        font_width(font, code, left, adv);
        Image page;
        int cx = 0, cy = 0;
        const GlyBlock* block = nullptr;
        if (!glyph(font, code, page, cx, cy, block))
            continue;
        const float x1 = first ? pen : pen - float(left) * kx;
        const ImVec2 uv0(float(cx) / float(page.w), float(cy) / float(page.h));
        const ImVec2 uv1(float(cx + block->cell_w) / float(page.w), float(cy + block->cell_h) / float(page.h));
        const ImVec2 p0(x1, pos.y), p1(x1 + float(block->cell_w) * kx, pos.y + float(block->cell_h) * k);
        if (top == bottom) {
            dl->AddImage(page.tex, p0, p1, uv0, uv1, top);
        } else {  // JUTResFont's gradient: the glyph's top takes one colour and its bottom the other
            dl->PushTextureID(page.tex);
            dl->PrimReserve(6, 4);
            const ImDrawIdx v = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
            dl->PrimWriteIdx(v); dl->PrimWriteIdx(static_cast<ImDrawIdx>(v + 1)); dl->PrimWriteIdx(static_cast<ImDrawIdx>(v + 2));
            dl->PrimWriteIdx(v); dl->PrimWriteIdx(static_cast<ImDrawIdx>(v + 2)); dl->PrimWriteIdx(static_cast<ImDrawIdx>(v + 3));
            dl->PrimWriteVtx(p0, uv0, top);
            dl->PrimWriteVtx(ImVec2(p1.x, p0.y), ImVec2(uv1.x, uv0.y), top);
            dl->PrimWriteVtx(p1, uv1, bottom);
            dl->PrimWriteVtx(ImVec2(p0.x, p1.y), ImVec2(uv0.x, uv1.y), bottom);
            dl->PopTextureID();
        }
        pen += float(first ? left + adv : adv) * kx;
        first = false;
    }
    return pen - pos.x;
}

float DrawString(ImDrawList* dl, ImVec2 pos, float size, ImU32 color, const char* s) {
    return DrawStringGradient(dl, pos, size, color, color, s, 0);
}

const std::vector<uint8_t>& FileSelectArc() {
    static std::vector<uint8_t> inner;
    static bool tried;
    if (!tried) {
        const std::vector<uint8_t>& outer = archive("res/Stage/Name/Stage.arc");
        if (outer.empty())
            return inner;  // the disc is not open yet
        tried = true;
        if (rarc::Extract(outer, "file_select.arc", inner))
            rarc::Yaz0Expand(inner);
        else
            std::fprintf(stderr, "[fileselect] Stage/Name/Stage.arc has no file_select.arc\n");
    }
    return inner;
}

bool FileSelectMember(const char* name, std::vector<uint8_t>& out) {
    const std::vector<uint8_t>& inner = FileSelectArc();
    if (inner.empty() || !rarc::Extract(inner, name, out))
        return false;
    rarc::Yaz0Expand(out);
    return !out.empty();
}

namespace {
// A texture's picture between black and white (Pic, Tinted), made once per key.
Image tinted(const std::string& key, const char* member, std::vector<uint8_t>& bti, bool found, uint32_t black, uint32_t white);
}

Image Pic(const char* member, uint32_t black, uint32_t white) {
    if (FileSelectArc().empty())
        return {};  // the disc is not open yet
    char key[160];
    std::snprintf(key, sizeof key, "pic|%s|%08X|%08X", member, black, white);
    if (auto it = g_arc_images.find(key); it != g_arc_images.end())
        return it->second;
    std::vector<uint8_t> bti;
    const bool found = FileSelectMember(member, bti);
    return tinted(key, member, bti, found, black, white);
}

Image Tinted(const char* archive_path, const char* member, uint32_t black, uint32_t white) {
    char key[200];
    std::snprintf(key, sizeof key, "tint|%s|%s|%08X|%08X", archive_path, member, black, white);
    if (auto it = g_arc_images.find(key); it != g_arc_images.end())
        return it->second;
    const std::vector<uint8_t>& arc = archive(archive_path);
    if (arc.empty())
        return {};  // the disc is not open yet
    std::vector<uint8_t> bti;
    const bool found = rarc::Extract(arc, member, bti);
    if (found)
        rarc::Yaz0Expand(bti);
    return tinted(key, member, bti, found, black, white);
}

namespace {
Image tinted(const std::string& key, const char* member, std::vector<uint8_t>& bti, bool found, uint32_t black, uint32_t white) {
    Image image;
    if (found) {
        gx_texture::Texture t;
        std::string error;
        std::vector<uint8_t> rgba;
        if (gx_texture::Parse(bti.data(), bti.size(), 0, t, &error) && gx_texture::DecodeRgba(t, rgba, &error)) {
            // J2DPicture::setBlackWhite: the texture's value picks each of the four channels between black and white
            // (TEV: black * (1 - texel) + white * texel). The default pair (0, 0xFFFFFFFF) leaves the texture as it is.
            // A texture whose header says it is opaque (note_02, the paper) has no alpha of its own: TEV takes 1.0 for it
            // (J2DPicture::setTevMode, getTransparency).
            if (bti.size() > 1 && bti[1] == 0)
                for (size_t i = 0; i + 3 < rgba.size(); i += 4)
                    rgba[i + 3] = 255;
            if (black != 0u || white != 0xFFFFFFFFu) {
                for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
                    for (int c = 0; c < 4; ++c) {
                        const int b = int((black >> (24 - 8 * c)) & 0xFF), w = int((white >> (24 - 8 * c)) & 0xFF);
                        rgba[i + size_t(c)] = static_cast<uint8_t>(b + ((w - b) * int(rgba[i + size_t(c)]) + 127) / 255);
                    }
                }
            }
            image.tex = aurora_imgui_add_texture(t.width, t.height, rgba.data());
            image.w = static_cast<int>(t.width);
            image.h = static_cast<int>(t.height);
        } else {
            std::fprintf(stderr, "[fileselect] %s: %s\n", member, error.c_str());
        }
    }
    g_arc_images[key] = image;
    return image;
}
}  // namespace

Image ArcImage(const char* archive_path, const char* member_name) {
    const std::string key = std::string(archive_path) + "|" + member_name;
    if (auto it = g_arc_images.find(key); it != g_arc_images.end())
        return it->second;
    const std::vector<uint8_t>& arc = archive(archive_path);
    if (arc.empty())
        return {};
    std::vector<uint8_t> bti;
    Image image;
    if (rarc::Extract(arc, member_name, bti)) {
        rarc::Yaz0Expand(bti);
        gx_texture::Texture t;
        std::string error;
        std::vector<uint8_t> rgba;
        if (gx_texture::Parse(bti.data(), bti.size(), 0, t, &error) && gx_texture::DecodeRgba(t, rgba, &error)) {
            image.tex = aurora_imgui_add_texture(t.width, t.height, rgba.data());
            image.w = static_cast<int>(t.width);
            image.h = static_cast<int>(t.height);
        } else {
            std::fprintf(stderr, "[fileselect] %s: %s\n", member_name, error.c_str());
        }
    }
    g_arc_images[key] = image;
    return image;
}

} // namespace fsart
