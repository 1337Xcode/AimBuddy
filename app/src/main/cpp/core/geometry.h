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

/// Smallest and largest ROI side. Below the model input size the crop would only be upscaled.
constexpr int kMinRoiSide = 256;
constexpr int kMaxRoiSide = 448;

/**
 * Chooses the detector's square region from the detection-zone radius (in screen pixels).
 * The region is centred on the screen centre and clamped to stay inside the capture image.
 */
inline RoiRect computeRoi(const CaptureGeometry& geometry, float zoneRadiusScreenPx) {
    RoiRect roi;
    if (!geometry.isValid()) {
        return roi;
    }
    const float captureSideF = 2.0f * zoneRadiusScreenPx / geometry.screenPerCapture();
    const int shortEdge = std::min(geometry.captureWidth, geometry.captureHeight);
    const int maxSide = std::min(kMaxRoiSide, shortEdge);
    const int minSide = std::min(kMinRoiSide, maxSide);
    int side = static_cast<int>(captureSideF + 0.5f);
    side = std::max(minSide, std::min(side, maxSide));
    side &= ~1;  // even, so the centre lands on a pixel boundary
    roi.side = side;
    roi.x = std::max(0, std::min((geometry.captureWidth - side) / 2, geometry.captureWidth - side));
    roi.y = std::max(0, std::min((geometry.captureHeight - side) / 2, geometry.captureHeight - side));
    return roi;
}

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_GEOMETRY_H
