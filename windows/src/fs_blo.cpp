// See fs_blo.h. J2DScreen::makeHierarchyPanes, J2DPane::makePaneStream, J2DPicture's and J2DTextBox's stream constructors
// (tww src/JSystem/J2DGraph) are the model for the reading; J2DPicture::drawSelf and J2DPrint for the drawing.
#ifdef rename
#undef rename  // bw_posix_compat.h's, which <filesystem> must not see
#endif

#include "fs_blo.h"

#include "fs_art.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fsblo {
namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
int16_t be16s(const uint8_t* p) { return static_cast<int16_t>(uint16_t(p[0]) << 8 | p[1]); }
uint16_t be16(const uint8_t* p) { return uint16_t(uint16_t(p[0]) << 8 | p[1]); }

// A JUTResReference: a type byte, the length of the name, the name.
std::string res_name(const std::vector<uint8_t>& d, size_t& at) {
    if (at + 2 > d.size())
        return {};
    const size_t n = d[at + 1];
    std::string name;
    for (size_t i = 0; i < n && at + 2 + i < d.size() && d[at + 2 + i] != 0; ++i)
        name += static_cast<char>(d[at + 2 + i]);
    at += 2 + n;
    return name;
}

bool parse(const std::vector<uint8_t>& d, Screen& out) {
    if (d.size() < 32 || std::memcmp(d.data(), "SCRNblo1", 8) != 0)
        return false;
    out.nodes.clear();
    std::vector<int> stack;  // the open parents
    int last = -1;           // the pane made last: the next BGN1 opens it
    size_t at = 32;
    while (at + 8 <= d.size()) {
        const uint32_t magic = be32(&d[at]);
        const size_t size = be32(&d[at + 4]);
        if (size < 8 || at + size > d.size())
            break;
        const char* m = reinterpret_cast<const char*>(&d[at]);
        if (std::memcmp(m, "BGN1", 4) == 0) {
            if (last >= 0)
                stack.push_back(last);
        } else if (std::memcmp(m, "END1", 4) == 0) {
            if (!stack.empty())
                stack.pop_back();
        } else if (std::memcmp(m, "PAN1", 4) == 0 || std::memcmp(m, "PIC1", 4) == 0 || std::memcmp(m, "TBX1", 4) == 0) {
            (void)magic;
            Node n;
            std::memcpy(n.kind, m, 4);
            size_t o = at + 8;
            int left = d[o];  // J2DPane::makePaneStream
            n.visible = d[o + 1] != 0;
            n.tag = be32(&d[o + 4]);
            n.x = be16s(&d[o + 8]);
            n.y = be16s(&d[o + 10]);
            n.w = be16s(&d[o + 12]);
            n.h = be16s(&d[o + 14]);
            o += 16;
            left -= 6;
            if (left > 0) { n.rot = float(be16(&d[o])); o += 2; --left; }
            if (left > 0) { n.base = d[o]; o += 1; --left; }
            if (left > 0) { n.alpha = d[o]; o += 1; --left; }
            if (left > 0) { n.inherit = d[o] != 0; o += 1; --left; }
            o = (o + 3) & ~size_t(3);
            if (std::memcmp(m, "PIC1", 4) == 0) {
                int num = d[o++];
                n.tex = res_name(d, o);  // the TIMG
                res_name(d, o);          // the TLUT
                o += 1;                  // the binding
                num -= 3;
                if (num > 0) { n.flag = d[o++]; --num; }
                if (num > 0) { ++o; --num; }
                if (num > 0) { n.black = be32(&d[o]); o += 4; --num; }
                if (num > 0) { n.white = be32(&d[o]); o += 4; --num; }
            } else if (std::memcmp(m, "TBX1", 4) == 0) {
                int count = d[o++];
                n.font = res_name(d, o).find("ia4") != std::string::npos ? 1 : 0;  // the font
                n.char_color = be32(&d[o]);
                n.grad_color = be32(&d[o + 4]);
                const int bind = d[o + 8];
                n.hbind = (bind >> 2) & 3;
                n.vbind = bind & 3;
                n.char_space = be16s(&d[o + 9]);
                n.line_space = be16s(&d[o + 11]);
                n.font_x = be16(&d[o + 13]);
                n.font_y = be16(&d[o + 15]);
                const size_t len = be16(&d[o + 17]);
                if (o + 19 + len <= d.size())
                    n.text.assign(reinterpret_cast<const char*>(&d[o + 19]), len);
                size_t e = o + 19 + len;
                count -= 10;
                if (count > 0) { ++e; --count; }  // whether it is joined to its parent
                if (count > 0 && e + 4 <= d.size()) { n.text_black = be32(&d[e]); e += 4; --count; }
                if (count > 0 && e + 4 <= d.size()) { n.text_white = be32(&d[e]); e += 4; --count; }
            }
            n.parent = stack.empty() ? -1 : stack.back();
            const int index = static_cast<int>(out.nodes.size());
            if (n.parent >= 0)
                out.nodes[size_t(n.parent)].kids.push_back(index);
            out.nodes.push_back(std::move(n));
            last = index;
        }
        at += size;
    }
    return !out.nodes.empty();
}

