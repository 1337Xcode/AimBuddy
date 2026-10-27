#ifndef AIMBUDDY_CORE_GEOMETRY_H
#define AIMBUDDY_CORE_GEOMETRY_H

#include <algorithm>

namespace aimbuddy {

/**
 * Sizes of the two pixel grids the pipeline converts between.
 *
 * The screen grid is the overlay's landscape grid; the capture grid is the ImageReader's.
 * Both have the same aspect ratio, so one scale factor maps either axis.
 */
struct CaptureGeometry {
    int screenWidth = 0;
    int screenHeight = 0;
    int captureWidth = 0;
    int captureHeight = 0;

    bool isValid() const {
        return screenWidth > 0 && screenHeight > 0 && captureWidth > 0 && captureHeight > 0;
    }
    /// Screen pixels per capture pixel.
    float screenPerCapture() const {
        return static_cast<float>(screenWidth) / static_cast<float>(captureWidth);
    }
};

/// Square region of the capture image the detector looks at, in capture pixels.
struct RoiRect {
    int x = 0;
    int y = 0;
    int side = 0;
};

/// The ROI is never smaller than the model input (it would only be upscaled) nor larger than this many times it.
constexpr float kMaxRoiToModelRatio = 1.75f;

/**
 * Chooses the detector's square region from the detection-zone radius (in screen pixels).
 *
 * The side is the zone's diameter converted to capture pixels, kept between the model input size
 * (one capture pixel per model pixel, the sharpest view the model can have) and 1.75 times that
 * (a mild shrink that still keeps distant targets visible). The region is centred on the screen
 * centre and clamped to stay inside the capture image.
 */
inline RoiRect computeRoi(const CaptureGeometry& geometry, float zoneRadiusScreenPx, int modelSize) {
    RoiRect roi;
    if (!geometry.isValid() || modelSize <= 0) {
        return roi;
    }
    const float captureSideF = 2.0f * zoneRadiusScreenPx / geometry.screenPerCapture();
    const int shortEdge = std::min(geometry.captureWidth, geometry.captureHeight);
    const int maxSide = std::min(static_cast<int>(static_cast<float>(modelSize) * kMaxRoiToModelRatio), shortEdge);
    const int minSide = std::min(modelSize, maxSide);
    int side = static_cast<int>(captureSideF + 0.5f);
    side = std::max(minSide, std::min(side, maxSide));
    side &= ~1;  // even, so the centre lands on a pixel boundary
    roi.side = side;
    roi.x = std::max(0, (geometry.captureWidth - side) / 2);
    roi.y = std::max(0, (geometry.captureHeight - side) / 2);
    return roi;
}

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_GEOMETRY_H
