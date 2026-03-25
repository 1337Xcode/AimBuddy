#ifndef ESP_IMGUI_HELPER_H
#define ESP_IMGUI_HELPER_H

#include "../imgui/imgui.h"
#include <string>
#include <cstdarg>

namespace ESP {
namespace ImGuiHelper {

/**
 * @brief Convert hex color to ImVec4
 * @param hex Color in 0xRRGGBB format
 * @param alpha Alpha channel (0-1)
 * @return ImVec4 color
 */
inline ImVec4 HexToVec4(int hex, float alpha = 1.0f) {
    int r = (hex >> 16) & 0xFF;
    int g = (hex >> 8) & 0xFF;
    int b = (hex >> 0) & 0xFF;
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, alpha);
}

/**
 * @brief Convert RGBA to ImVec4
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 * @param a Alpha (0-255)
 * @return ImVec4 color
 */
inline ImVec4 RGBAToVec4(float r, float g, float b, float a = 255.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

/**
 * @brief Draw filled circle at position
 * @param drawList ImGui draw list
 * @param center Center position
 * @param radius Radius in pixels
 * @param color Fill color
 * @param segments Number of segments (0 = auto)
 */
inline void DrawCircleFilled(ImDrawList* drawList, const ImVec2& center, float radius, ImU32 color, int segments = 0) {
    if (!drawList) return;
    if (segments == 0) {
        segments = static_cast<int>(radius * 0.5f);
        segments = std::max(12, std::min(segments, 64));
    }
    drawList->AddCircleFilled(center, radius, color, segments);
}

/**
 * @brief Draw box corners
 * @param drawList ImGui draw list
 * @param topLeft Top-left corner
 * @param bottomRight Bottom-right corner
 * @param color Line color
 * @param thickness Line thickness
 * @param cornerLength Length of corner lines
 */
inline void DrawBoxCorners(ImDrawList* drawList, const ImVec2& topLeft, const ImVec2& bottomRight, 
                           ImU32 color, float thickness = 2.0f, float cornerLength = 15.0f) {
    if (!drawList) return;

    // Top-left
    drawList->AddLine(ImVec2(topLeft.x, topLeft.y), ImVec2(topLeft.x + cornerLength, topLeft.y), color, thickness);
    drawList->AddLine(ImVec2(topLeft.x, topLeft.y), ImVec2(topLeft.x, topLeft.y + cornerLength), color, thickness);

    // Top-right
    drawList->AddLine(ImVec2(bottomRight.x, topLeft.y), ImVec2(bottomRight.x - cornerLength, topLeft.y), color, thickness);
    drawList->AddLine(ImVec2(bottomRight.x, topLeft.y), ImVec2(bottomRight.x, topLeft.y + cornerLength), color, thickness);

    // Bottom-left
    drawList->AddLine(ImVec2(topLeft.x, bottomRight.y), ImVec2(topLeft.x + cornerLength, bottomRight.y), color, thickness);
    drawList->AddLine(ImVec2(topLeft.x, bottomRight.y), ImVec2(topLeft.x, bottomRight.y - cornerLength), color, thickness);

    // Bottom-right
    drawList->AddLine(ImVec2(bottomRight.x, bottomRight.y), ImVec2(bottomRight.x - cornerLength, bottomRight.y), color, thickness);
    drawList->AddLine(ImVec2(bottomRight.x, bottomRight.y), ImVec2(bottomRight.x, bottomRight.y - cornerLength), color, thickness);
}

/**
 * @brief Draw 3D-style box with shadow
 * @param drawList ImGui draw list
 * @param topLeft Top-left corner
 * @param bottomRight Bottom-right corner
 * @param color Main color
 * @param thickness Line thickness
 * @param shadowColor Shadow color (optional)
 */
inline void DrawBox3D(ImDrawList* drawList, const ImVec2& topLeft, const ImVec2& bottomRight,
                      ImU32 color, float thickness = 2.0f, ImU32 shadowColor = 0) {
    if (!drawList) return;

    // Draw shadow if provided
    if (shadowColor != 0) {
        ImVec2 shadowOffset(2.0f, 2.0f);
        drawList->AddRect(
            ImVec2(topLeft.x + shadowOffset.x, topLeft.y + shadowOffset.y),
            ImVec2(bottomRight.x + shadowOffset.x, bottomRight.y + shadowOffset.y),
            shadowColor, 0.0f, 0, thickness
        );
    }

    // Draw main box
    drawList->AddRect(topLeft, bottomRight, color, 0.0f, 0, thickness);
}

/**
 * @brief Draw text with shadow/outline
 * @param drawList ImGui draw list
 * @param pos Text position
 * @param color Text color
 * @param text Text string
 * @param shadowColor Shadow color (optional)
 * @param shadowOffset Shadow offset
 */
inline void DrawTextWithShadow(ImDrawList* drawList, const ImVec2& pos, ImU32 color, 
                               const char* text, ImU32 shadowColor = IM_COL32(0, 0, 0, 180), 
                               const ImVec2& shadowOffset = ImVec2(1.0f, 1.0f)) {
    if (!drawList || !text) return;

    // Draw shadow
    if (shadowColor != 0) {
        drawList->AddText(ImVec2(pos.x + shadowOffset.x, pos.y + shadowOffset.y), shadowColor, text);
    }

    // Draw main text
    drawList->AddText(pos, color, text);
}

/**
 * @brief Format string (similar to sprintf)
 * @param fmt Format string
 * @param ... Arguments
 * @return Formatted string
 */
inline std::string Format(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return std::string(buffer);
}

/**
 * @brief Clamp value to range
 * @param v Value
 * @param min Minimum
 * @param max Maximum
 * @return Clamped value
 */
template<typename T>
inline T Clamp(T v, T min, T max) {
    return (v < min) ? min : (v > max) ? max : v;
}

} // namespace ImGuiHelper
} // namespace ESP

#endif // ESP_IMGUI_HELPER_H
