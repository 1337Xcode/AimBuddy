#ifndef AIMBUDDY_CORE_SCREEN_MAPPING_H
#define AIMBUDDY_CORE_SCREEN_MAPPING_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aimbuddy::touch {

/// Value range a touchscreen reports for each axis, in its own raw units.
struct RawRange {
    int32_t minX = 0;
    int32_t maxX = 0;
    int32_t minY = 0;
    int32_t maxY = 0;
};

/// A position in the touchscreen's raw units.
struct RawPoint {
    int32_t x = 0;
    int32_t y = 0;
};

/**
 * Turns a position on the display into a position in the touchscreen's raw units.
 *
 * The touchscreen reports in the orientation the panel was built in, which for a phone is
 * portrait. The display, and everything the app draws and measures, is in the orientation the
 * player currently sees. `rotation` says how the two differ, as Android's Display.getRotation()
 * numbers it: 0 upright, 1 turned a quarter counter-clockwise, 2 upside down, 3 a quarter
 * clockwise. `displayWidth` and `displayHeight` are the display's size in that orientation, so
 * they swap with a quarter turn.
 *
 * The result follows the same geometry Android uses to turn raw touches into display positions,
 * run backwards, so a touch sent here lands where it was meant to.
 */
inline RawPoint displayToRaw(float x, float y, int displayWidth, int displayHeight, int rotation,
                             const RawRange& range) {
    const float u = std::clamp(x / static_cast<float>(std::max(displayWidth, 1)), 0.0f, 1.0f);
    const float v = std::clamp(y / static_cast<float>(std::max(displayHeight, 1)), 0.0f, 1.0f);

    // Position as a fraction along the panel's own width (a) and height (b).
    float a = u;
    float b = v;
    switch (((rotation % 4) + 4) % 4) {
        case 1:
            a = 1.0f - v;
            b = u;
            break;
        case 2:
            a = 1.0f - u;
            b = 1.0f - v;
            break;
        case 3:
            a = v;
            b = 1.0f - u;
            break;
        default:
            break;
    }
    RawPoint point;
    point.x = range.minX + static_cast<int32_t>(std::lround(a * static_cast<float>(range.maxX - range.minX)));
    point.y = range.minY + static_cast<int32_t>(std::lround(b * static_cast<float>(range.maxY - range.minY)));
    return point;
}

/// A position on the display, in pixels.
struct DisplayPoint {
    float x = 0.0f;
    float y = 0.0f;
};

/// The way back from displayToRaw: where on the display a touch the screen reported in raw units is.
inline DisplayPoint rawToDisplay(int32_t rawX, int32_t rawY, int displayWidth, int displayHeight, int rotation,
                                 const RawRange& range) {
    const float spanX = static_cast<float>(std::max(range.maxX - range.minX, 1));
    const float spanY = static_cast<float>(std::max(range.maxY - range.minY, 1));
    const float a = std::clamp(static_cast<float>(rawX - range.minX) / spanX, 0.0f, 1.0f);
    const float b = std::clamp(static_cast<float>(rawY - range.minY) / spanY, 0.0f, 1.0f);

    // Position as a fraction along the display's own width (u) and height (v): displayToRaw's turn, undone.
    float u = a;
    float v = b;
    switch (((rotation % 4) + 4) % 4) {
        case 1:
            u = b;
            v = 1.0f - a;
            break;
        case 2:
            u = 1.0f - a;
            v = 1.0f - b;
            break;
        case 3:
            u = 1.0f - b;
            v = a;
            break;
        default:
            break;
    }
    return {u * static_cast<float>(displayWidth), v * static_cast<float>(displayHeight)};
}

}  // namespace aimbuddy::touch

#endif  // AIMBUDDY_CORE_SCREEN_MAPPING_H
