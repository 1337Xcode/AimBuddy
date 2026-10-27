#include "target_overlay.h"

#include <algorithm>
#include <cstdio>

namespace aimbuddy {

namespace {

/// Darkness of the halo under each outline.
constexpr int kHaloAlpha = 170;

/// Extra width of the halo on each side of the outline, in pixels.
constexpr float kHaloExtra = 1.5f;

/// A locked target is drawn this much thicker than the others.
constexpr float kLockedThicknessScale = 1.6f;

/// Longest corner bracket arm as a fraction of the shorter box side, and its hard limits in pixels.
constexpr float kBracketFraction = 0.28f;
constexpr float kBracketMinPx = 6.0f;
constexpr float kBracketMaxPx = 40.0f;

/// The same colour with its alpha scaled by `opacity` (0 to 1).
ImU32 withOpacity(ImU32 color, float opacity) {
    const float alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(opacity, 0.0f, 1.0f);
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

ImU32 haloColor(float opacity) { return IM_COL32(0, 0, 0, static_cast<int>(kHaloAlpha * opacity)); }

/// One line drawn twice: a wide dark halo, then the coloured line on top.
void haloLine(ImDrawList* list, ImVec2 from, ImVec2 to, ImU32 color, float thickness, float opacity) {
    list->AddLine(from, to, haloColor(opacity), thickness + 2.0f * kHaloExtra);
    list->AddLine(from, to, color, thickness);
}

/// A full rectangle outline with a halo.
void haloRect(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 color, float thickness, float opacity) {
    list->AddRect(min, max, haloColor(opacity), 0.0f, 0, thickness + 2.0f * kHaloExtra);
    list->AddRect(min, max, color, 0.0f, 0, thickness);
}

/// Only the four corners of a rectangle, each as two short arms.
void haloBrackets(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 color, float thickness, float opacity) {
    const float shorter = std::min(max.x - min.x, max.y - min.y);
    const float arm = std::clamp(shorter * kBracketFraction, kBracketMinPx, kBracketMaxPx);
    haloLine(list, min, ImVec2(min.x + arm, min.y), color, thickness, opacity);
    haloLine(list, min, ImVec2(min.x, min.y + arm), color, thickness, opacity);
    haloLine(list, ImVec2(max.x, min.y), ImVec2(max.x - arm, min.y), color, thickness, opacity);
    haloLine(list, ImVec2(max.x, min.y), ImVec2(max.x, min.y + arm), color, thickness, opacity);
    haloLine(list, ImVec2(min.x, max.y), ImVec2(min.x + arm, max.y), color, thickness, opacity);
    haloLine(list, ImVec2(min.x, max.y), ImVec2(min.x, max.y - arm), color, thickness, opacity);
    haloLine(list, max, ImVec2(max.x - arm, max.y), color, thickness, opacity);
    haloLine(list, max, ImVec2(max.x, max.y - arm), color, thickness, opacity);
}

/// A small diamond marking the aim point, dark underneath so it reads on any background.
void aimMarker(ImDrawList* list, float x, float y, ImU32 color, float radius, float opacity) {
    const ImVec2 outer[4] = {{x, y - radius}, {x + radius, y}, {x, y + radius}, {x - radius, y}};
    list->AddConvexPolyFilled(outer, 4, haloColor(opacity));
    const float inner = radius - 1.5f;
    const ImVec2 filled[4] = {{x, y - inner}, {x + inner, y}, {x, y + inner}, {x - inner, y}};
    list->AddConvexPolyFilled(filled, 4, color);
}

}  // namespace

void TargetOverlay::draw(ImDrawList* list, const TrackView* views, int count, const ModelProfile& profile,
                         const TargetStyle& style) {
    for (int i = 0; i < count; ++i) {
        const TrackView& body = views[i];
        if (profile.roleOf(body.classId) != ClassRole::kBody || body.opacity <= 0.0f) {
            continue;
        }
        const bool locked = body.id == style.lockedTrackId;
        const float thickness = locked ? style.thickness * kLockedThicknessScale : style.thickness;
        const ImU32 color = withOpacity(style.color, body.opacity);
        const float inset = style.gap + thickness * 0.5f;

        // Targets are in display pixels; the overlay window may start a few pixels in from the display corner.
        const float centerX = body.centerX - style.origin.x;
        const float centerY = body.centerY - style.origin.y;
        const ImVec2 min(centerX - body.width * 0.5f - inset, centerY - body.height * 0.5f - inset);
        const ImVec2 max(centerX + body.width * 0.5f + inset, centerY + body.height * 0.5f + inset);
        if (style.cornerBrackets) {
            haloBrackets(list, min, max, color, thickness, body.opacity);
        } else {
            haloRect(list, min, max, color, thickness, body.opacity);
        }

        if (style.showAimPoint) {
            const AimPoint aim = chooseAimPoint(body, findHeadFor(body, views, count, profile), style.aimZone);
            aimMarker(list, aim.x - style.origin.x, aim.y - style.origin.y, color, locked ? 7.0f : 5.0f, body.opacity);
        }
        if (style.showScore) {
            char label[16];
            std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(body.score * 100.0f + 0.5f));
            const ImVec2 size = ImGui::CalcTextSize(label);
            const ImVec2 position(centerX - size.x * 0.5f, min.y - size.y - 3.0f);
            list->AddText(ImVec2(position.x + 1.0f, position.y + 1.0f), withOpacity(IM_COL32(0, 0, 0, 220), body.opacity), label);
            list->AddText(position, color, label);
        }
    }
}

}  // namespace aimbuddy
