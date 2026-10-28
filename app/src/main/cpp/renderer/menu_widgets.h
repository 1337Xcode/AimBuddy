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
    kTinted,    ///< The accent colour on a faint fill of its own: an action in the middle of a page.
    kDanger,    ///< Tinted red: an action that stops something.
};

/// A fully rounded button of the given size. Its label shrinks to fit the width when it is too long. Returns true when tapped.
bool PillButton(const char* label, const ImVec2& size, PillStyle style);

/**
 * A slider: its name at the left and its value at the right of one line, and under them a thin track with a
 * round thumb. `valueFormat` is the printf format of the value, such as "%.0f px", and the value is rounded to
 * what that shows. Returns true when the player changed the value.
 */
bool Slider(const char* label, float* value, float min, float max, const char* valueFormat);

/// The same for a whole number; `valueFormat` takes an int, such as "%d".
bool SliderInt(const char* label, int* value, int min, int max, const char* valueFormat);

/// A drop down list with its name above it, over the full width. Returns true when another option was chosen.
bool Choice(const char* label, int* selected, const char* const* options, int count);

/**
 * Starts a rounded panel around the controls drawn until EndCard(). The sliders and lists in it stop short of the
 * panel's edge. Cards do not nest.
 */
void BeginCard();
void EndCard();

/// How wide the controls drawn next may be: the rest of the page's width, or of the open card's.
float ContentWidth();

/// A thin line between two rows of a card, from the card's text edge to its right edge.
void Divider();

/// A small dimmed paragraph, for what a card does not say itself. It wraps at the edge of the page.
void Footnote(const char* text);

}  // namespace aimbuddy::menu

#endif  // AIMBUDDY_RENDERER_MENU_WIDGETS_H
