#include "aim_controller.h"

#include <algorithm>
#include <cmath>

namespace aimbuddy {

namespace {

/// Step length assumed for the first step, and the range a measured step is clamped to.
constexpr float kFirstStepS = 1.0f / 90.0f;
constexpr float kMinStepS = 0.002f;
constexpr float kMaxStepS = 0.05f;

/// A target must be at least this visible (opacity) to be followed.
constexpr float kMinTargetOpacity = 0.6f;

/// Once locked, a target may drift this much further out than the field of view before it is dropped.
constexpr float kLockedFovSlack = 1.2f;

/// Time constants of the approach to the target: slowest at strength 0, fastest at strength 1.
constexpr float kSlowestTimeConstantS = 0.20f;
constexpr float kFastestTimeConstantS = 0.025f;

/// Finger speed limit at strength 0, as a fraction of the limit at strength 1.
constexpr float kMinSpeedShare = 0.4f;

/// The speed estimate is only believed when it is this many times larger than its own uncertainty;
/// the feed-forward fades in between the first and second value.
constexpr float kFeedForwardMinSnr = 1.0f;
constexpr float kFeedForwardFullSnr = 2.5f;

/// A target must have been matched this many times before its speed is trusted for feed-forward.
constexpr int kMinHitsForFeedForward = 4;

/// Size of the dead zone around a clearly moving target, as a fraction of the resting dead zone.
constexpr float kMovingDeadzoneShare = 0.3f;

/// When the finger is further than this share of its radius from the anchor, it is put back
/// at a moment when that will not cost a correction.
constexpr float kRecentreShare = 0.7f;

/// A re-centre only waits for a quiet moment while the error is below this fraction of the target's height.
constexpr float kQuietErrorBoxFraction = 0.25f;

/// Finger travel allowed to be on its way (moved but not yet visible on screen) is a small base
/// amount plus a part that grows steeply with how well the camera's response is known.
constexpr float kMinTravelCapPx = 8.0f;
constexpr float kTravelCapRangePx = 300.0f;

/// Swing detection: an error counts as on one side of the target beyond this many dead zones (or
/// this fraction of the target's height), and a swing is this many crossings within the window.
constexpr float kSwingMarginDeadzones = 3.0f;
constexpr float kSwingMarginBoxFraction = 0.10f;
constexpr int kSwingCrossings = 3;
constexpr float kSwingWindowS = 0.7f;

/// A swing takes the stability factor down by this share, to no less than the minimum; it comes
/// back at this rate (per second) once the aim has been calm for a while.
constexpr float kSwingStabilityKeep = 0.6f;
constexpr float kMinStability = 0.2f;
constexpr float kStabilityRecoveryPerS = 0.3f;
constexpr float kCalmBeforeRecoveryS = 0.5f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float nsToSeconds(int64_t ns) { return static_cast<float>(ns) * 1e-9f; }

/// Finger travel that may be on its way at once, for a camera response trusted to `confidence` (0 to 1).
float travelCapFor(float confidence) {
    const float c = std::clamp(confidence, 0.0f, 1.0f);
    return kMinTravelCapPx + kTravelCapRangePx * c * c * c;
}

/**
 * True when `rival` beats `current` by the switch ratio. Metrics are "lower is better"; the ones
 * for size and certainty are negative, so for them the ratio works the other way round.
 */
bool clearlyBetter(float rival, float current, float ratio) {
    return current >= 0.0f ? rival < current * ratio : rival < current / ratio;
}

/// The number that ranks a target for the chosen priority; lower is better.
float priorityMetric(int priority, const TrackView& view, float distanceToCrosshair) {
    if (priority == 1) {
        return -view.height;  // the biggest on screen, so usually the closest to the player
    }
    if (priority == 2) {
        return -view.score;  // the one the detector is surest about
    }
    return distanceToCrosshair;
}

/**
 * How sure the tracker is that a target is running, from 0 (standing still) to 1 (clearly moving).
 * A speed is believed only when it is well above its own uncertainty, so a still target whose
 * estimate wobbles with detector noise is never mistaken for a runner.
 */
float movingShareOf(const TrackView& view) {
    if (view.hits < kMinHitsForFeedForward) {
        return 0.0f;
    }
    const float speed = std::hypot(view.velocityX, view.velocityY);
    const float snr = speed / std::max(view.velocitySigma, 1.0f);
    return std::clamp((snr - kFeedForwardMinSnr) / (kFeedForwardFullSnr - kFeedForwardMinSnr), 0.0f, 1.0f);
}

}  // namespace

AimController::AimController(const AimSettings& settings, CameraMotion* motion)
    : settings_(settings), motion_(motion) {}

AimOutput AimController::step(int64_t nowNs, const Tracker& tracker, const ModelProfile& profile,
                              const PlayerInput& player) {
    const float dt = advanceClock(nowNs);

    if (releaseRequested_) {
        releaseRequested_ = false;
        forgetTarget();
        return lift();
    }

    const CameraResponse response = motion_->response();
    Pursuit pursuit;
    telemetry_.targetId = -1;
    if (!beginPursuit(nowNs, response, tracker, profile, &pursuit)) {
        // Nobody to follow: let go. A lock that is only briefly out of sight keeps the finger down, still.
        const AimOutput out = lockedTrackId() < 0 ? lift() : AimOutput();
        noteFinger();
        return out;
    }
    telemetry_.targetId = pursuit.body->id;
    telemetry_.errorX = pursuit.errorX;
    telemetry_.errorY = pursuit.errorY;
    telemetry_.deadzone = pursuit.deadzone;
    telemetry_.movingShare = pursuit.movingShare;
    watchForSwinging(nowNs, dt, pursuit);
    const AimOutput out = touching_ ? moveFinger(nowNs, dt, pursuit, planFingerStep(nowNs, response, pursuit, player, dt))
                                    : putFingerDown(pursuit);
    noteFinger();
    return out;
}

/// Copies the finger's state into the telemetry at the end of a step.
void AimController::noteFinger() {
    telemetry_.stability = stability_;
    telemetry_.fingerX = fingerX_;
    telemetry_.fingerY = fingerY_;
    telemetry_.touching = touching_;
}

float AimController::advanceClock(int64_t nowNs) {
    const float dt = lastStepNs_ == 0 ? kFirstStepS
                                      : std::clamp(nsToSeconds(nowNs - lastStepNs_), kMinStepS, kMaxStepS);
    lastStepNs_ = nowNs;
    return dt;
}

void AimController::forgetTarget() {
    lockedId_.store(-1, std::memory_order_relaxed);
    lostSinceNs_ = 0;
    rivalId_ = -1;
}

AimOutput AimController::lift() {
    AimOutput out;
    if (touching_) {
        out.action = AimAction::kTouchUp;
        touching_ = false;
    }
    fingerX_ = settings_.anchorX;
    fingerY_ = settings_.anchorY;
    return out;
}

void AimController::rankTargets(const TrackView* views, int count, const ModelProfile& profile, Candidate* best,
                                Candidate* locked) const {
    const int lockedId = lockedTrackId();
    for (int i = 0; i < count; ++i) {
        const TrackView& view = views[i];
        if (profile.roleOf(view.classId) != ClassRole::kBody || view.opacity < kMinTargetOpacity) {
            continue;
        }
        const AimPoint point = chooseAimPoint(view, nullptr, settings_.zone);
        const float distance = std::hypot(point.x - crosshairX_, point.y - crosshairY_);
        const bool isLocked = view.id == lockedId;
        const float reach = isLocked ? settings_.fovRadiusPx * kLockedFovSlack : settings_.fovRadiusPx;
        if (distance > reach) {
            continue;
        }
        const float metric = priorityMetric(settings_.priority, view, distance);
        if (best->index < 0 || metric < best->metric) {
            *best = {i, metric};
        }
        if (isLocked) {
            *locked = {i, metric};
        }
    }
}

int AimController::keepOrSwitchLock(int64_t nowNs, const TrackView* views, const Candidate& best,
                                    const Candidate& locked) {
    lostSinceNs_ = 0;
    // Keep the lock unless a rival has been clearly better for a while, so the assist does not
    // flick between two enemies standing near each other.
    const bool rivalAhead = best.index >= 0 && best.index != locked.index &&
                            clearlyBetter(best.metric, locked.metric, settings_.switchRatio);
    if (!rivalAhead) {
        rivalId_ = -1;
        return locked.index;
    }
    const int rivalId = views[best.index].id;
    if (rivalId_ != rivalId) {
        rivalId_ = rivalId;
        rivalSinceNs_ = nowNs;
        return locked.index;
    }
    if (nsToSeconds(nowNs - rivalSinceNs_) >= settings_.switchHoldSeconds) {
        lockedId_.store(rivalId, std::memory_order_relaxed);
        rivalId_ = -1;
        return best.index;
    }
    return locked.index;
}

bool AimController::waitingForLostTarget(int64_t nowNs) {
    if (lostSinceNs_ == 0) {
        lostSinceNs_ = nowNs;
    }
    return nsToSeconds(nowNs - lostSinceNs_) < settings_.lockLossSeconds;
}

int AimController::selectTarget(int64_t nowNs, const TrackView* views, int count, const ModelProfile& profile) {
    Candidate best;
    Candidate locked;
    rankTargets(views, count, profile, &best, &locked);

    if (lockedTrackId() >= 0) {
        if (locked.index >= 0) {
            return keepOrSwitchLock(nowNs, views, best, locked);
        }
        // The locked target is not visible right now: wait briefly in case it comes back.
        if (waitingForLostTarget(nowNs)) {
            return -1;
        }
        forgetTarget();
    }

    if (best.index >= 0) {
        lockedId_.store(views[best.index].id, std::memory_order_relaxed);
        lostSinceNs_ = 0;
        rivalId_ = -1;
    }
    return best.index;
}

bool AimController::beginPursuit(int64_t nowNs, const CameraResponse& response, const Tracker& tracker,
                                 const ModelProfile& profile, Pursuit* pursuit) {
    // Who to follow, judged by where targets are on screen right now.
    const int nowCount = tracker.snapshot(nowNs, nowViews_.data(), kMaxTracks);
    const int chosen = selectTarget(nowNs, nowViews_.data(), nowCount, profile);
    if (chosen < 0) {
        return false;
    }

    // Where that target will be when a move made now takes effect, with the camera slide already
    // promised by earlier moves included (the tracker adds it from the camera motion).
    const int64_t delayNs = static_cast<int64_t>(response.delayS * 1e9f);
    const int futureCount = tracker.snapshot(nowNs + delayNs, futureViews_.data(), kMaxTracks);
    const TrackView* body = &nowViews_[chosen];
    for (int i = 0; i < futureCount; ++i) {
        if (futureViews_[i].id == body->id) {
            body = &futureViews_[i];
            break;
        }
    }
    const TrackView* head = findHeadFor(*body, futureViews_.data(), futureCount, profile);
    const AimPoint aim = chooseAimPoint(*body, head, settings_.zone);

    pursuit->body = body;
    pursuit->errorX = aim.x - crosshairX_;
    pursuit->errorY = aim.y - crosshairY_;
    pursuit->distance = std::hypot(pursuit->errorX, pursuit->errorY);
    pursuit->movingShare = movingShareOf(*body);

    // The dead zone keeps the finger still when already on target. A moving target must be
    // followed closely, so the dead zone shrinks as the target's speed becomes certain.
    const float restingDeadzone = std::max(settings_.deadzoneMinPx, settings_.deadzoneBoxFraction * body->height);
    const float movingDeadzone = std::max(settings_.deadzoneMinPx, restingDeadzone * kMovingDeadzoneShare);
    pursuit->deadzone = lerp(restingDeadzone, movingDeadzone, pursuit->movingShare);
    return true;
}

AimOutput AimController::putFingerDown(const Pursuit& pursuit) {
    if (pursuit.onTarget()) {
        return AimOutput();  // already on target: nothing to do until it moves
    }
    touching_ = true;
    fingerX_ = settings_.anchorX;
    fingerY_ = settings_.anchorY;
    return {AimAction::kTouchDown, fingerX_, fingerY_};
}

AimController::FingerStep AimController::planFingerStep(int64_t /* nowNs */, const CameraResponse& response,
                                                        const Pursuit& pursuit, const PlayerInput& player,
                                                        float dt) const {
    const TrackView& body = *pursuit.body;
    const float gainX = std::max(response.gainX, 0.05f);
    const float gainY = std::max(response.gainY, 0.05f);
    const float yDirection = response.invertY ? -1.0f : 1.0f;
    FingerStep step;

    // Feed-forward: a target running at speed v needs the scene to slide at v too, so the finger
    // is kept moving at v / gain even when the error is zero. Without it the aim would trail a
    // moving target by speed / (closing rate), several body widths at a run.
    const float followShare = pursuit.movingShare * std::clamp(0.5f + settings_.strength, 0.0f, 1.0f);
    step.x += body.velocityX / gainX * dt * followShare;
    step.y += body.velocityY / gainY * dt * followShare * yDirection;

    // Feedback: close a fraction of the remaining error. It aims at the edge of the dead zone, not
    // its centre, so the finger comes to rest instead of twitching.
    if (!pursuit.onTarget()) {
        const float shrink = (pursuit.distance - pursuit.deadzone) / pursuit.distance;
        const float strength = player.lookActive ? settings_.strength * settings_.yieldWhileLooking : settings_.strength;
        const float timeConstant =
            lerp(kSlowestTimeConstantS, kFastestTimeConstantS, std::pow(std::clamp(strength, 0.0f, 1.0f), 0.7f));
        const float closing = (1.0f - std::exp(-dt / timeConstant)) * stability_;
        step.x += pursuit.errorX * shrink / gainX * closing;
        step.y += pursuit.errorY * shrink / gainY * closing * yDirection;
    }

    // A speed limit keeps a big error from becoming a sudden flick.
    const float speedShare = lerp(kMinSpeedShare, 1.0f, std::clamp(settings_.strength, 0.0f, 1.0f));
    float allowed = settings_.maxFingerSpeedPxS * speedShare * dt;

    // While the camera's response is not known well, only a little finger travel may be on its
    // way at a time. A wrong guess then costs a small overshoot instead of a swing, and the
    // first small move shows how the camera really answers.
    float pendingX = 0.0f;
    float pendingY = 0.0f;
    motion_->unseenFinger(&pendingX, &pendingY);
    const float room = std::max(0.0f, travelCapFor(motion_->confidence()) - std::hypot(pendingX, pendingY));
    allowed = std::min(allowed, room);

    const float length = std::hypot(step.x, step.y);
    if (length > allowed) {
        const float scale = length > 0.0f ? allowed / length : 0.0f;
        step.x *= scale;
        step.y *= scale;
    }
    return step;
}

AimOutput AimController::moveFinger(int64_t nowNs, float dt, const Pursuit& pursuit, FingerStep step) {
    // Keep the finger inside its allowed circle. Hitting the edge (or drifting far out while the
    // error is small) means the finger should be lifted and put back at the anchor.
    float newX = fingerX_ + step.x;
    float newY = fingerY_ + step.y;
    const float offsetX = newX - settings_.anchorX;
    const float offsetY = newY - settings_.anchorY;
    const float offset = std::hypot(offsetX, offsetY);
    const float radius = std::max(settings_.anchorRadiusPx, 1.0f);
    const bool hitEdge = offset > radius;
    if (hitEdge) {
        newX = settings_.anchorX + offsetX * radius / offset;
        newY = settings_.anchorY + offsetY * radius / offset;
    }
    const bool quiet = pursuit.distance < kQuietErrorBoxFraction * pursuit.body->height;
    if (hitEdge || (offset > kRecentreShare * radius && quiet)) {
        return lift();  // the next step puts the finger down at the anchor again
    }

    const float movedX = newX - fingerX_;
    const float movedY = newY - fingerY_;
    fingerX_ = newX;
    fingerY_ = newY;
    if (movedX == 0.0f && movedY == 0.0f) {
        return AimOutput();
    }

    // Tell everyone who needs to know how the scene will slide because of this move.
    motion_->recordFinger(nowNs, static_cast<int64_t>(dt * 1e9f), movedX, movedY);
    return {AimAction::kTouchMove, fingerX_, fingerY_};
}

void AimController::forgetSwinging() {
    crossingCount_ = 0;
    sideX_ = 0;
    sideY_ = 0;
}

void AimController::watchForSwinging(int64_t nowNs, float dt, const Pursuit& pursuit) {
    // A different target restarts the watch: the first error to a new target says nothing about swinging.
    if (pursuit.body->id != swingTrackId_) {
        swingTrackId_ = pursuit.body->id;
        forgetSwinging();
    }

    // The aim swings when the error flips from one side of the target to the other, by a clear
    // margin each time, again and again. Crossing the target once is just arriving at it.
    const float margin = std::max(kSwingMarginDeadzones * pursuit.deadzone, kSwingMarginBoxFraction * pursuit.body->height);
    const auto side = [margin](float error) { return error > margin ? 1 : (error < -margin ? -1 : 0); };
    bool crossed = false;
    const int sideX = side(pursuit.errorX);
    const int sideY = side(pursuit.errorY);
    if (sideX != 0) {
        crossed = crossed || (sideX_ != 0 && sideX != sideX_);
        sideX_ = sideX;
    }
    if (sideY != 0) {
        crossed = crossed || (sideY_ != 0 && sideY != sideY_);
        sideY_ = sideY;
    }

    // Keep the recent crossings only.
    int kept = 0;
    for (int i = 0; i < crossingCount_; ++i) {
        if (nsToSeconds(nowNs - crossingNs_[i]) <= kSwingWindowS) {
            crossingNs_[kept++] = crossingNs_[i];
        }
    }
    crossingCount_ = kept;
    if (crossed && crossingCount_ < kMaxCrossings) {
        crossingNs_[crossingCount_++] = nowNs;
        lastCrossingNs_ = nowNs;
    }

    if (crossingCount_ >= kSwingCrossings) {
        stability_ = std::max(kMinStability, stability_ * kSwingStabilityKeep);
        crossingCount_ = 0;
    } else if (nsToSeconds(nowNs - lastCrossingNs_) > kCalmBeforeRecoveryS) {
        stability_ = std::min(1.0f, stability_ + kStabilityRecoveryPerS * dt);
    }
}

}  // namespace aimbuddy
