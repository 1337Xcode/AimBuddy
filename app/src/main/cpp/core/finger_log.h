#ifndef AIMBUDDY_CORE_FINGER_LOG_H
#define AIMBUDDY_CORE_FINGER_LOG_H

#include <algorithm>
#include <array>
#include <cstdint>

namespace aimbuddy {

/**
 * A short record of how far the assist dragged its finger, and when.
 *
 * The game turns the camera some time after a drag, so what the screen shows now still lacks the
 * last few moves. Keeping the moves lets the rest of the app work out what is on its way, and
 * lets the response estimator compare each drag with the slide it caused.
 *
 * A drag is spread evenly over the time one control step takes, because the game applies a
 * finger move over a frame or two rather than at one instant. Not thread safe: callers lock.
 */
class FingerLog {
public:
    /// Notes that the finger moved by (dx, dy) pixels, evenly between sentNs and sentNs + spreadNs.
    void record(int64_t sentNs, int64_t spreadNs, float dx, float dy) {
        // A full record forgets its oldest entry first.
        if (count_ == kCapacity) {
            head_ = (head_ + 1) % kCapacity;
            --count_;
        }
        entries_[(head_ + count_) % kCapacity] = {sentNs, std::max<int64_t>(spreadNs, 0), dx, dy};
        ++count_;
    }

    /**
     * The finger travel whose effect reaches the screen inside (fromNs, toNs], if the camera needs
     * delayS seconds to answer. Each drag counts for the share of its spread that falls in the window.
     */
    void landedBetween(float delayS, int64_t fromNs, int64_t toNs, float* dx, float* dy) const {
        const int64_t delayNs = static_cast<int64_t>(delayS * 1e9f);
        float sumX = 0.0f;
        float sumY = 0.0f;
        for (int i = 0; i < count_; ++i) {
            const Entry& entry = entries_[(head_ + i) % kCapacity];
            const float share = overlapShare(entry, delayNs, fromNs, toNs);
            sumX += entry.dx * share;
            sumY += entry.dy * share;
        }
        *dx = sumX;
        *dy = sumY;
    }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

    bool empty() const { return count_ == 0; }

private:
    struct Entry {
        int64_t sentNs;
        int64_t spreadNs;
        float dx;
        float dy;
    };

    /// Part of the entry's movement that lands inside (fromNs, toNs].
    static float overlapShare(const Entry& entry, int64_t delayNs, int64_t fromNs, int64_t toNs) {
        const int64_t start = entry.sentNs + delayNs;
        if (entry.spreadNs == 0) {
            return (start > fromNs && start <= toNs) ? 1.0f : 0.0f;   // an instant movement
        }
        const int64_t overlapStart = std::max(fromNs, start);
        const int64_t overlapEnd = std::min(toNs, start + entry.spreadNs);
        if (overlapEnd <= overlapStart) {
            return 0.0f;
        }
        return static_cast<float>(overlapEnd - overlapStart) / static_cast<float>(entry.spreadNs);
    }

    /// About two seconds of history at the 125 Hz control rate.
    static constexpr int kCapacity = 256;

    std::array<Entry, kCapacity> entries_{};
    int head_ = 0;
    int count_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_FINGER_LOG_H
