#ifndef AIMBUDDY_CORE_MEASURED_MOTION_H
#define AIMBUDDY_CORE_MEASURED_MOTION_H

#include <algorithm>
#include <array>
#include <cstdint>

namespace aimbuddy {

/**
 * How far the whole scene was seen to slide between captured frames.
 *
 * Each entry covers the time between two frames and says how far the scene moved across the screen
 * in it, whoever turned the camera: the assist's finger, the player's, or the game itself. Unlike
 * the record of the assist's own drags it needs no knowledge of the game's sensitivity.
 *
 * Entries must be added in time order and are assumed to be contiguous. Not thread safe: callers lock.
 */
class MeasuredMotion {
public:
    /**
     * Adds the slide seen between two frames, spread evenly over the interval. `measured` is false
     * for a slide that was not seen but assumed, because the frames could not be compared.
     */
    void add(int64_t fromNs, int64_t toNs, float dx, float dy, bool measured = true) {
        if (toNs <= fromNs) {
            return;
        }
        if (count_ == kCapacity) {
            head_ = (head_ + 1) % kCapacity;
            --count_;
        }
        entries_[(head_ + count_) % kCapacity] = {fromNs, toNs, dx, dy, measured};
        ++count_;
    }

    /**
     * The net slide of the scene that was really seen inside (fromNs, toNs], and how much of that
     * time was covered by frames that could be compared. Slides that were only assumed count for neither.
     */
    void seenShiftBetween(int64_t fromNs, int64_t toNs, float* dx, float* dy, int64_t* coveredNs) const {
        float sumX = 0.0f;
        float sumY = 0.0f;
        int64_t covered = 0;
        for (int i = 0; i < count_; ++i) {
            const Entry& entry = entries_[(head_ + i) % kCapacity];
            const int64_t overlapStart = std::max(fromNs, entry.fromNs);
            const int64_t overlapEnd = std::min(toNs, entry.toNs);
            if (overlapEnd <= overlapStart || !entry.measured) {
                continue;
            }
            const float share = static_cast<float>(overlapEnd - overlapStart) / static_cast<float>(entry.toNs - entry.fromNs);
            sumX += entry.dx * share;
            sumY += entry.dy * share;
            covered += overlapEnd - overlapStart;
        }
        *dx = sumX;
        *dy = sumY;
        *coveredNs = covered;
    }

    /// The slide inside (fromNs, toNs]. Time past the newest entry contributes nothing.
    void shiftBetween(int64_t fromNs, int64_t toNs, float* dx, float* dy) const {
        float sumX = 0.0f;
        float sumY = 0.0f;
        for (int i = 0; i < count_; ++i) {
            const Entry& entry = entries_[(head_ + i) % kCapacity];
            const int64_t overlapStart = std::max(fromNs, entry.fromNs);
            const int64_t overlapEnd = std::min(toNs, entry.toNs);
            if (overlapEnd <= overlapStart) {
                continue;
            }
            const float share = static_cast<float>(overlapEnd - overlapStart) / static_cast<float>(entry.toNs - entry.fromNs);
            sumX += entry.dx * share;
            sumY += entry.dy * share;
        }
        *dx = sumX;
        *dy = sumY;
    }

    /// End of the newest entry, or 0 when there is none.
    int64_t latestNs() const { return count_ == 0 ? 0 : entries_[(head_ + count_ - 1) % kCapacity].toNs; }

    bool empty() const { return count_ == 0; }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

private:
    struct Entry {
        int64_t fromNs;
        int64_t toNs;
        float dx;
        float dy;
        bool measured;
    };

    /// About two seconds of history at 60 frames a second.
    static constexpr int kCapacity = 128;

    std::array<Entry, kCapacity> entries_{};
    int head_ = 0;
    int count_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_MEASURED_MOTION_H
