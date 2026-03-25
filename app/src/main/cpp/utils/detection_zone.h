#ifndef DETECTION_ZONE_H
#define DETECTION_ZONE_H

#include <algorithm>
#include "../settings.h"

namespace ESP {

struct DetectionZoneMetrics {
    float halfWidthPx{0.0f};
    float halfHeightPx{0.0f};
    int effectiveCropSize{Config::CROP_SIZE};
};

inline DetectionZoneMetrics ComputeDetectionZoneMetrics(
    float detectionFovRadius,
    int logicalScreenWidth,
    float displayWidth,
    float displayHeight,
    int captureWidth,
    int captureHeight
) {
    DetectionZoneMetrics metrics{};

    if (detectionFovRadius <= 0.0f || displayWidth <= 0.0f || displayHeight <= 0.0f) {
        return metrics;
    }

    const int safeLogicalScreenWidth = std::max(1, logicalScreenWidth);
    const int safeCaptureWidth = std::max(1, captureWidth);
    const int safeCaptureHeight = std::max(1, captureHeight);

    int targetSize = static_cast<int>(detectionFovRadius * 2.0f);
    targetSize = std::max(256, std::min(targetSize, safeLogicalScreenWidth));

    int effectiveCropSize = static_cast<int>(
        targetSize * (static_cast<float>(safeCaptureWidth) / static_cast<float>(safeLogicalScreenWidth))
    );
    effectiveCropSize = std::max(256, std::min(effectiveCropSize, Config::CROP_SIZE));

    metrics.effectiveCropSize = effectiveCropSize;
    metrics.halfWidthPx = 0.5f * static_cast<float>(effectiveCropSize) *
        (displayWidth / static_cast<float>(safeCaptureWidth));
    metrics.halfHeightPx = 0.5f * static_cast<float>(effectiveCropSize) *
        (displayHeight / static_cast<float>(safeCaptureHeight));

    return metrics;
}

} // namespace ESP

#endif // DETECTION_ZONE_H
