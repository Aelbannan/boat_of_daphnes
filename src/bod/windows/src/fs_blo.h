// The file select's own screen layout, read from the disc at run time (res/Stage/Name/Stage.arc: file_select.arc:
// file_select.blo, and main_parts_fileselect.blo): the panes of the game's J2DScreen with their positions, sizes, textures,
// colours and text boxes, drawn the way J2DPane, J2DPicture and J2DTextBox draw them. The randomizer's screens are made of
// those panes at the sizes the game gives them (the message window, the title strip, the record panes, the nameplates), so
// they look like the rest of the file select. Nothing of the disc is shipped.
#pragma once

#include <imgui.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fsblo {

// The game's 640 x 480 screen laid on the window: screen = origin + point * scale.
struct View {
    float s = 1.f, ox = 0.f, oy = 0.f;
    ImVec2 at(float x, float y) const { return ImVec2(ox + x * s, oy + y * s); }
};

// A four-letter pane name as the game writes it: 'nt00'.
constexpr uint32_t Tag(const char (&t)[5]) {
    return uint32_t(uint8_t(t[0])) << 24 | uint32_t(uint8_t(t[1])) << 16 | uint32_t(uint8_t(t[2])) << 8 | uint8_t(t[3]);
}

struct Node {
    uint32_t tag = 0;
    char kind[5] = {0, 0, 0, 0, 0};  // PAN1, PIC1, TBX1
    float x = 0, y = 0, w = 0, h = 0;  // in the parent's pixels
    float rot = 0;                     // degrees, about the base position
    int base = 0;                      // J2DBasePosition
    uint8_t alpha = 255;
    bool inherit = true;
    bool visible = true;
    // PIC1
    std::string tex;
    int flag = 0;  // J2DPicture's mirror and quarter-turn bits
    uint32_t black = 0, white = 0xFFFFFFFFu;
    // TBX1
    uint32_t char_color = 0xFFFFFFFFu, grad_color = 0xFFFFFFFFu;
    int font = 0;              // 0 the message font, 1 the title font (the 'ia4' one: letters with their outline)
    int hbind = 0, vbind = 0;  // 0 centre, 1 right/bottom, 2 left/top
    uint32_t text_black = 0, text_white = 0xFFFFFFFFu;  // J2DTextBox's black and white: the colours the glyphs' intensity picks between
    int font_x = 24, font_y = 24, line_space = 24, char_space = 0;
    std::string text;
    int parent = -1;
    std::vector<int> kids;
};

struct Screen {
    std::vector<Node> nodes;
    bool ready() const { return !nodes.empty(); }
    int find(uint32_t tag) const;
    // A pane's top left in the screen's own pixels, parents' positions added.
    void origin(int index, float* x, float* y) const;
};

// The two screens of the file select, once the disc is open (empty until then).
const Screen& FileSelect();    // file_select.blo
const Screen& MainParts();     // main_parts_fileselect.blo (the corner prompts, drawn by the game's own code)

// How a subtree is drawn: moved, faded, and with some panes left out or their text replaced.
struct Pose {
    float dx = 0, dy = 0;       // moves the subtree's root from where the blo puts it
    float alpha = 1.f;          // the whole subtree's, on top of the panes' own
    std::map<uint32_t, uint8_t> alpha_of;  // a pane's own alpha (0..255) in place of the blo's, which is where it starts fading in from
    std::set<uint32_t> skip;    // panes (and what is under them) not drawn
    std::map<uint32_t, std::string> text;  // text boxes' words in place of the blo's
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> colors;  // a picture's black and white in place of the blo's
};

// Draws the pane `tag` and everything under it.
void Draw(ImDrawList* dl, const View& view, const Screen& screen, uint32_t tag, const Pose& pose);
// A text box's string on its own (the box's size, binding, colours), at a place: for words the blo does not have.
void PutText(ImDrawList* dl, const View& view, const Node& box, float x, float y, const std::string& text, float alpha, ImU32 top = 0,
              ImU32 bottom = 0);  // top and bottom, when not 0, are the glyphs' colours (alpha in them) in place of the box's
// The width of text in a box's font, in the screen's pixels.
float TextWidth(const Node& box, const std::string& text);

// A window the way the game builds its message windows: torn strips along the four sides (16 pixels thick) and the paper
// between, any size. The colours are the blo's black and white for it (nt00: 0xFFC83C00, 0xFFE696FF); alpha 0..1.
void Window(ImDrawList* dl, const View& view, float x, float y, float w, float h, uint32_t black, uint32_t white, float alpha,
            float thickness = 16.f);

// The same with the shadow the game puts under its message window (offset 11, 13; black at alpha 50).
void WindowWithShadow(ImDrawList* dl, const View& view, float x, float y, float w, float h, uint32_t black, uint32_t white, float alpha);
// One picture of the file select archive in a rectangle (J2DPicture's flags and black/white).
void Picture(ImDrawList* dl, const View& view, const char* tex, float x, float y, float w, float h, int flag, uint32_t black, uint32_t white,
             float alpha);

} // namespace fsblo
