// HUD button colors for the buttons whose color is their texture (the A
// button's disc, B, Z): the Cosmetics Editor's colors applied by recoloring
// those textures from the disc's menures.arc and handing them to the renderer
// as replacements, as Link's colors are (link_colors.h). The X, Y and R
// buttons and the A button's ring are pane tints (runtime/host/src/cosmetics.c).
#pragma once

namespace hud_colors {

// Once a frame: re-registers the textures whose chosen color changed.
void Frame();

} // namespace hud_colors
