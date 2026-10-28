#include "player_fingers.h"

#include <algorithm>
#include <cmath>

namespace aimbuddy::touch {

namespace {

/// Time constant of the smoothing of a finger's speed: it follows a change within about two of these.
constexpr float kSmoothingS = 0.05f;

constexpr float kSecondsPerNs = 1e-9f;

}  // namespace

void PlayerFingers::reset() {
    trackCount_ = 0;
    activity_ = Activity();
}

/// The track a finger had at the last update, or nullptr for a finger that has only just come down.
const PlayerFingers::Track* PlayerFingers::findTrack(int32_t id) const {
    for (int t = 0; t < trackCount_; ++t) {
        if (tracks_[t].id == id) {
            return &tracks_[t];
        }
    }
    return nullptr;
}

/// Moves a track on to where its finger is now, bringing its speed towards the one the move shows.
PlayerFingers::Track PlayerFingers::advance(const Track& before, const Finger& finger, int64_t nowNs) {
    const int64_t spanNs = nowNs - before.sinceNs;
    if (spanNs <= 0) {
        return before;   // the same instant again: nothing has had time to move
    }
    const float seconds = static_cast<float>(spanNs) * kSecondsPerNs;
    const float instantPxS = std::hypot(finger.x - before.x, finger.y - before.y) / seconds;
    Track track = before;
    track.x = finger.x;
    track.y = finger.y;
    track.sinceNs = nowNs;
    track.speedPxS = before.speedPxS + (instantPxS - before.speedPxS) * (1.0f - std::exp(-seconds / kSmoothingS));
    return track;
}

void PlayerFingers::update(int64_t nowNs, const Finger* fingers, int count, float displayWidth) {
    std::array<Track, kMaxFingers> next{};
    int nextCount = 0;
    for (int i = 0; i < count && nextCount < kMaxFingers; ++i) {
        const Finger& finger = fingers[i];
        const Track* before = findTrack(finger.id);
        next[nextCount++] = before != nullptr ? advance(*before, finger, nowNs) : Track{finger.id, finger.x, finger.y, nowNs, 0.0f};
    }
    tracks_ = next;
    trackCount_ = nextCount;

    activity_ = Activity();
    activity_.fingersDown = trackCount_;
    for (int t = 0; t < trackCount_; ++t) {
        float& quickest = tracks_[t].x < displayWidth * 0.5f ? activity_.leftPxS : activity_.rightPxS;
        quickest = std::max(quickest, tracks_[t].speedPxS);
    }
}

}  // namespace aimbuddy::touch
