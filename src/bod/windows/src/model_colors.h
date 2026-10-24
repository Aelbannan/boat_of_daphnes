// The colors of a player model: what the Cosmetics Editor shows for it and
// what the recolor needs, built in memory from the model's own files the way
// wwrando's customizer does (customizer.py: get_model_metadata,
// change_player_custom_colors; wwrando is MIT, Copyright (c) 2018 LagoLunatic).
//
// A model (wwrando format) is a folder or archive with Link.arc, metadata.txt
// (custom color names and base colors per outfit, presets) and color_masks\
// (one picture per color; pure red FF0000 marks the pixels it owns). Build
// reads the textures wwrando recolors out of Link.arc, decodes them to RGBA
// with the renderer's replacement keys (the same data scripts/windows/
// link_colors.py makes for the vanilla Link from the disc), and reads the
// masks. Recolor applies the player's chosen colors to them, as wwrando's
// color exchange would, and gives the pixels to register as replacements.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace model_colors {

// A model's files as the loader hands them over.
struct Files {
    std::string name;                  // the model's name (its folder's)
    std::vector<uint8_t> link_arc;     // Link.arc, as served (Yaz0 is expanded when it is compressed)
    std::string metadata;              // metadata.txt, empty when the model has none
    // Every picture under color_masks\: path relative to the model folder, lower case, '/'
    // ("color_masks/hero_hair.png", "color_masks/mouths/mouths3tc.1_skin.png") -> the PNG file.
    std::map<std::string, std::vector<uint8_t>> masks;
};

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> bytes; // RGBA8 for a texture; one byte a pixel for a mask (1 = red in the picture)
};

struct Texture {
    std::string name;               // "linktexS3TC", "hitomi", "mouthS3TC.3", ...
    std::vector<std::string> keys;  // the renderer's keys of its headers (more than one when they differ)
    Image rgba;                     // as the first header decodes it
};

struct Color {
    std::string name; // exactly as the metadata names it ("Belt Buckle")
    uint32_t base;    // 0xRRGGBB, what the region is in the original texture
};

struct Preset {
    std::string name;
    std::vector<std::pair<std::string, uint32_t>> colors; // name, 0xRRGGBB
};

struct Mask {
    bool casual = false;
    std::string color;
    std::string texture;
    std::string path; // the picture, as in Files::masks
    Image image;      // same size as the texture
};

struct Outfit {
    std::vector<Color> colors;   // in the metadata's order
    std::vector<Preset> presets;
    // Which color a texture with no mask of its own takes (customizer.py:430-449): "" when the
    // texture has a mask of any color, which makes the name unused.
    std::string hands_color, mouth_color, hitomi_color, eyebrow_color;
};

struct Set {
    std::string name;      // the model's name
    std::string id_name;   // name made safe for a CVar path
    std::string author, comment;
    std::string casual_text;             // the model's name for the Outset clothes (28 characters at most)
    bool disable_casual = false;         // no Outset clothes
    bool colored_eyebrows = false;       // has_colored_eyebrows
    std::string casual_hair_color;       // the color the Outset clothes' back hair takes
    Outfit outfits[2];                   // [0] Hero's Clothes, [1] Outset clothes
    std::vector<Texture> textures;
    std::vector<Mask> masks;
    std::vector<std::string> mask_paths; // the color_masks files that matched a color (for tests)
    std::vector<std::string> warnings;   // what was skipped and why
};

// False (and *error) only when Link.arc cannot be read. A model without metadata gives a Set with no colors.
bool Build(const Files& files, Set& out, std::string* error);

// "Link.<model>.<Hero|Casual>.<color>" with spaces and punctuation out of the names: the editor's id
// for a color of this model; its CVars are gCosmetics.<id>.Value / .Changed / ...
std::string ColorId(const Set& set, bool casual, const std::string& color);

// The color the player chose for a color of the model (0xRRGGBB), or nothing when it is not changed.
using Chosen = std::function<std::optional<uint32_t>(bool casual, const std::string& color)>;

struct Recolored {
    std::string texture;
    std::vector<std::string> keys; // register the pixels under each
    Image image;                   // RGBA8
};

// The textures the chosen colors change, recolored as customizer.py does. Hands, eyes, mouths and
// eyebrows are one texture for both outfits: they take the Outset colors only when only those were changed.
std::vector<Recolored> Recolor(const Set& set, const Chosen& chosen);

// wwrando's color_exchange (texture_utils.py) on one region: hue to the chosen color's, saturation
// and value moved by its difference from the base color. mask null: the whole image.
void Exchange(Image& img, const Image* mask, uint32_t base, uint32_t chosen, bool ignore_bright);

} // namespace model_colors
