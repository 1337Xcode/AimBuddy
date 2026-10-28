/**
 * menu_widgets.cpp - See menu_widgets.h.
 */

#include "renderer/menu_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

// How a slider is laid out, in font heights.
constexpr float kStripEm = 2.2f;         ///< The height a finger can drag in: far more than the thumb, so it is easy to hit.
constexpr float kThumbRadiusEm = 0.78f;
constexpr float kTrackEm = 0.38f;
constexpr float kNameGapEm = 0.12f;      ///< Between the line with the name and the strip under it.
constexpr float kValueGapEm = 0.8f;      ///< The least between a long name and the value.
constexpr float kHaloScale = 1.9f;       ///< The glow round the thumb while a finger holds it, against the thumb's size.
constexpr float kHaloAlpha = 0.22f;

ImVec4 scaled(const ImVec4& colour, float factor) {
    return ImVec4(std::min(1.0f, colour.x * factor), std::min(1.0f, colour.y * factor), std::min(1.0f, colour.z * factor), colour.w);
}

/// Rounds `value` to the decimals that the printf format shows, so that what is kept is what is displayed.
float roundedToFormat(float value, const char* format) {
    char text[32];
    std::snprintf(text, sizeof(text), format, static_cast<double>(value));
    return std::strtof(text, nullptr);
}

/// Draws the name at the left of a line and the value, dimmer, at its right. A name too long for the room is cut off before the value.
void drawNameAndValue(const ImVec2& at, float width, const char* name, const char* valueText) {
    ImDrawList* list = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float valueWidth = ImGui::CalcTextSize(valueText).x;
    list->AddText(font, size, ImVec2(at.x + width - valueWidth, at.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), valueText);
    const float room = width - valueWidth - kValueGapEm * size;
    const ImVec4 clip(at.x, at.y, at.x + room, at.y + size + 2.0f);
    list->AddText(font, size, at, ImGui::GetColorU32(ImGuiCol_Text), name, nullptr, 0.0f, &clip);
}

/// Moves the cursor up from the usual space under a line of text to the smaller gap between a name and the control it belongs to.
void tuckUnderName() {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + kNameGapEm * ImGui::GetFontSize());
}

/// Leaves room for the line with a slider's name and value and puts the cursor where the strip under it starts. Returns where the line is.
ImVec2 reserveNameLine(float width) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
    tuckUnderName();
    return at;
}

/**
 * The strip a finger drags: draws the track and the thumb at `ratio` (0 to 1) and, while a finger is down on it,
 * moves `ratio` to the finger. Returns true when the finger moved it.
 */
bool dragStrip(float width, float* ratio) {
    const float font = ImGui::GetFontSize();
    const float radius = kThumbRadiusEm * font;
    const ImVec2 size(width, kStripEm * font);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##strip", size);
    const bool held = ImGui::IsItemActive();

    // The thumb's centre travels between one radius from each end, so the thumb never leaves the strip.
    const float travelStart = origin.x + radius;
    const float travel = std::max(1.0f, size.x - 2.0f * radius);
    bool moved = false;
    if (held) {
        const float target = std::clamp((ImGui::GetIO().MousePos.x - travelStart) / travel, 0.0f, 1.0f);
        moved = target != *ratio;
        *ratio = target;
    }

    ImDrawList* list = ImGui::GetWindowDrawList();
    const float middle = origin.y + 0.5f * size.y;
    const float halfTrack = 0.5f * kTrackEm * font;
    const float thumbX = travelStart + *ratio * travel;
    list->AddRectFilled(ImVec2(origin.x, middle - halfTrack), ImVec2(origin.x + size.x, middle + halfTrack),
                        ImGui::GetColorU32(ImGuiCol_FrameBgHovered), halfTrack);
    list->AddRectFilled(ImVec2(origin.x, middle - halfTrack), ImVec2(thumbX, middle + halfTrack), ImGui::GetColorU32(ImGuiCol_SliderGrab),
                        halfTrack);
    const ImVec2 thumb(thumbX, middle);
    if (held) {
        list->AddCircleFilled(thumb, kHaloScale * radius, ImGui::GetColorU32(ImGuiCol_SliderGrab, kHaloAlpha));
    }
    list->AddCircleFilled(ImVec2(thumb.x, thumb.y + 0.08f * radius), radius + 1.0f, IM_COL32(0, 0, 0, 70));   // a soft shadow
    list->AddCircleFilled(thumb, radius, ImGui::GetColorU32(ImGuiCol_Text));
    return moved;
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

bool Slider(const char* label, float* value, float min, float max, const char* valueFormat) {
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 line = reserveNameLine(width);
    float ratio = max > min ? std::clamp((*value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
    ImGui::PushID(label);
    const bool moved = dragStrip(width, &ratio);
    ImGui::PopID();

    bool changed = false;
    if (moved) {
        const float next = roundedToFormat(min + ratio * (max - min), valueFormat);
        changed = next != *value;
        *value = next;
    }
    char text[32];
    std::snprintf(text, sizeof(text), valueFormat, static_cast<double>(*value));
    drawNameAndValue(line, width, label, text);   // after the strip, so that the value shown is the new one
    return changed;
}

bool SliderInt(const char* label, int* value, int min, int max, const char* valueFormat) {
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 line = reserveNameLine(width);
    float ratio = max > min ? std::clamp(static_cast<float>(*value - min) / static_cast<float>(max - min), 0.0f, 1.0f) : 0.0f;
    ImGui::PushID(label);
    const bool moved = dragStrip(width, &ratio);
    ImGui::PopID();

    bool changed = false;
    if (moved) {
        const int next = min + static_cast<int>(std::lround(ratio * static_cast<float>(max - min)));
        changed = next != *value;
        *value = next;
    }
    char text[32];
    std::snprintf(text, sizeof(text), valueFormat, *value);
    drawNameAndValue(line, width, label, text);
    return changed;
}

bool Choice(const char* label, int* selected, const char* const* options, int count) {
    ImGui::TextUnformatted(label);
    tuckUnderName();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::PushID(label);
    const bool changed = ImGui::Combo("##choice", selected, options, count);
    ImGui::PopID();
    return changed;
}

}  // namespace aimbuddy::menu