Screen g_screens[2];
bool g_tried[2];

const Screen& load(int which, const char* member) {
    if (!g_tried[which]) {
        std::vector<uint8_t> blo;
        if (fsart::FileSelectArc().empty())
            return g_screens[which];  // the disc is not open yet: tried again
        g_tried[which] = true;
        if (fsart::FileSelectMember(member, blo) && parse(blo, g_screens[which]))
            std::fprintf(stderr, "[fileselect] %s: %zu panes\n", member, g_screens[which].nodes.size());
        else
            std::fprintf(stderr, "[fileselect] %s could not be read\n", member);
    }
    return g_screens[which];
}

ImU32 color(uint32_t rgba, float alpha) {
    const float a = float(rgba & 0xFF) * alpha;
    return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, int(std::clamp(a, 0.f, 255.f) + 0.5f));
}

// J2DPicture's mirror flags (1: top and bottom, 2: left and right) and its quarter turn (4), then the pane's own rotation
// about its base position, drawn as one quad.
void picture(ImDrawList* dl, const View& view, const Node& n, float x, float y, uint32_t black, uint32_t white, float alpha) {
    const fsart::Image im = fsart::Pic(n.tex.c_str(), black, white);
    if (im.tex == 0 || alpha <= 0.f)
        return;
    ImVec2 uv[4] = {ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1)};  // top left, top right, bottom right, bottom left
    if (n.flag & 4) {
        uv[0] = ImVec2(0, 0); uv[1] = ImVec2(0, 1); uv[2] = ImVec2(1, 1); uv[3] = ImVec2(1, 0);
    }
    if (n.flag & 1) { std::swap(uv[0], uv[3]); std::swap(uv[1], uv[2]); }
    if (n.flag & 2) { std::swap(uv[0], uv[1]); std::swap(uv[3], uv[2]); }
    ImVec2 c[4] = {ImVec2(x, y), ImVec2(x + n.w, y), ImVec2(x + n.w, y + n.h), ImVec2(x, y + n.h)};
    if (n.rot != 0.f) {
        const float px = x + ((n.base % 3) == 0 ? 0.f : (n.base % 3) == 1 ? n.w * 0.5f : n.w);
        const float py = y + ((n.base / 3) == 0 ? 0.f : (n.base / 3) == 1 ? n.h * 0.5f : n.h);
        const float r = n.rot * 3.14159265f / 180.f, cs = std::cos(r), sn = std::sin(r);
        for (ImVec2& p : c) {
            const float dx = p.x - px, dy = p.y - py;
            p = ImVec2(px + dx * cs - dy * sn, py + dx * sn + dy * cs);
        }
    }
    for (ImVec2& p : c)
        p = view.at(p.x, p.y);
    dl->AddImageQuad(im.tex, c[0], c[1], c[2], c[3], uv[0], uv[1], uv[2], uv[3], IM_COL32(255, 255, 255, int(std::clamp(alpha, 0.f, 1.f) * 255.f + 0.5f)));
}

// The lines of a text box and where each starts, as J2DPrint lays them out: the box's binding places the block, each line
// is placed in it.
void layout(const Node& box, const std::string& text, float x, float y, std::vector<std::pair<std::string, ImVec2>>& lines, float* width) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    const float step = float(box.line_space);
    const float block = float(box.font_y) + step * float(parts.size() - 1);
    float top = y;
    if (box.vbind == 0)
        top = y + (box.h - block) * 0.5f;
    else if (box.vbind == 1)
        top = y + box.h - block;
    float widest = 0.f;
    for (size_t i = 0; i < parts.size(); ++i) {
        const float w = fsart::TextWidth(float(box.font_y), parts[i].c_str(), box.font, float(box.font_x) / float(box.font_y));
        widest = std::max(widest, w);
        float left = x;
        if (box.hbind == 0)
            left = x + (box.w - w) * 0.5f;
        else if (box.hbind == 1)
            left = x + box.w - w;
        lines.emplace_back(parts[i], ImVec2(left, top + step * float(i)));
    }
    if (width != nullptr)
        *width = widest;
}

