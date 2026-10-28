/**
 * menu_widgets.cpp - See menu_widgets.h.
 */

#include "renderer/menu_widgets.h"

#include <algorithm>

namespace aimbuddy::menu {

namespace {

/// The red of the app's destructive actions, as a text colour and, much fainter, as a fill.
constexpr ImVec4 kDangerText(1.0f, 0.42f, 0.39f, 1.0f);
constexpr float kDangerFillAlpha = 0.18f;
constexpr float kDangerFillPressedAlpha = 0.34f;

/// How far the label may shrink to fit a narrow button before it is cut off instead.
constexpr float kLeastLabelScale = 0.75f;

/// The space, in font heights, kept between a pill's end and its label.
constexpr float kPillPaddingEm = 0.9f;

ImVec4 scaled(const ImVec4& colour, float factor) {
    return ImVec4(std::min(1.0f, colour.x * factor), std::min(1.0f, colour.y * factor), std::min(1.0f, colour.z * factor), colour.w);
}

}  // namespace

bool PillButton(const char* label, const ImVec2& size, PillStyle style) {
    const ImGuiStyle& theme = ImGui::GetStyle();
    ImVec4 fill;
    ImVec4 fillPressed;
    ImVec4 text;
    if (style == PillStyle::kPrimary) {
        fill = theme.Colors[ImGuiCol_SliderGrab];   // the accent colour
        fillPressed = scaled(fill, 0.8f);
        text = theme.Colors[ImGuiCol_WindowBg];     // the dark of the window reads on the accent
        text.w = 1.0f;
    } else {
        fill = ImVec4(kDangerText.x, kDangerText.y, kDangerText.z, kDangerFillAlpha);
        fillPressed = ImVec4(kDangerText.x, kDangerText.y, kDangerText.z, kDangerFillPressedAlpha);
        text = kDangerText;
    }

    // A label that is too long for the button is drawn smaller rather than cut off.
    const float room = size.x - 2.0f * kPillPaddingEm * ImGui::GetFontSize();
    const float labelWidth = ImGui::CalcTextSize(label).x;
    const float scale = labelWidth > room && labelWidth > 0.0f ? std::max(kLeastLabelScale, room / labelWidth) : 1.0f;

    ImGui::PushStyleColor(ImGuiCol_Button, fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fill);   // a finger has no hover: the press is what shows
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, fillPressed);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.5f * size.y);
    ImGui::PushFont(nullptr, theme.FontSizeBase * scale);
    const bool tapped = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return tapped;
}

}  // namespace aimbuddy::menu
