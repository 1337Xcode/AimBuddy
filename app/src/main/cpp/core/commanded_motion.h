#ifndef AIMBUDDY_CORE_COMMANDED_MOTION_H
#define AIMBUDDY_CORE_COMMANDED_MOTION_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>

#include "ego_motion.h"

namespace aimbuddy {

/**
 * The camera movement the assist itself has asked for.
 *
 * When the assist drags its finger, the game turns the camera a moment later and the whole
 * scene slides across the screen. This keeps a short record of every such request, each with
 * the time its effect reaches the screen, so the tracker can separate that slide from a
 * target's own movement and the controller can avoid correcting the same error twice.
 *
 * A movement is spread evenly over a time interval, because a finger move is applied over one
 * or more game frames, not at a single instant.
 */
class CommandedMotion final : public EgoMotion {
public:
    /// Notes that the scene will slide by (dx, dy) pixels, evenly, between startNs and endNs.
    void record(int64_t startNs, int64_t endNs, float dx, float dy) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Drop entries too old to matter before reusing the oldest slot of a full record.
        if (count_ == kCapacity) {
            head_ = (head_ + 1) % kCapacity;
            --count_;
        }
        entries_[(head_ + count_) % kCapacity] = {startNs, std::max(endNs, startNs), dx, dy};
        ++count_;
    }

    void shiftBetween(int64_t fromNs, int64_t toNs, float* dx, float* dy) const override {
        float sumX = 0.0f;
        float sumY = 0.0f;
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < count_; ++i) {
            const Entry& e = entries_[(head_ + i) % kCapacity];
            const float fraction = overlapFraction(e, fromNs, toNs);
            sumX += e.dx * fraction;
            sumY += e.dy * fraction;
        }
        *dx = sumX;
        *dy = sumY;
    }

    /// Forgets everything, for example when aiming restarts.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        count_ = 0;
    }

private:
    struct Entry {
        int64_t startNs;
        int64_t endNs;
        float dx;
        float dy;
    };

    /// Part of the entry's movement that falls inside (fromNs, toNs].
    static float overlapFraction(const Entry& e, int64_t fromNs, int64_t toNs) {
        if (e.endNs == e.startNs) {
            return (e.startNs > fromNs && e.startNs <= toNs) ? 1.0f : 0.0f;  // an instant movement
        }
        const int64_t overlapStart = std::max(fromNs, e.startNs);
        const int64_t overlapEnd = std::min(toNs, e.endNs);
        if (overlapEnd <= overlapStart) {
            return 0.0f;
        }
        return static_cast<float>(overlapEnd - overlapStart) / static_cast<float>(e.endNs - e.startNs);
    }

    /// Roughly one second of history at a 120 Hz control rate.
    static constexpr int kCapacity = 128;

    mutable std::mutex mutex_;
    std::array<Entry, kCapacity> entries_{};
    int head_ = 0;
    int count_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_COMMANDED_MOTION_H
