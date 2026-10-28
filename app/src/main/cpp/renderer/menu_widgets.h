/**
 * menu_widgets.h - Controls of the overlay menu that ImGui does not draw the way the app's own screens look.
 *
 * They take their colours from ImGui's style, which applyTheme() in imgui_menu.cpp sets, and their sizes from
 * the font's height, so they follow the menu's look and scale with it. All of them are for the GL thread,
 * between ImGui::NewFrame() and ImGui::Render().
 */
#ifndef AIMBUDDY_RENDERER_MENU_WIDGETS_H
#define AIMBUDDY_RENDERER_MENU_WIDGETS_H

#include "imgui/imgui.h"

namespace aimbuddy::menu {

/// How a pill button is coloured.
enum class PillStyle {
    kPrimary,   ///< Filled with the accent colour: the main action of a row of buttons.
    kDanger,    ///< Tinted red: an action that stops something.
};

/// A fully rounded button of the given size. Its label shrinks to fit the width when it is too long. Returns true when tapped.
bool PillButton(const char* label, const ImVec2& size, PillStyle style);

}  // namespace aimbuddy::menu

#endif  // AIMBUDDY_RENDERER_MENU_WIDGETS_H
