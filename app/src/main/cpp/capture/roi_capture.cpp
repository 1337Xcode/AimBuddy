#include "roi_capture.h"

#include <chrono>
#include <cstring>

#include "../utils/logger.h"

namespace aimbuddy {

namespace {

/// Image timestamps older than this are ignored and the arrival time is used instead.
constexpr int64_t kMaxPlausibleFrameAgeNs = 500'000'000;

int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void RoiCapture::configure(const CaptureGeometry& geometry) {
    std::lock_guard<std::mutex> lock(geometryMutex_);
    geometry_ = geometry;
}

void RoiCapture::setScreenSize(int width, int height) {
    std::lock_guard<std::mutex> lock(geometryMutex_);
    geometry_.screenWidth = width;
    geometry_.screenHeight = height;
}

CaptureGeometry RoiCapture::geometry() const {
    std::lock_guard<std::mutex> lock(geometryMutex_);
    return geometry_;
}

RoiRect RoiCapture::currentRoi() const {
    return computeRoi(geometry(), zoneRadius_.load(std::memory_order_relaxed),
                      modelSize_.load(std::memory_order_relaxed));
}

bool RoiCapture::ingest(AHardwareBuffer* buffer, int64_t imageTimestampNs) {
    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(buffer, &desc);
    if (desc.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM) {
        if (!loggedBadFormat_) {
            LOGE("Unsupported capture buffer format %u", desc.format);
            loggedBadFormat_ = true;
        }
        return false;
    }

    // The buffer is the source of truth for the capture size; the screen size comes from configure().
    CaptureGeometry geometry = this->geometry();
    geometry.captureWidth = static_cast<int>(desc.width);
    geometry.captureHeight = static_cast<int>(desc.height);
    const RoiRect roi = computeRoi(geometry, zoneRadius_.load(std::memory_order_relaxed),
                                   modelSize_.load(std::memory_order_relaxed));
    if (roi.side <= 0) {
        return false;
    }

    ARect lockRect;
    lockRect.left = roi.x;
    lockRect.top = roi.y;
    lockRect.right = roi.x + roi.side;
    lockRect.bottom = roi.y + roi.side;
    void* pixels = nullptr;
    if (AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, &lockRect, &pixels) != 0 ||
        pixels == nullptr) {
        LOGE("Failed to lock capture buffer");
        return false;
    }

    RoiFrame& frame = mailbox_.beginWrite();
    const size_t sideBytes = static_cast<size_t>(roi.side) * 4;
    frame.rgba.resize(sideBytes * static_cast<size_t>(roi.side));
    const size_t strideBytes = static_cast<size_t>(desc.stride) * 4;
    const uint8_t* source = static_cast<const uint8_t*>(pixels) +
                            static_cast<size_t>(roi.y) * strideBytes + static_cast<size_t>(roi.x) * 4;
    uint8_t* target = frame.rgba.data();
    for (int row = 0; row < roi.side; ++row) {
        std::memcpy(target + row * sideBytes, source + row * strideBytes, sideBytes);
    }
    AHardwareBuffer_unlock(buffer, nullptr);

    const int64_t now = steadyNowNs();
    const bool timestampUsable = imageTimestampNs > 0 && imageTimestampNs <= now &&
                                 now - imageTimestampNs < kMaxPlausibleFrameAgeNs;
    frame.roi = roi;
    frame.geometry = geometry;
    frame.captureTimeNs = timestampUsable ? imageTimestampNs : now;
    mailbox_.publish();
    // The detector starts on the frame now, and what the observer does with it (the camera look, which can take
    // several milliseconds) runs alongside, not in front. The slot is not written again before this thread asks for
    // the next frame, so the observer can read it while the detector does.
    if (observer_) {
        observer_(frame);
    }
    return true;
}

}  // namespace aimbuddy
