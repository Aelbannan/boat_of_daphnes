// Pictures the randomizer's file-select screens draw, made from the game's own files at run time (nothing is
// shipped): each quest's title logo (TlogoE.arc: the stock one from the disc, wwrando's from the patch it makes), and
// the seed hash's icons (the game's item icons, res/Msg/itemicon.arc).
#pragma once

#include <imgui.h>

#include <cstdint>

#include <filesystem>
#include <vector>
#include <string>

namespace fsart {

struct Image {
    ImTextureID tex = 0;
    int w = 0, h = 0;
};
struct Logo {
    Image main;  // "The Legend of Zelda" (logo_zelda_main.bti)
    Image sub;   // "the wind waker", and for wwrando "Randomizer" under it (the subtitle model's texture)
    bool ready() const { return main.tex != 0 && sub.tex != 0; }
};

// With the disc open and before any overlay replaces TlogoE.arc: keeps the stock archive's bytes.
void CaptureStock();
// Where wwrando's TlogoE.arc is, tried in order, the first that exists wins (a prepared game folder's, a seed's overlay,
// the copy kept from the last seed).
void SetRandoSources(const std::vector<std::filesystem::path>& candidates);
// quest 0: the stock logo; quest 1: wwrando's. Made once (needs a frame to upload the textures); not ready while the
// archive cannot be read.
const Logo& GetLogo(int quest);

// An image of a member of one of the disc's archives ("res/Msg/menures.arc", "ba_a.bti"), made once; 0 until the disc is open.
Image ArcImage(const char* archive, const char* member);

// The file select's own archive (res/Stage/Name/Stage.arc: file_select.arc: file_select.blo, note_02.bti, note_03_1.bti, ...),
// expanded; empty while the disc is not open. Its members, expanded.
const std::vector<uint8_t>& FileSelectArc();
bool FileSelectMember(const char* name, std::vector<uint8_t>& out);
// A picture of that archive the way J2DPicture::setBlackWhite shows it: each of the texture's four channels is picked between
// `black` and `white` (0xRRGGBBAA, the blo's own words) by the texel's value, alpha included (an I4 texture's alpha is its
// intensity). Made once per colour pair. The default pair, 0 and 0xFFFFFFFF, is the texture as it is.
Image Pic(const char* member, uint32_t black, uint32_t white);

// The game's own message font (res/Msg/fontres.arc: rock_24_20_4i_usa.bfn), drawn glyph by glyph as JUTResFont does, so the
// file select's text is in the game's lettering. `size` is the line height in pixels.
bool FontReady(int which = 0);  // which: 0 the message font, 1 the title font of file_select.arc (letters with their outline)
// Draws s with its top left at pos, in colour; returns the width drawn (0 while the font is not ready).
float DrawString(ImDrawList* dl, ImVec2 pos, float size, ImU32 color, const char* s);
// The same, the glyphs' tops in one colour and their bottoms in another (a text box's character and gradient colours).
float DrawStringGradient(ImDrawList* dl, ImVec2 pos, float size, ImU32 top, ImU32 bottom, const char* s, int which = 0, float xratio = 1.f);  // xratio: the glyphs' width over their height
float TextWidth(float size, const char* s, int which = 0, float xratio = 1.f);

// A hash word's icon: an item icon chosen by the word, the same every time. 0 until the disc's icons are readable.
Image HashIcon(const std::string& word);

} // namespace fsart
