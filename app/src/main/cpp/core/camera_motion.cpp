#include "camera_motion.h"

#include <algorithm>
#include <cmath>

namespace aimbuddy {

namespace {

/// Trust in the player's own figures before the camera has been measured, and when they were set by hand.
constexpr float kPriorConfidence = 0.35f;
constexpr float kManualConfidence = 0.9f;

/// How often the estimate is looked at while measurements arrive.
constexpr int64_t kEstimateEveryNs = 50'000'000;

/// Measurements below this quality are replaced by what the response predicts.
constexpr float kMinMeasurementQuality = 0.35f;

/// A first measurement this sure of itself replaces the prior at once; later ones move it in steps.
constexpr float kJumpConfidence = 0.5f;

/// Largest factor by which one look at the estimate moves a gain, and largest move of the delay.
constexpr float kMaxGainStep = 1.35f;
constexpr float kMaxDelayStepS = 0.012f;

/// The scene has answered the finger when it slid at least this share of the finger's net travel
/// (games slide it by far more than this), judged only once the finger has moved this far and
/// frames covering nearly all of the time could be compared: a stretch in which the picture could
/// not be read (a busy scene, a fast turn) hides slides, and the net slide would then fall short.
constexpr float kAnswerMinShare = 0.15f;
constexpr float kAnswerMinFingerPx = 30.0f;
constexpr float kAnswerMinCoverage = 0.85f;

/// A time later than any move can land, for "everything from here on".
constexpr int64_t kForeverNs = INT64_MAX / 4;

/// Limits for the figures in use.
constexpr float kMinGain = 0.15f;
constexpr float kMaxGain = 40.0f;
constexpr float kMaxDelayS = 0.2f;

/// How far a new confidence reading pulls the stored one.
constexpr float kConfidencePull = 0.3f;

/// Moves `current` towards `target` along a ratio scale, by at most `maxFactor` at once.
float stepGain(float current, float target, float rate, float maxFactor) {
    const float logCurrent = std::log(std::max(current, kMinGain));
    const float logTarget = std::log(std::clamp(target, kMinGain, kMaxGain));
    const float wanted = std::exp(logCurrent + rate * (logTarget - logCurrent));
    return std::clamp(wanted, current / maxFactor, current * maxFactor);
}

}  // namespace

void CameraMotion::setPrior(const CameraResponse& prior, bool learn) {
    std::lock_guard<std::mutex> lock(mutex_);
    prior_ = prior;
    estimator_.setInvertY(prior.invertY);
    if (learn != learn_) {
        hasLearned_ = false;
        learn_ = learn;
    }
    if (!learn_ || !hasLearned_) {
        current_ = prior;
        confidence_ = learn_ ? kPriorConfidence : kManualConfidence;
    } else {
        current_.invertY = prior.invertY;
    }
}

void CameraMotion::seed(const CameraResponse& earlier, float confidence) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_.gainX = std::clamp(earlier.gainX, kMinGain, kMaxGain);
    current_.gainY = std::clamp(earlier.gainY, kMinGain, kMaxGain);
    current_.delayS = std::clamp(earlier.delayS, 0.0f, kMaxDelayS);
    current_.invertY = prior_.invertY;
    confidence_ = std::clamp(confidence, 0.0f, 1.0f);
    hasLearned_ = true;
}

CameraResponse CameraMotion::response() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

float CameraMotion::confidence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return confidence_;
}

bool CameraMotion::hasLearned() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasLearned_;
}

void CameraMotion::recordFinger(int64_t sentNs, int64_t spreadNs, float dx, float dy) {
    std::lock_guard<std::mutex> lock(mutex_);
    fingers_.record(sentNs, spreadNs, dx, dy);
}

void CameraMotion::modelShift(int64_t fromNs, int64_t toNs, float* dx, float* dy) const {
    float fingerX = 0.0f;
    float fingerY = 0.0f;
    fingers_.landedBetween(current_.delayS, fromNs, toNs, &fingerX, &fingerY);
    current_.sceneShift(fingerX, fingerY, dx, dy);
}

void CameraMotion::addMeasurement(int64_t fromNs, int64_t toNs, float dx, float dy, float quality) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (toNs <= fromNs || toNs <= measured_.latestNs()) {
        return;   // out of order or empty: the record only grows forward
    }
    const bool usable = quality >= kMinMeasurementQuality && std::isfinite(dx) && std::isfinite(dy);
    if (!usable) {
        // Nothing trustworthy was seen: assume the camera did what the assist's own moves say.
        float predictedX = 0.0f;
        float predictedY = 0.0f;
        modelShift(fromNs, toNs, &predictedX, &predictedY);
        measured_.add(fromNs, toNs, predictedX, predictedY, false);
        return;
    }
    measured_.add(fromNs, toNs, dx, dy);
    if (!learn_) {
        return;
    }
    estimator_.addSample(fingers_, {fromNs, toNs, dx, dy, quality});
    if (toNs - lastEstimateNs_ >= kEstimateEveryNs) {
        lastEstimateNs_ = toNs;
        adopt(estimator_.estimate());
    }
}

