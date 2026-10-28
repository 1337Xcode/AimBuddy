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

/// A target is judged to be stuck to the screen when, over a stretch of at least this many seconds and
/// at least this many looks (and no longer than the window, after which the watch starts again), the
/// camera swept at least this many pixels in all, every place the target was seen at lay within the
/// larger of this many pixels and this share of its height, and it was on average further from the
/// crosshair than the larger of this many pixels and this share of its height. That must be so on this
/// many looks in a row. A target that runs or strafes, or one the camera is following, does not stay
/// put on the screen; one the aim is on is not far from the crosshair.
constexpr float kStuckMinSeconds = 0.4f;
constexpr int kStuckMinLooks = 4;
constexpr float kStuckWindowSeconds = 2.0f;
constexpr float kStuckMinSweepPx = 150.0f;
constexpr float kStuckSpreadPx = 14.0f;
constexpr float kStuckSpreadBoxFraction = 0.12f;
constexpr float kStuckMinErrorPx = 50.0f;
constexpr float kStuckErrorBoxFraction = 0.3f;
constexpr int kStuckLooks = 2;

/// A target given up on is left alone this long the first time, twice as long each time after, to the limit;
/// it is forgotten this long after that. Two boxes are the same one when their centres are within this share
/// of the larger one's height and their areas differ by less than this factor.
constexpr float kGiveUpFirstS = 3.0f;
constexpr float kGiveUpLongestS = 12.0f;
constexpr float kGiveUpForgetS = 30.0f;
constexpr float kSamePlaceBoxShare = 0.5f;
constexpr float kSameBoxAreaFactor = 2.5f;

/// A box given up on stays out of the picture this long after the assist is willing to try it again.
constexpr int64_t kDecoyHiddenExtraNs = 5'000'000'000;

/// A finger is given this long to show an effect before the game is judged to be ignoring it, and
/// only the most recent stretch of this length is looked at (the records of moves and slides are short).
constexpr float kAnswerWithinS = 1.4f;
constexpr float kAnswerWindowS = 1.5f;

/// The verdict "ignored" must hold this long before the finger is lifted, so a moment in which the
/// picture was hard to read cannot condemn a finger that is being answered.
constexpr float kIgnoredFor = 0.3f;

/// With the finger set to hands off, the crosshair must have stayed on the target this long before the correction counts as made.
constexpr float kLandedHoldS = 0.04f;

/// After the first lift the finger waits this long before touching again; each further failure doubles it, to this limit.
constexpr float kRetryFirstS = 0.25f;
constexpr float kRetryLongestS = 4.0f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float nsToSeconds(int64_t ns) { return static_cast<float>(ns) * 1e-9f; }

/// While the camera cannot be watched, the finger moves, closes the gap and leads a moving target by
/// this much less at the most (each by its own share of the blindness, 0 to 1).
constexpr float kBlindTravelLoss = 0.75f;
constexpr float kBlindClosingLoss = 0.5f;

/// Finger travel that may be on its way at once, for a camera response trusted to `confidence` (0 to 1)
/// and a camera that could not be watched for the share `blindness` of the last second.
float travelCapFor(float confidence, float blindness) {
    const float c = std::clamp(confidence, 0.0f, 1.0f);
    return (kMinTravelCapPx + kTravelCapRangePx * c * c * c) * (1.0f - kBlindTravelLoss * blindness);
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
        forgetFinger();
        return lift();
    }

    const CameraResponse response = motion_->response();
    Pursuit pursuit;
    telemetry_.targetId = -1;
    if (!beginPursuit(nowNs, response, tracker, profile, &pursuit)) {
        // Nobody to follow: let go. A lock that is only briefly out of sight keeps the finger down, still.
        AimOutput out;
        if (lockedTrackId() < 0) {
            out = lift();
            endRun();
        }
        noteFinger();
        return out;
    }
    telemetry_.targetId = pursuit.body->id;
    telemetry_.errorX = pursuit.errorX;
    telemetry_.errorY = pursuit.errorY;
    telemetry_.deadzone = pursuit.deadzone;
    telemetry_.movingShare = pursuit.movingShare;
    watchForSwinging(nowNs, dt, pursuit);
    if (cannotCloseOn(nowNs, tracker, *pursuit.body)) {
        // Not part of the scene: let go of it and of the finger, and look for another.
        const AimOutput out = lift();
        endRun();
        noteFinger();
        return out;
    }
    if (touching_ && fingerSeemsDead(nowNs)) {
        // The game is not answering this finger: lift it and try again after a wait.
        const float wait = std::min(kRetryLongestS, kRetryFirstS * static_cast<float>(1 << std::min(deadStreak_, 4)));
        mayTouchAfterNs_ = nowNs + static_cast<int64_t>(wait * 1e9f);
        ++deadStreak_;
        ++telemetry_.deadFingers;
        const AimOutput out = lift();
        endRun();
        noteFinger();
        return out;
    }
    if (settings_.afterLanding == AfterLanding::kHandsOff && hasLanded(nowNs, pursuit)) {
        // The one correction for this target has been made: hands off.
        const AimOutput out = lift();
        endRun();
        noteFinger();
        return out;
    }
    const AimOutput out = touching_ ? moveFinger(nowNs, dt, pursuit, planFingerStep(nowNs, response, pursuit, player, dt))
                                    : putFingerDown(nowNs, pursuit);
    noteFinger();
    return out;
}

