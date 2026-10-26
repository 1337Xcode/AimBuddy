#ifndef AIMBUDDY_CAPTURE_ROI_CAPTURE_H
#define AIMBUDDY_CAPTURE_ROI_CAPTURE_H

#include <android/hardware_buffer.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "../core/frame_mailbox.h"
#include "../core/geometry.h"

namespace aimbuddy {

/// The centre region of one captured frame, copied out of the capture buffer.
struct RoiFrame {
    std::vector<uint8_t> rgba;  ///< Tightly packed RGBA, side * side * 4 bytes.
    RoiRect roi;                ///< Where the region sits in the capture image.
    CaptureGeometry geometry;   ///< Grid sizes in effect when the frame was taken.
    int64_t captureTimeNs = 0;  ///< When the frame was on screen (steady clock).
};

/**
 * Copies the detector's region out of each captured frame and publishes it.
 *
 * The capture buffer belongs to the ImageReader and is recycled the moment the Kotlin
 * callback returns, so the pixels are copied synchronously instead of being queued by
 * reference. Only the region the detector needs is touched, a few hundred kilobytes
 * per frame instead of the whole screen.
 */
class RoiCapture {
public:
    /// Sets the grid sizes. Call before frames arrive (capture stopped).
    void configure(const CaptureGeometry& geometry);

    /// Updates only the screen grid, for when the overlay surface changes size.
    void setScreenSize(int width, int height);

    /// Sets the detection-zone radius in screen pixels; takes effect on the next frame.
    void setZoneRadius(float screenPx) { zoneRadius_.store(screenPx, std::memory_order_relaxed); }

    /// The region currently being copied, in capture pixels.
    RoiRect currentRoi() const;

    /// The grid sizes currently in effect.
    CaptureGeometry geometry() const;

    /// Copies the region out of `buffer` and publishes it. Returns false if the frame was skipped.
    bool ingest(AHardwareBuffer* buffer, int64_t imageTimestampNs);

    LatestMailbox<RoiFrame>& mailbox() { return mailbox_; }

private:
    mutable std::mutex geometryMutex_;
    CaptureGeometry geometry_;
    std::atomic<float> zoneRadius_{200.0f};
    LatestMailbox<RoiFrame> mailbox_;
    bool loggedBadFormat_ = false;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CAPTURE_ROI_CAPTURE_H
