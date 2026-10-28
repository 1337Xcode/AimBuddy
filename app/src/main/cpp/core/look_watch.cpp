#include "look_watch.h"

#include <algorithm>
#include <cmath>

namespace aimbuddy {

namespace {

/// A finger on the look half moving slower than the first speed is not steering; at the second speed and
/// above it clearly is. Speeds are in display heights a second, so they feel the same on every phone.
constexpr float kSlowestSteeringHeightsPerS = 0.03f;
constexpr float kFullSteeringHeightsPerS = 0.20f;

/// A report older than this is not believed: the helper sends one every few tens of milliseconds while a finger is down.
constexpr int64_t kStaleReportNs = 150'000'000;

/// Time constant of the fall of the answer once the finger stops.
constexpr float kFallS = 0.15f;

/// The longest gap between two calls that is counted, so a stall does not drop the answer to nothing in one go.
constexpr float kLongestStepS = 0.1f;

}  // namespace

void LookWatch::reset() {
    steering_ = 0.0f;
    lastNs_ = 0;
}

float LookWatch::update(int64_t nowNs, const touch::PlayerFingers::Activity& activity, int64_t reportedNs, float anchorX,
                        float displayWidth, float displayHeight) {
    const float stepS = lastNs_ == 0 ? 0.0f : std::clamp(static_cast<float>(nowNs - lastNs_) * 1e-9f, 0.0f, kLongestStepS);
    lastNs_ = nowNs;

    float steeringNow = 0.0f;
    const bool believed = reportedNs > 0 && nowNs - reportedNs <= kStaleReportNs;
    if (believed && displayWidth > 0.0f && displayHeight > 0.0f) {
        const float speedPxS = anchorX < displayWidth * 0.5f ? activity.leftPxS : activity.rightPxS;
        const float heightsPerS = speedPxS / displayHeight;
        steeringNow = std::clamp((heightsPerS - kSlowestSteeringHeightsPerS) /
                                     (kFullSteeringHeightsPerS - kSlowestSteeringHeightsPerS),
                                 0.0f, 1.0f);
    }
    steering_ = std::max(steeringNow, steering_ * std::exp(-stepS / kFallS));
    return steering_;
}

}  // namespace aimbuddy
