// Settings > Mod Menu > Browse Models: The Wind Waker's player models on GameBanana (gamebanana.com, game 6173, Skins),
// listed with their pictures, and one chosen downloaded into the mods folder (through GameBanana's own download link, so
// it is counted) and put in effect. GameBanana's files are their authors': nothing is mirrored, the author and the licence
// are shown, and the page links to the mod's own page.
#pragma once

namespace gbbrowser {

// The page in the menu (the menu's scale).
void DrawEmbedded(float scale);
// Its own window, while it is popped out (gOpenWindows.ModelBrowser); every frame.
void Frame(float scale, float fontScale);

} // namespace gbbrowser