void draw_text(ImDrawList* dl, const View& view, const Node& box, float x, float y, const std::string& text, float alpha, ImU32 top = 0,
               ImU32 bottom = 0) {
    if (text.empty())
        return;
    std::vector<std::pair<std::string, ImVec2>> lines;
    layout(box, text, x, y, lines, nullptr);
    const float a = std::clamp(alpha, 0.f, 1.f);
    // The message font's glyphs are intensity only: the box's white is their colour (the file select's text is dark grey), times
    // the character colours. The title font's glyphs carry their own dark outline.
    auto tint = [&](uint32_t c) {
        if (box.font != 0)
            return color(c, a);
        const uint32_t w = box.text_white;
        const uint32_t r = ((c >> 24) & 0xFF) * ((w >> 24) & 0xFF) / 255, g = ((c >> 16) & 0xFF) * ((w >> 16) & 0xFF) / 255,
                       b = ((c >> 8) & 0xFF) * ((w >> 8) & 0xFF) / 255;
        return color(r << 24 | g << 16 | b << 8 | (c & 0xFF), a);
    };
    for (const auto& [line, at] : lines) {
        const ImVec2 p = view.at(at.x, at.y);
        fsart::DrawStringGradient(dl, p, float(box.font_y) * view.s, top != 0u ? top : tint(box.char_color),
                                  top != 0u ? bottom : tint(box.grad_color), line.c_str(), box.font, float(box.font_x) / float(box.font_y));
    }
}

void draw_node(ImDrawList* dl, const View& view, const Screen& screen, int index, float px, float py, float parent_alpha, const Pose& pose,
               bool root) {
    const Node& n = screen.nodes[size_t(index)];
    if (!n.visible || pose.skip.count(n.tag) != 0)
        return;
    const float x = px + n.x + (root ? pose.dx : 0.f), y = py + n.y + (root ? pose.dy : 0.f);
    uint8_t own = n.alpha;
    if (const auto it = pose.alpha_of.find(n.tag); it != pose.alpha_of.end())
        own = it->second;
    const float alpha = (n.inherit ? parent_alpha : 1.f) * float(own) / 255.f;
    if (std::memcmp(n.kind, "PIC1", 4) == 0) {
        uint32_t black = n.black, white = n.white;
        if (const auto it = pose.colors.find(n.tag); it != pose.colors.end()) {
            black = it->second.first;
            white = it->second.second;
        }
        picture(dl, view, n, x, y, black, white, alpha * pose.alpha);
    } else if (std::memcmp(n.kind, "TBX1", 4) == 0) {
        const auto it = pose.text.find(n.tag);
        draw_text(dl, view, n, x, y, it != pose.text.end() ? it->second : n.text, alpha * pose.alpha);
    }
    for (const int kid : n.kids)
        draw_node(dl, view, screen, kid, x, y, alpha, pose, false);
}

} // namespace

const Screen& FileSelect() { return load(0, "file_select.blo"); }
const Screen& MainParts() { return load(1, "main_parts_fileselect.blo"); }

int Screen::find(uint32_t tag) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].tag == tag)
            return static_cast<int>(i);
    return -1;
}

void Screen::origin(int index, float* x, float* y) const {
    float ox = 0.f, oy = 0.f;
    for (int i = index; i >= 0; i = nodes[size_t(i)].parent) {
        ox += nodes[size_t(i)].x;
        oy += nodes[size_t(i)].y;
    }
    *x = ox;
    *y = oy;
}

void Draw(ImDrawList* dl, const View& view, const Screen& screen, uint32_t tag, const Pose& pose) {
    const int index = screen.find(tag);
    if (index < 0)
        return;
    float x = 0.f, y = 0.f;
    const int parent = screen.nodes[size_t(index)].parent;
    if (parent >= 0)
        screen.origin(parent, &x, &y);
    draw_node(dl, view, screen, index, x, y, 1.f, pose, true);
}

