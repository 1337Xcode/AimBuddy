#ifndef AIMBUDDY_CORE_DETECTION_BATCH_H
#define AIMBUDDY_CORE_DETECTION_BATCH_H

#include <cstdint>

#include "tracker.h"

namespace aimbuddy {

/**
 * Everything the detector learned from one frame, in screen pixels.
 *
 * It is a plain fixed-size value so it can be copied between threads without allocating.
 */
struct DetectionBatch {
    Detection detections[kMaxDetections];
    int count = 0;
    ScanRegion scanned;          ///< The part of the screen the detector examined.
    int64_t captureNs = 0;       ///< When the frame was on screen (steady clock).
    float inferenceMs = 0.0f;    ///< Time spent in preprocessing, the network and decoding.
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_DETECTION_BATCH_H