/// True when the finger has moved a good way and the screen has not slid in answer, for a while.
bool AimController::fingerSeemsDead(int64_t nowNs) {
    if (nsToSeconds(nowNs - runStartNs_) < kAnswerWithinS) {
        return false;
    }
    const int64_t fromNs = std::max(runStartNs_, nowNs - static_cast<int64_t>(kAnswerWindowS * 1e9f));
    float fingerPx = 0.0f;
    float slidePx = 0.0f;
    switch (motion_->fingerAnswered(fromNs, nowNs, &fingerPx, &slidePx)) {
        case CameraMotion::Answer::kAnswered:
            deadStreak_ = 0;
            ignoredSinceNs_ = 0;
            return false;
        case CameraMotion::Answer::kIgnored:
            if (ignoredSinceNs_ == 0) {
                ignoredSinceNs_ = nowNs;
            }
            telemetry_.deadFingerPx = fingerPx;
            telemetry_.deadSlidePx = slidePx;
            return nsToSeconds(nowNs - ignoredSinceNs_) >= kIgnoredFor;
        case CameraMotion::Answer::kUnknown:
            break;
    }
    ignoredSinceNs_ = 0;
    return false;
}

/// True once the crosshair has stayed on the target for a moment, and for as long as that target stays the lock.
bool AimController::hasLanded(int64_t nowNs, const Pursuit& pursuit) {
    if (landedId_ == pursuit.body->id) {
        return true;
    }
    if (!pursuit.onTarget()) {
        onTargetSinceNs_ = 0;
        return false;
    }
    if (onTargetSinceNs_ == 0) {
        onTargetSinceNs_ = nowNs;
    }
    if (nsToSeconds(nowNs - onTargetSinceNs_) < kLandedHoldS) {
        return false;
    }
    landedId_ = pursuit.body->id;
    return true;
}

/// Ends the run of touches: the next finger gets the full time to show an effect, and a game that ignored
/// the one before is not held against it.
void AimController::endRun() {
    runStartNs_ = 0;
    ignoredSinceNs_ = 0;
}

