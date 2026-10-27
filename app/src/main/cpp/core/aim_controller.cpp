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

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float nsToSeconds(int64_t ns) { return static_cast<float>(ns) * 1e-9f; }

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
        return -view.height;  // the largest, so usually the closest to the player
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

AimController::AimController(const AimSettings& settings) : settings_(settings) {}

AimOutput AimController::step(int64_t nowNs, const Tracker& tracker, const ModelProfile& profile,
                              const PlayerInput& player) {
    const float dt = advanceClock(nowNs);

    if (releaseRequested_) {
        releaseRequested_ = false;
        forgetTarget();
        return lift();
    }

    Pursuit pursuit;
    if (!beginPursuit(nowNs, tracker, profile, &pursuit)) {
        // Nobody to follow: let go. A lock that is only briefly out of sight keeps the finger down, still.
        return lockedTrackId() < 0 ? lift() : AimOutput();
    }
    if (!touching_) {
        return putFingerDown(pursuit);
    }
    return moveFinger(nowNs, dt, pursuit, planFingerStep(pursuit, player, dt));
}

float AimController::advanceClock(int64_t nowNs) {
    const float dt = lastStepNs_ == 0 ? kFirstStepS
                                      : std::clamp(nsToSeconds(nowNs - lastStepNs_), kMinStepS, kMaxStepS);
    lastStepNs_ = nowNs;
    return dt;
}

int64_t AimController::actuationDelayNs() const {
    return static_cast<int64_t>(settings_.actuationDelayS * 1e9f);
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

bool AimController::beginPursuit(int64_t nowNs, const Tracker& tracker, const ModelProfile& profile,
                                 Pursuit* pursuit) {
    // Who to follow, judged by where targets are on screen right now.
    const int nowCount = tracker.snapshot(nowNs, nowViews_.data(), kMaxTracks);
    const int chosen = selectTarget(nowNs, nowViews_.data(), nowCount, profile);
    if (chosen < 0) {
        return false;
    }

    // Where that target will be when a move made now takes effect, with the camera slide already
    // promised by earlier moves included (the tracker adds it from the commanded-motion log).
    const int futureCount = tracker.snapshot(nowNs + actuationDelayNs(), futureViews_.data(), kMaxTracks);
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

AimController::FingerStep AimController::planFingerStep(const Pursuit& pursuit, const PlayerInput& player,
                                                        float dt) const {
    const TrackView& body = *pursuit.body;
    const float gain = std::max(settings_.sceneGain, 0.05f);
    const float yDirection = settings_.invertY ? -1.0f : 1.0f;
    FingerStep step;

    // Feed-forward: a target running at speed v needs the scene to slide at v too, so the finger
    // is kept moving at v / gain even when the error is zero. Without it the aim would trail a
    // moving target by speed / (closing rate), several body widths at a run.
    const float followShare = pursuit.movingShare * std::clamp(0.5f + settings_.strength, 0.0f, 1.0f);
    step.x += body.velocityX / gain * dt * followShare;
    step.y += body.velocityY / gain * dt * followShare * yDirection;

    // Feedback: close a fraction of the remaining error. It aims at the edge of the dead zone, not
    // its centre, so the finger comes to rest instead of twitching.
    if (!pursuit.onTarget()) {
        const float shrink = (pursuit.distance - pursuit.deadzone) / pursuit.distance;
        const float strength = player.lookActive ? settings_.strength * settings_.yieldWhileLooking : settings_.strength;
        const float timeConstant =
            lerp(kSlowestTimeConstantS, kFastestTimeConstantS, std::pow(std::clamp(strength, 0.0f, 1.0f), 0.7f));
        const float closing = 1.0f - std::exp(-dt / timeConstant);
        step.x += pursuit.errorX * shrink / gain * closing;
        step.y += pursuit.errorY * shrink / gain * closing * yDirection;
    }

    // A speed limit keeps a big error from becoming a sudden flick.
    const float speedShare = lerp(kMinSpeedShare, 1.0f, std::clamp(settings_.strength, 0.0f, 1.0f));
    const float maxStep = settings_.maxFingerSpeedPxS * speedShare * dt;
    const float length = std::hypot(step.x, step.y);
    if (length > maxStep) {
        step.x *= maxStep / length;
        step.y *= maxStep / length;
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

    // Tell the tracker how the scene will slide because of this move, once the game applies it.
    const int64_t landsNs = nowNs + actuationDelayNs();
    const float yScene = settings_.invertY ? settings_.sceneGain : -settings_.sceneGain;
    motion_.record(landsNs, landsNs + static_cast<int64_t>(dt * 1e9f), -settings_.sceneGain * movedX, yScene * movedY);
    return {AimAction::kTouchMove, fingerX_, fingerY_};
}

}  // namespace aimbuddy
