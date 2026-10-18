// The settings menu, structured the way Ship of Harkinian's port menu is: a
// row of headers (Settings, Enhancements, Dev Tools, ...), a sidebar of pages
// for the chosen header, and one to three columns of widgets on the page, with
// a search box over every label and tooltip. Pages are declared as tables of
// widgets (AddHeader, AddSidebar, AddWidget with chained options); a widget
// that holds a setting names its CVar (ship/cvar.h) and saves on change.
//
// SoH publishes no license, so none of its code is used: this is a fresh
// implementation of the same structure and behaviour.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

namespace shipmenu {

// The menu's accent colours (Settings > General > Menu Theme).
int theme_count();
const char* theme_name(int index);
ImVec4 theme_color(int index);
// The menu's look (theme color on buttons, frames and checks) for another
// window, such as the Cosmetics Editor: Push before Begin, Pop after End.
void PushThemeStyle();
void PopThemeStyle();
// The menu's UI scale, for a page's tool that is drawn in the menu (valid while the menu draws).
float MenuScale();
// The detached window of a Window page: draws `body` in its own ImGui window while `cvar` is 1, and sets the CVar
// to 0 (docking the page back into the menu) when the player closes it. `size` is in screen pixels.
void PopoutWindow(const char* title, const char* cvar, ImVec2 size, float background, float fontScale,
                  const std::function<void()>& body, const std::function<void()>& onClose = {});

enum class WidgetType {
    CVarCheckbox,
    CVarCombobox,
    CVarSliderInt,
    CVarSliderFloat,
    CVarInputText,   // a string CVar; maxInt is its longest length
    CVarColorPicker, // a color CVar (defaultColor)
    Button,
    WindowButton, // toggles a window's gOpenWindows.* CVar
    Window,       // a tool drawn in the page (custom), with a Popout button that detaches it into its own window
                  // (the gOpenWindows.* CVar in `cvar` is 1 while it is out); closing that window docks it back
    Text,
    SeparatorText,
    Separator,
    Custom,
};

struct WidgetInfo;
using WidgetFunc = std::function<void(WidgetInfo&)>;

struct WidgetInfo {
    std::string name;
    std::string cvar;
    WidgetType type = WidgetType::Text;
    std::string tooltip;
    // Combobox
    std::vector<std::string> items;
    // Defaults and ranges
    int defaultInt = 0;
    float defaultFloat = 0.f;
    int minInt = 0, maxInt = 100, stepInt = 1;
    float minFloat = 0.f, maxFloat = 1.f, stepFloat = 0.1f;
    // A slider shows "name" with its value: the name holds the format
    // ("Internal Resolution: %d %%"). Percent multiplies a float by 100.
    bool percent = false;
    ImVec4 color{0, 0, 0, 0}; // Text colour (0 alpha: the default)
    std::string defaultText;  // CVarInputText
    ImVec4 defaultColor{1, 1, 1, 1}; // CVarColorPicker (w is the default alpha)
    bool useAlpha = false;           // CVarColorPicker: an alpha slider too, and the CVar keeps the alpha
    WidgetFunc callback;      // after the value changed (or the button was pressed)
    WidgetFunc preFunc;       // each frame before drawing: set disabled reasons, hidden
    WidgetFunc custom;        // Custom: draws itself
    std::vector<std::string> disabledReasons; // filled by preFunc; non-empty disables
    bool hidden = false;
    bool sameLine = false;
    bool hideInSearch = false;

    WidgetInfo& CVar(const std::string& name_) { cvar = name_; return *this; }
    WidgetInfo& Tooltip(const std::string& text) { tooltip = text; return *this; }
    WidgetInfo& Items(std::vector<std::string> list) { items = std::move(list); return *this; }
    WidgetInfo& DefaultValue(int value) { defaultInt = value; defaultFloat = static_cast<float>(value); return *this; }
    WidgetInfo& DefaultFloat(float value) { defaultFloat = value; return *this; }
    WidgetInfo& Range(int lo, int hi, int step = 1) { minInt = lo; maxInt = hi; stepInt = step; return *this; }
    WidgetInfo& RangeFloat(float lo, float hi, float step) { minFloat = lo; maxFloat = hi; stepFloat = step; return *this; }
    WidgetInfo& Percent() { percent = true; return *this; }
    WidgetInfo& Color(ImVec4 c) { color = c; return *this; }
    WidgetInfo& DefaultText(std::string text) { defaultText = std::move(text); return *this; }
    WidgetInfo& DefaultColor(ImVec4 c) { defaultColor = c; return *this; }
    WidgetInfo& UseAlpha() { useAlpha = true; return *this; }
    WidgetInfo& Callback(WidgetFunc f) { callback = std::move(f); return *this; }
    WidgetInfo& PreFunc(WidgetFunc f) { preFunc = std::move(f); return *this; }
    WidgetInfo& CustomFunction(WidgetFunc f) { custom = std::move(f); return *this; }
    WidgetInfo& SameLine() { sameLine = true; return *this; }
    WidgetInfo& HideInSearch() { hideInSearch = true; return *this; }
};

struct Sidebar {
    int columns = 1;
    std::vector<std::vector<WidgetInfo>> widgets;
};

struct Header {
    std::string label;
    std::string sidebarCVar; // remembers the page last shown under this header
    std::vector<std::string> order;
    std::map<std::string, Sidebar> sidebars;
};

// A popup with a question and two buttons.
struct Modal {
    std::string title, text, yes, no;
    std::function<void()> onYes;
};

class Menu {
public:
    Header& AddHeader(const std::string& label);
    Sidebar& AddSidebar(const std::string& header, const std::string& name, int columns);
    WidgetInfo& AddWidget(const std::string& header, const std::string& sidebar, int column, const std::string& name,
                          WidgetType type);

    // The whole menu, over the game. `scale` is the UI scale for sizes,
    // `fontScale` the factor for the font (the font atlas may be drawn at a
    // larger size); `open` goes false when the player closes it.
    void Draw(bool* open, float scale, float fontScale);
    // Runs the callback of the widget that holds this CVar, as if its value had
    // just been changed in the menu (testing, and settings changed from code).
    bool FireCallback(const std::string& cvar);
    void Ask(Modal modal) { mModal = std::move(modal); mModalOpen = true; }

    std::function<void()> onQuit;
    std::function<void()> onRestart;

private:
    void DrawWidget(WidgetInfo& widget, float width);
    void DrawSearchResults(const std::string& query, float width);

    std::vector<std::string> mOrder;
    std::map<std::string, Header> mHeaders;
    char mSearch[128] = "";
    Modal mModal;
    bool mModalOpen = false;
    float mScale = 1.f;
};

} // namespace shipmenu