/// Forgets everything held against the game for ignoring the finger, for a fresh start of the assist: it was
/// switched off, or the screen turned, and what the game did before says nothing about what it will do now.
void AimController::forgetFinger() {
    endRun();
    deadStreak_ = 0;
    mayTouchAfterNs_ = 0;
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
    landedId_ = -1;
    onTargetSinceNs_ = 0;
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
        if (profile.roleOf(view.classId) != ClassRole::kBody || view.opacity < kMinTargetOpacity ||
            view.phase == TrackPhase::kTentative || isGivenUp(view, lastStepNs_)) {
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

/// True when the box is the one the entry was made for: the same target, or one in the same place of about the same size.
bool AimController::matchesGivenUp(const GivenUp& entry, const TrackView& view) {
    const float tallest = std::max(view.height, entry.height);
    const float areaRatio = std::max(view.width * view.height, entry.width * entry.height) /
                            std::max(1.0f, std::min(view.width * view.height, entry.width * entry.height));
    const bool samePlace = std::hypot(view.centerX - entry.centerX, view.centerY - entry.centerY) <= kSamePlaceBoxShare * tallest &&
                           areaRatio <= kSameBoxAreaFactor;
    return view.id == entry.trackId || samePlace;
}

/// True when the target is one that was given up on and is still being left alone.
bool AimController::isGivenUp(const TrackView& view, int64_t nowNs) const {
    std::lock_guard<std::mutex> lock(givenUpMutex_);
    for (const GivenUp& entry : givenUp_) {
        if (entry.trackId >= 0 && nowNs < entry.untilNs && matchesGivenUp(entry, view)) {
            return true;
        }
    }
    return false;
}

bool AimController::isDecoy(const TrackView& view, int64_t nowNs) const {
    // Past the wait the assist may try the box again, but the box has not become a person: it stays hidden
    // a few seconds longer, and a new giving up renews it. A real enemy who stands where the box was shows again soon.
    std::lock_guard<std::mutex> lock(givenUpMutex_);
    for (const GivenUp& entry : givenUp_) {
        if (entry.trackId >= 0 && nowNs < entry.untilNs + kDecoyHiddenExtraNs && matchesGivenUp(entry, view)) {
            return true;
        }
    }
    return false;
}

/// Forgets what was seen of the target being watched, so the watch starts afresh at the next look.
void AimController::forgetWatch() {
    watchedId_ = -1;
    watchLastSeenNs_ = 0;
    watchLooks_ = 0;
    futileLooks_ = 0;
}

/**
 * Judges, at each new look the tracker has had at the target, whether the target is stuck to the
 * screen. The judging uses where the target was when it was last seen, not where it is projected
 * to be, because the projection moves with the assist's own finger and would make anything look
 * as if it were coming closer. Returns true once it is, after giving it up.
 */
bool AimController::cannotCloseOn(int64_t nowNs, const Tracker& tracker, const TrackView& body) {
    if (body.lastSeenNs <= 0 || body.phase == TrackPhase::kTentative || body.lastSeenNs == watchLastSeenNs_) {
        return false;   // nothing new has been seen since the last look
    }
    const int count = tracker.snapshot(body.lastSeenNs, watchViews_.data(), kMaxTracks, true);
    const TrackView* seen = nullptr;
    for (int i = 0; i < count; ++i) {
        if (watchViews_[i].id == body.id) {
            seen = &watchViews_[i];
            break;
        }
    }
    if (seen == nullptr) {
        return false;
    }
    const int64_t previousLookNs = watchLastSeenNs_;
    watchLastSeenNs_ = body.lastSeenNs;
    const AimPoint aim = chooseAimPoint(*seen, nullptr, settings_.zone);
    const float errorPx = std::hypot(aim.x - crosshairX_, aim.y - crosshairY_);
    const float watchedS = nsToSeconds(body.lastSeenNs - watchStartNs_);
    if (body.id != watchedId_ || watchedS > kStuckWindowSeconds || watchedS < 0.0f || previousLookNs <= 0) {
        watchedId_ = body.id;
        watchStartNs_ = body.lastSeenNs;
        watchMinX_ = watchMaxX_ = seen->centerX;
        watchMinY_ = watchMaxY_ = seen->centerY;
        watchSweptPx_ = 0.0f;
        watchErrorSumPx_ = errorPx;
        watchLooks_ = 1;
        futileLooks_ = 0;
        return false;
    }

    // Where the target has been seen, how far it was from the crosshair, and how far the camera swept
    // (as the frames measured it) between the last look and this one.
    watchMinX_ = std::min(watchMinX_, seen->centerX);
    watchMaxX_ = std::max(watchMaxX_, seen->centerX);
    watchMinY_ = std::min(watchMinY_, seen->centerY);
    watchMaxY_ = std::max(watchMaxY_, seen->centerY);
    watchErrorSumPx_ += errorPx;
    ++watchLooks_;
    float sweepX = 0.0f;
    float sweepY = 0.0f;
    motion_->shiftBetween(previousLookNs, body.lastSeenNs, &sweepX, &sweepY);
    watchSweptPx_ += std::hypot(sweepX, sweepY);

    const float spreadPx = std::max(watchMaxX_ - watchMinX_, watchMaxY_ - watchMinY_);
    const float meanErrorPx = watchErrorSumPx_ / static_cast<float>(watchLooks_);
    const bool stuck = watchedS >= kStuckMinSeconds && watchLooks_ >= kStuckMinLooks && watchSweptPx_ >= kStuckMinSweepPx &&
                       spreadPx <= std::max(kStuckSpreadPx, kStuckSpreadBoxFraction * seen->height) &&
                       meanErrorPx >= std::max(kStuckMinErrorPx, kStuckErrorBoxFraction * seen->height);
    futileLooks_ = stuck ? futileLooks_ + 1 : 0;
    if (futileLooks_ < kStuckLooks) {
        return false;
    }
    giveUpOn(nowNs, *seen);
    return true;
}

/// Leaves the target alone for a while, longer if it is the same one as before, and lets go of the lock.
void AimController::giveUpOn(int64_t nowNs, const TrackView& seen) {
    std::lock_guard<std::mutex> lock(givenUpMutex_);
    GivenUp* slot = nullptr;
    for (GivenUp& entry : givenUp_) {
        if (entry.trackId >= 0 && nowNs >= entry.forgottenNs) {
            entry = GivenUp();   // too long ago to count against it
        }
        const float tallest = std::max(seen.height, entry.height);
        const bool samePlace = entry.trackId >= 0 &&
                               std::hypot(seen.centerX - entry.centerX, seen.centerY - entry.centerY) <= kSamePlaceBoxShare * tallest;
        if (samePlace || entry.trackId == seen.id) {
            slot = &entry;
            break;
        }
    }
    if (slot == nullptr) {
        for (GivenUp& entry : givenUp_) {
            if (entry.trackId < 0) {
                slot = &entry;
                break;
            }
        }
    }
    if (slot == nullptr) {
        slot = &givenUp_[0];   // all taken: the oldest slot is as good as any
        for (GivenUp& entry : givenUp_) {
            if (entry.untilNs < slot->untilNs) {
                slot = &entry;
            }
        }
    }
    const int times = slot->trackId >= 0 ? slot->times + 1 : 1;
    const float waitS = std::min(kGiveUpLongestS, kGiveUpFirstS * static_cast<float>(1 << std::min(times - 1, 5)));
    slot->trackId = seen.id;
    slot->centerX = seen.centerX;
    slot->centerY = seen.centerY;
    slot->width = seen.width;
    slot->height = seen.height;
    slot->times = times;
    slot->untilNs = nowNs + static_cast<int64_t>(waitS * 1e9f);
    slot->forgottenNs = slot->untilNs + static_cast<int64_t>(kGiveUpForgetS * 1e9f);
    ++telemetry_.giveUps;
    forgetTarget();
    forgetWatch();
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

AimOutput AimController::putFingerDown(int64_t nowNs, const Pursuit& pursuit) {
    if (pursuit.onTarget()) {
        endRun();   // nothing to do until the target moves, and the touch that follows is a new run
        return AimOutput();
    }
    if (nowNs < mayTouchAfterNs_) {
        return AimOutput();  // waiting after an ignored finger
    }
    // A finger put down again after reaching the edge of its circle carries on the run it was in: the time
    // the game has had to answer is counted from the first touch, not from this one.
    if (runStartNs_ == 0) {
        runStartNs_ = nowNs;
    }
    touching_ = true;
    fingerX_ = settings_.anchorX;
    fingerY_ = settings_.anchorY;
    return {AimAction::kTouchDown, fingerX_, fingerY_};
}

AimController::FingerStep AimController::planFingerStep(int64_t nowNs, const CameraResponse& response,
                                                        const Pursuit& pursuit, const PlayerInput& player,
                                                        float dt) const {
    const TrackView& body = *pursuit.body;
    const float blindness = motion_->blindness(nowNs);
    const float gainX = std::max(response.gainX, 0.05f);
    const float gainY = std::max(response.gainY, 0.05f);
    const float yDirection = response.invertY ? -1.0f : 1.0f;
    FingerStep step;

    // Feed-forward: a target running at speed v needs the scene to slide at v too, so the finger
    // is kept moving at v / gain even when the error is zero. Without it the aim would trail a
    // moving target by speed / (closing rate), several body widths at a run.
    // It leans on the camera's response being known, so it fades out while the camera cannot be watched.
    // It does not know whether the player's thumb is already turning the camera along with the target.
    // If it is, the two overshoot a little and the feedback below takes the excess back, which leaves the
    // assist helping a runner only a little, where it helps a strafing target a good deal.
    const float followShare = pursuit.movingShare * std::clamp(0.5f + settings_.strength, 0.0f, 1.0f) * (1.0f - blindness);
    step.x += body.velocityX / gainX * dt * followShare;
    step.y += body.velocityY / gainY * dt * followShare * yDirection;

    // Feedback: close a fraction of the remaining error. It aims at the edge of the dead zone, not
    // its centre, so the finger comes to rest instead of twitching.
    if (!pursuit.onTarget()) {
        const float shrink = (pursuit.distance - pursuit.deadzone) / pursuit.distance;
        const float strength = settings_.strength * lerp(1.0f, settings_.yieldWhileLooking, std::clamp(player.steering, 0.0f, 1.0f));
        const float timeConstant =
            lerp(kSlowestTimeConstantS, kFastestTimeConstantS, std::pow(std::clamp(strength, 0.0f, 1.0f), 0.7f));
        const float closing = (1.0f - std::exp(-dt / timeConstant)) * stability_ * (1.0f - kBlindClosingLoss * blindness);
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
    const float room = std::max(0.0f, travelCapFor(motion_->confidence(), blindness) - std::hypot(pendingX, pendingY));
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
