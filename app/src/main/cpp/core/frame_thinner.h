#ifndef AIMBUDDY_CORE_FRAME_THINNER_H
#define AIMBUDDY_CORE_FRAME_THINNER_H

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace aimbuddy {

/**
 * Picks which captured frames to detect when the phone asks for fewer detections a second, for
 * example because it is getting hot. A frame that comes too soon after the last one picked is
 * skipped, not held back, so a frame that is detected is detected the moment it arrives.
 *
 * The frames picked keep to a schedule of one every period, so the rate comes out right whatever
 * the screen's frame rate is (30, 60 or 120 Hz) and however much the frame times jitter.
 */
class FrameThinner {
public:
    /// Sets the most detections a second to allow. Zero or less allows every frame. Thread safe.
    void setMaxRate(float perSecond) { maxRate_.store(perSecond, std::memory_order_relaxed); }

    /// True if the frame taken at `captureNs` is to be detected. Call it once per frame, in order, from one thread.
    bool accept(int64_t captureNs) {
        const float rate = maxRate_.load(std::memory_order_relaxed);
        if (rate <= 0.0f) {
            nextNs_ = 0;
            return true;
        }
        const int64_t period = static_cast<int64_t>(1e9f / rate);
        const int64_t early = period / kEarlyDivisor;   // a frame may come a little before its turn
        // A due time far ahead of the clock means the clock went back, so the schedule starts over.
        const bool clockWentBack = nextNs_ - captureNs > kBackJumpPeriods * period;
        if (captureNs + early < nextNs_ && !clockWentBack) {
            return false;
        }
        // The next turn is a period after this one's. After a long gap it is a period from now,
        // so the time that passed is not made up for with a burst of frames.
        nextNs_ = (clockWentBack ? captureNs : std::max(nextNs_, captureNs - early)) + period;
        return true;
    }

private:
    static constexpr int64_t kEarlyDivisor = 4;
    static constexpr int64_t kBackJumpPeriods = 2;

    std::atomic<float> maxRate_{0.0f};
    int64_t nextNs_ = 0;   ///< When the next frame is due, on the capture clock.
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_FRAME_THINNER_H