void CameraMotion::adopt(const ResponseEstimator::Estimate& estimate) {
    if (!estimate.valid) {
        return;
    }
    const bool jump = !hasLearned_ && estimate.confidence >= kJumpConfidence;
    const float rate = std::clamp(estimate.confidence, 0.25f, 0.7f);
    const float maxFactor = jump ? kMaxGain : kMaxGainStep;
    const float stepRate = jump ? 1.0f : rate;

    // Most games use the same sensitivity both ways, so an axis the finger has not moved along
    // follows the one it has, keeping the ratio the player's own figures have.
    const float priorRatioY = prior_.gainY / std::max(prior_.gainX, kMinGain);
    float targetX = estimate.gainX;
    float targetY = estimate.gainY;
    if (!estimate.hasGainX) {
        targetX = estimate.gainY / std::max(priorRatioY, 0.1f);
    }
    if (!estimate.hasGainY) {
        targetY = estimate.gainX * priorRatioY;
    }
    if (jump || estimate.hasGainX || estimate.hasGainY) {
        current_.gainX = stepGain(current_.gainX, targetX, stepRate, maxFactor);
        current_.gainY = stepGain(current_.gainY, targetY, stepRate, maxFactor);
    }
    if (estimate.hasDelay) {
        const float wanted = std::clamp(estimate.delayS, 0.0f, kMaxDelayS);
        const float step = std::clamp(stepRate * (wanted - current_.delayS), -kMaxDelayStepS, kMaxDelayStepS);
        current_.delayS = jump ? wanted : current_.delayS + step;
    }
    confidence_ = hasLearned_ ? confidence_ + kConfidencePull * (estimate.confidence - confidence_) : estimate.confidence;
    hasLearned_ = true;
}

void CameraMotion::unseenFinger(float* dx, float* dy) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Everything that lands after the newest captured frame, however far ahead.
    fingers_.landedBetween(current_.delayS, measured_.latestNs(), kForeverNs, dx, dy);
}

CameraMotion::Answer CameraMotion::fingerAnswered(int64_t fromNs, int64_t toNs, float* fingerPx, float* slidePx) const {
    std::lock_guard<std::mutex> lock(mutex_);
    float fingerX = 0.0f;
    float fingerY = 0.0f;
    fingers_.landedBetween(current_.delayS, fromNs, toNs, &fingerX, &fingerY);
    float slideX = 0.0f;
    float slideY = 0.0f;
    int64_t coveredNs = 0;
    measured_.seenShiftBetween(fromNs, toNs, &slideX, &slideY, &coveredNs);
    if (fingerPx != nullptr && slidePx != nullptr) {
        const bool alongX = std::fabs(fingerX) >= std::fabs(fingerY);
        *fingerPx = std::fabs(alongX ? fingerX : fingerY);
        *slidePx = std::fabs(alongX ? slideX : slideY);
    }
    if (static_cast<float>(coveredNs) < kAnswerMinCoverage * static_cast<float>(toNs - fromNs)) {
        return Answer::kUnknown;
    }
    // Each axis on its own, so a game that looks the other way up and down makes no difference.
    const bool xMoved = std::fabs(fingerX) >= kAnswerMinFingerPx;
    const bool yMoved = std::fabs(fingerY) >= kAnswerMinFingerPx;
    if (!xMoved && !yMoved) {
        return Answer::kUnknown;
    }
    const bool xAnswered = xMoved && std::fabs(slideX) >= kAnswerMinShare * std::fabs(fingerX);
    const bool yAnswered = yMoved && std::fabs(slideY) >= kAnswerMinShare * std::fabs(fingerY);
    return xAnswered || yAnswered ? Answer::kAnswered : Answer::kIgnored;
}

void CameraMotion::shiftBetween(int64_t fromNs, int64_t toNs, float* dx, float* dy) const {
    std::lock_guard<std::mutex> lock(mutex_);
    float seenX = 0.0f;
    float seenY = 0.0f;
    const int64_t seenUntilNs = measured_.latestNs();
    if (seenUntilNs > fromNs) {
        measured_.shiftBetween(fromNs, std::min(toNs, seenUntilNs), &seenX, &seenY);
    }
    // Past the newest captured frame only the assist's own moves are known.
    float aheadX = 0.0f;
    float aheadY = 0.0f;
    const int64_t aheadFromNs = std::max(fromNs, seenUntilNs);
    if (toNs > aheadFromNs) {
        modelShift(aheadFromNs, toNs, &aheadX, &aheadY);
    }
    *dx = seenX + aheadX;
    *dy = seenY + aheadY;
}

void CameraMotion::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    fingers_.clear();
    measured_.clear();
    estimator_.reset();
    lastEstimateNs_ = 0;
}

}  // namespace aimbuddy