void PutText(ImDrawList* dl, const View& view, const Node& box, float x, float y, const std::string& text, float alpha, ImU32 top,
              ImU32 bottom) {
    draw_text(dl, view, box, x, y, text, alpha, top, bottom);
}

float TextWidth(const Node& box, const std::string& text) {
    std::vector<std::pair<std::string, ImVec2>> lines;
    float width = 0.f;
    layout(box, text, 0.f, 0.f, lines, &width);
    return width;
}

// A picture tiled at the texture's own density (one texel a unit of the 640 x 480 screen), mirrored from tile to tile as its
// MirroredRepeat wrap says, the last tile cut and not scaled. `flag` is J2DPicture's (1 and 2 mirror the pane, 4 turns the
// texture a quarter, so its long side runs down the pane).
static void tiled(ImDrawList* dl, const View& view, const fsart::Image& im, float x, float y, float w, float h, int flag, float alpha) {
    if (im.tex == 0 || w <= 0.f || h <= 0.f || alpha <= 0.f)
        return;
    const bool tumble = (flag & 4) != 0;
    const float tx = float(tumble ? im.h : im.w), ty = float(tumble ? im.w : im.h);  // a tile's size in units, along x and down
    const ImU32 white = IM_COL32(255, 255, 255, int(std::clamp(alpha, 0.f, 1.f) * 255.f + 0.5f));
    for (int iy = 0; float(iy) * ty < h - 0.001f; ++iy) {
        for (int ix = 0; float(ix) * tx < w - 0.001f; ++ix) {
            const float cx = std::min(tx, w - float(ix) * tx), cy = std::min(ty, h - float(iy) * ty);
            const float fx = cx / tx, fy = cy / ty;
            float a0 = (ix & 1) ? 1.f : 0.f, a1 = (ix & 1) ? 1.f - fx : fx;  // along the pane's x, 0..1 of the tile
            float b0 = (iy & 1) ? 1.f : 0.f, b1 = (iy & 1) ? 1.f - fy : fy;
            if (flag & 2) { a0 = 1.f - a0; a1 = 1.f - a1; }
            if (flag & 1) { b0 = 1.f - b0; b1 = 1.f - b1; }
            auto uv = [&](float u, float v) { return tumble ? ImVec2(v, u) : ImVec2(u, v); };
            const ImVec2 p0 = view.at(x + float(ix) * tx, y + float(iy) * ty), p1 = view.at(x + float(ix) * tx + cx, y + float(iy) * ty + cy);
            dl->AddImageQuad(im.tex, p0, ImVec2(p1.x, p0.y), p1, ImVec2(p0.x, p1.y), uv(a0, b0), uv(a1, b0), uv(a1, b1), uv(a0, b1), white);
        }
    }
}

void Window(ImDrawList* dl, const View& view, float x, float y, float w, float h, uint32_t black, uint32_t white, float alpha, float t) {
    // nt00's construction (the left strip, the bottom and top strips between, the right strip, the paper in the middle), the
    // strips and the paper tiled at their own density instead of one texture stretched over the pane.
    const fsart::Image paper = fsart::Pic("note_02.bti", black, white), strip = fsart::Pic("note_03_1.bti", black, white);
    tiled(dl, view, paper, x + t, y + t, w - 2 * t, h - 2 * t, 0, alpha);
    tiled(dl, view, strip, x + t, y, w - 2 * t, t, 0, alpha);
    tiled(dl, view, strip, x + t, y + h - t, w - 2 * t, t, 1, alpha);
    tiled(dl, view, strip, x, y, t, h, 5, alpha);
    tiled(dl, view, strip, x + w - t, y, t, h, 6, alpha);
}

void WindowWithShadow(ImDrawList* dl, const View& view, float x, float y, float w, float h, uint32_t black, uint32_t white, float alpha) {
    Window(dl, view, x + 11.f, y + 13.f, w, h, 0x00000000u, 0x000000FFu, alpha * 50.f / 255.f);  // the shadow pane (ntk0): black, alpha 50
    Window(dl, view, x, y, w, h, black, white, alpha);
}

void Picture(ImDrawList* dl, const View& view, const char* tex, float x, float y, float w, float h, int flag, uint32_t black, uint32_t white,
             float alpha) {
    Node n;
    n.tex = tex;
    n.flag = flag;
    n.w = w;
    n.h = h;
    picture(dl, view, n, x, y, black, white, alpha);
}

} // namespace fsblo
