// SPDX-License-Identifier: GPL-3.0-or-later
// What the settings menus share, so they look and work the same on every
// desktop build: the Mac and Linux menu (settings_menu.cpp) and the Windows one
// (windows/src/win_settings.cpp). Widgets that wrap in a narrow menu, and a
// controller in them.
#pragma once
#include <imgui.h>
#include <algorithm>
#include <imgui_internal.h> // ClearActiveID, ClosePopupsExceptModals, CalcTypematicRepeatAmount
#include <cmath>
#include <string>
#include "button_remap.h"

// A dropdown with its label on the right, or above it when the window is too
// narrow for both.
inline bool bw_menu_combo(const char* label, int* index, const char* const* items, int count) {
    const float avail = ImGui::GetContentRegionAvail().x;
    if (ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemInnerSpacing.x <= avail * 0.5f) {
        ImGui::SetNextItemWidth(std::max(80.f, avail * 0.48f));
        return ImGui::Combo(label, index, items, count);
    }
    ImGui::PushID(label);
    ImGui::TextWrapped("%s", label);
    ImGui::SetNextItemWidth(-1.f);
    const bool changed = ImGui::Combo("##value", index, items, count);
    ImGui::PopID();
    return changed;
}

// A checkbox whose label wraps when the window is too narrow for it; clicking
// the wrapped label toggles it too.
inline bool bw_menu_checkbox(const char* label, bool* value) {
    const ImGuiStyle& style = ImGui::GetStyle();
    if (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x <=
        ImGui::GetContentRegionAvail().x)
        return ImGui::Checkbox(label, value);
    ImGui::PushID(label);
    bool changed = ImGui::Checkbox("##box", value);
    ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
    ImGui::TextWrapped("%s", label);
    if (ImGui::IsItemClicked()) {
        *value = !*value;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

// The controller button the player mapped to the GameCube's B (Controls ›
// Controller buttons; "" is the controller's own layout), as an ImGui key:
// the menu's "back".
inline ImGuiKey bw_menu_back_key(const std::string& button_map) {
    BwButtonMap map;
    bw_button_map_parse(button_map, &map);
    switch (map.native[1]) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceDown;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return ImGuiKey_GamepadL3;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return ImGuiKey_GamepadR3;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_GamepadFaceRight;
    }
}

// The left stick moves through the menu as the D-pad does. ImGui moves only
// with the D-pad (its left stick scrolls), and the SDL backend sets the D-pad
// from the controller every frame, so a push of the stick reaches ImGui as a
// tap of an arrow key instead: one as it's pushed, then repeating while it's
// held, as a held D-pad does. Call it once a frame while the menu is open.
inline void bw_menu_stick_as_dpad() {
    static ImGuiDir held = ImGuiDir_None;
    static float held_for = 0.f;
    ImGuiIO& io = ImGui::GetIO();
    const auto value = [&io](ImGuiKey key) { return io.KeysData[key - ImGuiKey_NamedKey_BEGIN].AnalogValue; };
    const float x = value(ImGuiKey_GamepadLStickRight) - value(ImGuiKey_GamepadLStickLeft);
    const float y = value(ImGuiKey_GamepadLStickDown) - value(ImGuiKey_GamepadLStickUp);
    ImGuiDir dir = ImGuiDir_None;
    if (std::fabs(x) >= 0.5f || std::fabs(y) >= 0.5f)
        dir = std::fabs(x) > std::fabs(y) ? (x < 0.f ? ImGuiDir_Left : ImGuiDir_Right)
                                          : (y < 0.f ? ImGuiDir_Up : ImGuiDir_Down);
    if (dir != held) {
        held = dir;
        held_for = 0.f;
    } else {
        held_for += io.DeltaTime;
    }
    if (dir == ImGuiDir_None)
        return;
    const float before = held_for == 0.f ? -1.f : held_for - io.DeltaTime;
    if (ImGui::CalcTypematicRepeatAmount(before, held_for, io.KeyRepeatDelay, io.KeyRepeatRate * 0.8f) <= 0)
        return;
    const ImGuiKey key = dir == ImGuiDir_Left    ? ImGuiKey_LeftArrow
                         : dir == ImGuiDir_Right ? ImGuiKey_RightArrow
                         : dir == ImGuiDir_Up    ? ImGuiKey_UpArrow
                                                 : ImGuiKey_DownArrow;
    io.AddKeyEvent(key, true);
    io.AddKeyEvent(key, false);
}

// A selected slider moves with left and right (the D-pad, or the arrow keys,
// which the left stick taps: bw_menu_stick_as_dpad) without A to grab it
// first: -1, 1, or 0 for no press. Call it right after the slider; it fills
// its row, so left and right have nothing else to do there.
inline int bw_menu_slider_nudge() {
    if (!ImGui::IsItemFocused() || ImGui::IsItemActive())
        return 0;
    const auto pressed = [](ImGuiKey a, ImGuiKey b) {
        return ImGui::IsKeyPressed(a, true) || ImGui::IsKeyPressed(b, true);
    };
    if (pressed(ImGuiKey_GamepadDpadLeft, ImGuiKey_LeftArrow))
        return -1;
    if (pressed(ImGuiKey_GamepadDpadRight, ImGuiKey_RightArrow))
        return 1;
    return 0;
}

// The right stick scrolls the current window (a tab's body): the game isn't
// listening while the menu is open.
inline void bw_menu_stick_scroll() {
    const ImGuiIO& io = ImGui::GetIO();
    const auto stick = [&io](ImGuiKey key) { return io.KeysData[key - ImGuiKey_NamedKey_BEGIN].AnalogValue; };
    const float scroll = stick(ImGuiKey_GamepadRStickDown) - stick(ImGuiKey_GamepadRStickUp);
    if (scroll > 0.2f || scroll < -0.2f)
        ImGui::SetScrollY(ImGui::GetScrollY() + scroll * 900.f * io.DeltaTime);
}

// The game's B closes the menu, unless it is closing a dropdown or ending an
// edit. ImGui's own cancel is always the right face button and acts first, so
// this looks at how things were a frame ago; for another button, it does what
// that cancel does. Call it once a frame inside the menu's window; true: close.
inline bool bw_menu_back_pressed(ImGuiKey back, bool* busy_before) {
    bool close = false;
    if (ImGui::IsKeyPressed(back, false)) {
        if (!*busy_before)
            close = true;
        else if (back != ImGuiKey_GamepadFaceRight) {
            if (ImGui::IsAnyItemActive())
                ImGui::ClearActiveID();
            else
                ImGui::ClosePopupsExceptModals();
        }
    }
    *busy_before = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || ImGui::IsAnyItemActive();
    return close;
}
