#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../runtime/host/src/ship/cvar.h"

static std::string read_all(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "bod_cvar_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "settings.json";
    std::filesystem::remove(path);

    // A missing file: every read is its default.
    CVarInit(path.string().c_str());
    assert(!CVarExists("gSettings.Graphics.Vsync"));
    assert(CVarGetInteger("gSettings.Graphics.Vsync", 1) == 1);
    assert(std::strcmp(CVarGetString("gSettings.Controls.ButtonMap", "x"), "x") == 0);

    CVarSetInteger("gSettings.Graphics.Vsync", 0);
    CVarSetInteger("gSettings.Window.X", -1200);
    CVarSetFloat("gSettings.Menu.BackgroundOpacity", 0.5f);
    CVarSetString("gSettings.Controls.ButtonMap", "A=1,B=2");
    CVarSetColor("gSettings.Menu.Accent", CVarColor{10, 20, 30, 40});
    CVarSetInteger("gEnhancements.BetterWW.instant_text", 1);
    CVarSave();
    assert(!std::filesystem::exists(dir / "settings.json.pending"));
    const std::string text = read_all(path);
    assert(text.find("\"CVars\"") != std::string::npos);
    assert(text.find("\"BetterWW\"") != std::string::npos); // nested by the dots

    // Read back.
    CVarInit(path.string().c_str());
    assert(CVarGetInteger("gSettings.Graphics.Vsync", 1) == 0);
    assert(CVarGetInteger("gSettings.Window.X", 0) == -1200);
    assert(std::fabs(CVarGetFloat("gSettings.Menu.BackgroundOpacity", 0.f) - 0.5f) < 1e-6f);
    assert(std::strcmp(CVarGetString("gSettings.Controls.ButtonMap", ""), "A=1,B=2") == 0);
    const CVarColor c = CVarGetColor("gSettings.Menu.Accent", CVarColor{});
    assert(c.r == 10 && c.g == 20 && c.b == 30 && c.a == 40);
    assert(CVarGetInteger("gEnhancements.BetterWW.instant_text", 0) == 1);

    // Clearing one, and a block.
    CVarClear("gSettings.Window.X");
    assert(CVarGetInteger("gSettings.Window.X", 7) == 7);
    CVarClearBlock("gEnhancements.");
    assert(!CVarExists("gEnhancements.BetterWW.instant_text"));
    assert(CVarExists("gSettings.Graphics.Vsync"));

    // Saved on the tick after a change, not before.
    CVarSetInteger("gCheats.SaveStates", 1);
    CVarSaveNextFrame();
    assert(read_all(path).find("gCheats") == std::string::npos);
    CVarTick();
    assert(read_all(path).find("gCheats") != std::string::npos);

    // An unreadable file starts from defaults.
    { std::ofstream(path, std::ios::binary | std::ios::trunc) << "{ not json"; }
    CVarInit(path.string().c_str());
    assert(!CVarExists("gSettings.Graphics.Vsync"));
    assert(read_all(dir / "settings.json.unreadable") == "{ not json"); // kept before the next save

    std::filesystem::remove_all(dir);
    std::puts("cvar_test: ok");
    return 0;
}
