// The Input Viewer: see input_viewer.h.
#ifdef rename
#undef rename // bw_posix_compat.h's, which <filesystem> (aurora's headers) must not see
#endif

#include "input_viewer.h"

#include "ship_menu.h"
#include "ship/cvar.h"

#include <dolphin/pad.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

// runtime/host/src/main.c: the pad state port 1 was last handed (after mappings, blocking, scripts).
extern "C" bool bluewake_input_viewer_pad(uint16_t* button, int8_t* stick_x, int8_t* stick_y, int8_t* substick_x,
                                          int8_t* substick_y, uint8_t* trigger_left, uint8_t* trigger_right);

namespace inputviewer {
namespace {

using shipmenu::WidgetInfo;
using shipmenu::WidgetType;

#define VIEWER_CVAR(name) "gInputViewer." name
constexpr const char* kShown = "gOpenWindows.InputViewer";

enum Outline { OUTLINE_ALWAYS, OUTLINE_NOT_PRESSED, OUTLINE_PRESSED, OUTLINE_HIDDEN };
enum StickMode { STICK_ALWAYS, STICK_IN_USE, STICK_HIDDEN };

constexpr float kPi = 3.14159265f;
// The buttons are laid out in a 635 x 280 box (units); a line is 4 units wide.
constexpr float kW = 635.f, kH = 280.f, kLine = 4.f;

struct Canvas {
    ImDrawList* dl;
    ImVec2 origin;
    float s; // pixels per unit
    ImVec2 p(float x, float y) const { return ImVec2(origin.x + x * s, origin.y + y * s); }
    float r(float v) const { return v * s; }
};

ImU32 rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

const ImU32 kWhite = rgba(240, 240, 240), kYellow = rgba(255, 222, 0), kGreen = rgba(30, 205, 140),
            kRed = rgba(225, 10, 20), kPurple = rgba(160, 80, 175);

// What a button shows: its outline (by the Button Outlines setting) and, while pressed, its fill.
struct Look {
    bool fill, outline;
};
Look look(int mode, bool pressed) {
    const bool outline = mode == OUTLINE_ALWAYS || (mode == OUTLINE_NOT_PRESSED && !pressed) ||
                         (mode == OUTLINE_PRESSED && pressed);
    return {pressed, outline};
}

void ring(const Canvas& c, float x, float y, float radius, ImU32 color, Look l) {
    if (l.fill)
        c.dl->AddCircleFilled(c.p(x, y), c.r(radius), color, 48);
    if (l.outline || l.fill)
        c.dl->AddCircle(c.p(x, y), c.r(radius - kLine * 0.5f), color, 48, c.r(kLine));
}

// A rounded band along a curve: the outline of a pill (straight) or a kidney bean (curved), with round ends.
// The centre line is a quadratic curve from a to b that bows `bow` units to the left of a->b; the half width
// grows from `waist` in the middle to `end` at the ends.
std::vector<ImVec2> band(const Canvas& c, ImVec2 a, ImVec2 b, float bow, float waist, float end) {
    constexpr int kN = 20, kCap = 12;
    const ImVec2 d(b.x - a.x, b.y - a.y);
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    const ImVec2 left(d.y / len, -d.x / len);
    const ImVec2 ctl((a.x + b.x) * 0.5f + left.x * bow * 2.f, (a.y + b.y) * 0.5f + left.y * bow * 2.f);
    ImVec2 pt[kN], nl[kN];
    float hw[kN];
    for (int i = 0; i < kN; ++i) {
        const float t = float(i) / float(kN - 1), u = 1.f - t;
        pt[i] = ImVec2(u * u * a.x + 2 * u * t * ctl.x + t * t * b.x, u * u * a.y + 2 * u * t * ctl.y + t * t * b.y);
        const ImVec2 tan(2 * u * (ctl.x - a.x) + 2 * t * (b.x - ctl.x), 2 * u * (ctl.y - a.y) + 2 * t * (b.y - ctl.y));
        const float tl = std::sqrt(tan.x * tan.x + tan.y * tan.y);
        nl[i] = ImVec2(tan.y / tl, -tan.x / tl);
        const float m = std::fabs(t - 0.5f) * 2.f;
        hw[i] = waist + (end - waist) * m * m;
    }
    std::vector<ImVec2> out;
    for (int i = 0; i < kN; ++i)
        out.push_back(c.p(pt[i].x + nl[i].x * hw[i], pt[i].y + nl[i].y * hw[i]));
    // Round end at b: from the left side around the front to the right side.
    const float ab = std::atan2(nl[kN - 1].y, nl[kN - 1].x);
    for (int k = 1; k < kCap; ++k) {
        const float ang = ab + kPi * float(k) / float(kCap);
        out.push_back(c.p(pt[kN - 1].x + std::cos(ang) * hw[kN - 1], pt[kN - 1].y + std::sin(ang) * hw[kN - 1]));
    }
    for (int i = kN - 1; i >= 0; --i)
        out.push_back(c.p(pt[i].x - nl[i].x * hw[i], pt[i].y - nl[i].y * hw[i]));
    const float aa = std::atan2(-nl[0].y, -nl[0].x);
    for (int k = 1; k < kCap; ++k) {
        const float ang = aa + kPi * float(k) / float(kCap);
        out.push_back(c.p(pt[0].x + std::cos(ang) * hw[0], pt[0].y + std::sin(ang) * hw[0]));
    }
    return out;
}

void band_button(const Canvas& c, ImVec2 a, ImVec2 b, float bow, float waist, float end, ImU32 color, Look l) {
    if (!l.fill && !l.outline)
        return;
    if (l.fill) {
        const std::vector<ImVec2> fill = band(c, a, b, bow, waist, end);
        c.dl->AddConcavePolyFilled(fill.data(), int(fill.size()), color);
    }
    // The outline's centre runs half a line inside the shape, so the shape's size is the same either way.
    const float in = kLine * 0.5f;
    const std::vector<ImVec2> edge = band(c, a, b, bow, waist - in, end - in);
    c.dl->AddPolyline(edge.data(), int(edge.size()), color, ImDrawFlags_Closed, c.r(kLine));
}

void octagon(const Canvas& c, float cx, float cy, float radius, ImU32 color) {
    ImVec2 pts[8];
    for (int i = 0; i < 8; ++i) {
        const float a = -kPi * 0.5f + float(i) * kPi / 4.f; // a corner at the top
        pts[i] = c.p(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
    }
    c.dl->AddPolyline(pts, 8, color, ImDrawFlags_Closed, c.r(kLine));
}

// Where the knob sits for a stick value (-127..127, up is +y): `travel` units from the centre at full tilt.
ImVec2 knob(float cx, float cy, float travel, int x, int y) {
    float fx = float(x) / 127.f, fy = float(y) / 127.f;
    const float len = std::sqrt(fx * fx + fy * fy);
    if (len > 1.f) {
        fx /= len;
        fy /= len;
    }
    return ImVec2(cx + fx * travel, cy - fy * travel);
}

// L and R: a pill whose body fills with the analog value; its end, past the divider, lights at the full click.
void trigger(const Canvas& c, float x0, float x1, float y, bool right_side, int value, bool click, int mode) {
    constexpr float h = 10.f, cap = 20.f;
    const float div = right_side ? x0 + cap : x1 - cap;
    const float f = std::clamp(float(value) / 255.f, 0.f, 1.f);
    if (f > 0.02f) {
        const float body0 = right_side ? div : x0, body1 = right_side ? x1 : div;
        const float a = right_side ? body1 - (body1 - body0) * f : body0, b = right_side ? body1 : body0 + (body1 - body0) * f;
        c.dl->AddRectFilled(c.p(a, y - h), c.p(b, y + h), rgba(240, 240, 240, 170), c.r(h),
                            right_side ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersLeft);
    }
    if (click)
        c.dl->AddRectFilled(c.p(right_side ? x0 : div, y - h), c.p(right_side ? div : x1, y + h), kWhite, c.r(h),
                            right_side ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight);
    if (look(mode, click || f > 0.02f).outline) {
        c.dl->AddRect(c.p(x0, y - h), c.p(x1, y + h), kWhite, c.r(h), 0, c.r(kLine));
        c.dl->AddLine(c.p(div, y - h), c.p(div, y + h), kWhite, c.r(kLine));
    }
}

// The D-pad: a cross outline, each pressed arm filled.
void dpad(const Canvas& c, float cx, float cy, unsigned buttons, int mode) {
    constexpr float w = 9.f, len = 24.f; // half the arm's width, the arm's reach
    struct Arm {
        unsigned bit;
        float dx, dy;
    } arms[4] = {{PAD_BUTTON_UP, 0, -1}, {PAD_BUTTON_DOWN, 0, 1}, {PAD_BUTTON_LEFT, -1, 0}, {PAD_BUTTON_RIGHT, 1, 0}};
    bool any = false;
    for (const Arm& a : arms) {
        if (!(buttons & a.bit))
            continue;
        any = true;
        const float x0 = a.dx == 0 ? cx - w : (a.dx < 0 ? cx - len : cx);
        const float x1 = a.dx == 0 ? cx + w : (a.dx < 0 ? cx : cx + len);
        const float y0 = a.dy == 0 ? cy - w : (a.dy < 0 ? cy - len : cy);
        const float y1 = a.dy == 0 ? cy + w : (a.dy < 0 ? cy : cy + len);
        c.dl->AddRectFilled(c.p(x0, y0), c.p(x1, y1), kWhite);
    }
    if (!look(mode, any).outline)
        return;
    const ImVec2 pts[12] = {c.p(cx - w, cy - len), c.p(cx + w, cy - len), c.p(cx + w, cy - w), c.p(cx + len, cy - w),
                            c.p(cx + len, cy + w), c.p(cx + w, cy + w),   c.p(cx + w, cy + len), c.p(cx - w, cy + len),
                            c.p(cx - w, cy + w),   c.p(cx - len, cy + w), c.p(cx - len, cy - w), c.p(cx - w, cy - w)};
    c.dl->AddPolyline(pts, 12, kWhite, ImDrawFlags_Closed, c.r(kLine * 0.8f));
}

void draw_pad(const PADStatus& st, float scale) {
    const float s = CVarGetFloat(VIEWER_CVAR("Scale"), 1.0f) * scale * 0.5f;
    const bool analog = CVarGetInteger(VIEWER_CVAR("ShowAnalogValues"), 0) != 0;
    const int mode = std::clamp(CVarGetInteger(VIEWER_CVAR("ButtonOutline"), OUTLINE_ALWAYS), 0, 3);
    const int stick_mode = std::clamp(CVarGetInteger(VIEWER_CVAR("StickMode"), STICK_ALWAYS), 0, 2);
    const int cstick_mode = std::clamp(CVarGetInteger(VIEWER_CVAR("CStickMode"), STICK_ALWAYS), 0, 2);

    const ImVec2 size(kW * s, (kH + (analog ? 40.f : 0.f)) * s);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    const Canvas c{ImGui::GetWindowDrawList(), origin, s};
    const unsigned b = st.button;

    trigger(c, 30, 170, 25, false, st.triggerLeft, (b & PAD_TRIGGER_L) != 0, mode);
    trigger(c, 232, 367, 25, false, st.triggerRight, (b & PAD_TRIGGER_R) != 0, mode);

    // The control stick: its gate, and the knob's rings and centre that move with it.
    const bool lmoved = std::abs(int(st.stickX)) > 2 || std::abs(int(st.stickY)) > 2;
    if (stick_mode == STICK_ALWAYS || (stick_mode == STICK_IN_USE && lmoved)) {
        octagon(c, 95, 150, 80, kWhite);
        const ImVec2 k = knob(95, 150, 30, st.stickX, st.stickY);
        for (const float rr : {46.f, 37.f, 28.f})
            c.dl->AddCircle(c.p(k.x, k.y), c.r(rr), kWhite, 48, c.r(kLine * 0.75f));
        c.dl->AddCircle(c.p(k.x, k.y), c.r(9), kWhite, 24, c.r(kLine * 0.75f));
    }
    // The C-stick: a yellow gate and a solid yellow knob.
    const bool cmoved = std::abs(int(st.substickX)) > 2 || std::abs(int(st.substickY)) > 2;
    if (cstick_mode == STICK_ALWAYS || (cstick_mode == STICK_IN_USE && cmoved)) {
        octagon(c, 298, 150, 62, kYellow);
        const ImVec2 k = knob(298, 150, 22, st.substickX, st.substickY);
        c.dl->AddCircleFilled(c.p(k.x, k.y), c.r(36), kYellow, 48);
    }

    dpad(c, 196, 245, b, mode);
    ring(c, 404, 115, 20, kWhite, look(mode, (b & PAD_BUTTON_START) != 0));
    ring(c, 505, 145, 53, kGreen, look(mode, (b & PAD_BUTTON_A) != 0));
    ring(c, 425, 205, 27, kRed, look(mode, (b & PAD_BUTTON_B) != 0));
    // Y above A and X to its right: beans bowed away from A, a little fatter at their ends.
    band_button(c, ImVec2(462, 76), ImVec2(506, 52), 5.f, 18.f, 21.f, kWhite, look(mode, (b & PAD_BUTTON_Y) != 0));
    band_button(c, ImVec2(580, 104), ImVec2(592, 150), 5.f, 18.f, 21.f, kWhite, look(mode, (b & PAD_BUTTON_X) != 0));
    // Z: a purple pill over the X button.
    band_button(c, ImVec2(560, 28), ImVec2(586, 47), 0.f, 12.f, 12.f, kPurple, look(mode, (b & PAD_TRIGGER_Z) != 0));

    if (analog) {
        char text[96];
        std::snprintf(text, sizeof text, "Stick %d, %d   C %d, %d   L %d  R %d", st.stickX, st.stickY, st.substickX,
                      st.substickY, st.triggerLeft, st.triggerRight);
        const ImVec2 ts = ImGui::CalcTextSize(text);
        c.dl->AddText(ImVec2(c.p(kW * 0.5f, 0).x - ts.x * 0.5f, c.p(0, kH + 8).y), rgba(255, 255, 255, 230), text);
    }
}

PADStatus read_status() {
    PADStatus st{};
    uint16_t button = 0;
    int8_t lx = 0, ly = 0, cx = 0, cy = 0;
    uint8_t tl = 0, tr = 0;
    bluewake_input_viewer_pad(&button, &lx, &ly, &cx, &cy, &tl, &tr);
    st.button = button;
    st.stickX = lx;
    st.stickY = ly;
    st.substickX = cx;
    st.substickY = cy;
    st.triggerLeft = tl;
    st.triggerRight = tr;
    return st;
}

} // namespace

// An overlay like the item tracker: no title bar or frame, see-through unless a background is chosen, and clicks
// pass through it unless dragging is on; its place is kept as a fraction of the screen.
void Frame(float scale, float fontScale) {
    if (!CVarGetInteger(kShown, 0))
        return;
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoScrollbar;
    if (!CVarGetInteger(VIEWER_CVAR("Draggable"), 0))
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // Placed again whenever the screen size changes (the window starts at 4:3 and is resized a moment later).
    static ImVec2 placed_for;
    const bool resized = placed_for.x != display.x || placed_for.y != display.y;
    placed_for = display;
    // Its size, to place it: bottom centre until it is dragged, and always kept inside the screen.
    const float s = CVarGetFloat(VIEWER_CVAR("Scale"), 1.0f) * scale * 0.5f;
    const ImVec2 pad = ImGui::GetStyle().WindowPadding;
    const ImVec2 size(kW * s + pad.x * 2.f,
                      (kH + (CVarGetInteger(VIEWER_CVAR("ShowAnalogValues"), 0) ? 40.f : 0.f)) * s + pad.y * 2.f);
    const bool placed = CVarExists(VIEWER_CVAR("Position.X")) && CVarExists(VIEWER_CVAR("Position.Y"));
    const float fx = CVarGetFloat(VIEWER_CVAR("Position.X"), 0.f), fy = CVarGetFloat(VIEWER_CVAR("Position.Y"), 0.f);
    const ImVec2 want = placed ? ImVec2(fx * display.x, fy * display.y)
                               : ImVec2((display.x - size.x) * 0.5f, display.y - size.y - 8.f * scale);
    ImGui::SetNextWindowPos(ImVec2(std::clamp(want.x, 0.f, std::max(0.f, display.x - size.x)),
                                   std::clamp(want.y, 0.f, std::max(0.f, display.y - size.y))),
                            resized ? ImGuiCond_Always : ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, CVarGetFloat(VIEWER_CVAR("Opacity"), 0.0f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f * scale);
    ImGui::Begin("Input Viewer##overlay", nullptr, flags);
    ImGui::SetWindowFontScale(fontScale);
    draw_pad(read_status(), scale);
    if (ImGui::IsWindowFocused() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && display.x > 0 && display.y > 0) {
        const ImVec2 pos = ImGui::GetWindowPos();
        if (std::fabs(pos.x / display.x - fx) > 1e-4f || std::fabs(pos.y / display.y - fy) > 1e-4f) {
            CVarSetFloat(VIEWER_CVAR("Position.X"), pos.x / display.x);
            CVarSetFloat(VIEWER_CVAR("Position.Y"), pos.y / display.y);
            CVarSaveNextFrame();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void BuildMenu(shipmenu::Menu& m) {
    const char* H = "Settings";
    const char* S = "Input Viewer";
    m.AddSidebar(H, S, 1);
    m.AddWidget(H, S, 0, "Input Viewer", WidgetType::SeparatorText);
    m.AddWidget(H, S, 0, "Show Input Viewer", WidgetType::CVarCheckbox)
        .CVar(kShown)
        .Tooltip("GameCube buttons over the game that light up with what the game is being handed on port 1: from the "
                 "keyboard, a controller or the GameCube adapter.");
    m.AddWidget(H, S, 0, "Enable Dragging", WidgetType::CVarCheckbox)
        .CVar(VIEWER_CVAR("Draggable"))
        .Tooltip("Lets you move the Input Viewer with the mouse. Turn it off again so clicks pass through it.");
    m.AddWidget(H, S, 0, "Input Viewer Settings", WidgetType::SeparatorText);
    m.AddWidget(H, S, 0, "Scale: %.1fx", WidgetType::CVarSliderFloat)
        .CVar(VIEWER_CVAR("Scale"))
        .RangeFloat(0.5f, 2.5f, 0.1f)
        .DefaultFloat(1.0f);
    m.AddWidget(H, S, 0, "Background Opacity: %.0f %%", WidgetType::CVarSliderFloat)
        .CVar(VIEWER_CVAR("Opacity"))
        .RangeFloat(0.0f, 1.0f, 0.05f)
        .DefaultFloat(0.0f)
        .Percent()
        .Tooltip("A dark panel behind the buttons. 0 leaves only the buttons over the game.");
    m.AddWidget(H, S, 0, "Show Analog Values", WidgetType::CVarCheckbox)
        .CVar(VIEWER_CVAR("ShowAnalogValues"))
        .Tooltip("The numbers under the buttons: both sticks (-127 to 127) and the triggers (0 to 255).");
    m.AddWidget(H, S, 0, "Button Outlines", WidgetType::CVarCombobox)
        .CVar(VIEWER_CVAR("ButtonOutline"))
        .Items({"Always Shown", "Shown Only While Not Pressed", "Shown Only While Pressed", "Always Hidden"})
        .DefaultValue(OUTLINE_ALWAYS);
    m.AddWidget(H, S, 0, "Control Stick", WidgetType::CVarCombobox)
        .CVar(VIEWER_CVAR("StickMode"))
        .Items({"Always", "While In Use", "Never"})
        .DefaultValue(STICK_ALWAYS);
    m.AddWidget(H, S, 0, "C-Stick", WidgetType::CVarCombobox)
        .CVar(VIEWER_CVAR("CStickMode"))
        .Items({"Always", "While In Use", "Never"})
        .DefaultValue(STICK_ALWAYS);
}

} // namespace inputviewer
