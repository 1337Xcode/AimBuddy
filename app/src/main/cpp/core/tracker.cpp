#include "tracker.h"

#include <algorithm>
#include <cmath>

#include "assignment.h"

namespace aimbuddy {

namespace {

/// Cost that marks a track/detection pair as not allowed to match.
constexpr float kBlockedCost = 2.0f;

/// Longest time step the motion model integrates in one go; a longer gap means updates stalled.
constexpr float kMaxStepSeconds = 0.25f;

/// A box cut off by the edge of the scanned region has an unreliable position and size on that axis.
constexpr float kClippedVarianceScale = 16.0f;

constexpr float kNanosecondsPerSecond = 1e9f;

/// A pause between two results longer than this (the app was in the background) says nothing about how often they come.
constexpr int64_t kMaxArrivalGapNs = 1'000'000'000;

/// A missed target is held for this many times the gap between results, if that is longer than the configured time,
/// and forgotten after this many. Results that come every 200 ms leave 300 ms to hold a target that one of them missed.
constexpr int64_t kHoldGaps = 3;       // in halves: 1.5 gaps
constexpr int64_t kCoastGaps = 7;      // in halves: 3.5 gaps

/// Results come as often as they have until this many gaps have passed without one; only then is the detector quiet.
constexpr int64_t kQuietAfterGaps = 2;

float square(float v) { return v * v; }

float nsToSeconds(int64_t ns) { return static_cast<float>(ns) / kNanosecondsPerSecond; }

/// How two boxes (centre and size) overlap, as the shared area and each box's own area.
struct Overlap {
    float intersection = 0.0f;
    float areaA = 0.0f;
    float areaB = 0.0f;

    /// Shared area divided by the area covered by either box, in [0, 1].
    float overUnion() const {
        const float unionArea = areaA + areaB - intersection;
        return unionArea > 0.0f ? intersection / unionArea : 0.0f;
    }
    /// Shared area divided by the smaller box, in [0, 1]. High when one box lies inside the other.
    float overSmaller() const {
        const float smaller = std::min(areaA, areaB);
        return smaller > 0.0f ? intersection / smaller : 0.0f;
    }
};

Overlap measureOverlap(float cxA, float cyA, float wA, float hA, float cxB, float cyB, float wB, float hB) {
    Overlap result;
    result.areaA = wA * hA;
    result.areaB = wB * hB;
    const float overlapW = std::min(cxA + wA * 0.5f, cxB + wB * 0.5f) - std::max(cxA - wA * 0.5f, cxB - wB * 0.5f);
    const float overlapH = std::min(cyA + hA * 0.5f, cyB + hB * 0.5f) - std::max(cyA - hA * 0.5f, cyB - hB * 0.5f);
    if (overlapW > 0.0f && overlapH > 0.0f) {
        result.intersection = overlapW * overlapH;
    }
    return result;
}

/// A detection the tracker can use: finite numbers and a box with an area.
bool isUsable(const Detection& d) {
    return std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.width) &&
           std::isfinite(d.height) && std::isfinite(d.score) && d.width > 0.0f && d.height > 0.0f;
}

}  // namespace

TrackerConfig TrackerConfig::forConfidence(float confidence) {
    const float birth = std::max(0.20f, std::min(confidence, 0.95f));
    TrackerConfig config;
    config.birthScore = birth;
    config.highScore = std::max(0.20f, birth - 0.10f);
    config.lowScore = std::max(0.10f, std::min(birth * 0.30f, 0.20f));
    config.showTentativeScore = std::min(0.97f, birth + 0.40f);
    return config;
}

Tracker::Tracker(const TrackerConfig& config) : config_(config) {}

void Tracker::setConfig(const TrackerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

void Tracker::setEgoMotion(const EgoMotion* egoMotion) {
    std::lock_guard<std::mutex> lock(mutex_);
    egoMotion_ = egoMotion;
}

void Tracker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    trackCount_ = 0;
    nextId_ = 1;
    hasArrival_ = false;
    latestArrivalNs_ = 0;
    arrivalGapNs_ = 0;
}

void Tracker::noteArrival(int64_t captureNs, int64_t arrivalNs) {
    arrivalNs = arrivalNs < 0 ? captureNs : arrivalNs;
    if (hasArrival_ && arrivalNs > latestArrivalNs_) {
        const int64_t gapNs = std::min(arrivalNs - latestArrivalNs_, kMaxArrivalGapNs);
        arrivalGapNs_ = arrivalGapNs_ == 0 ? gapNs : arrivalGapNs_ + (gapNs - arrivalGapNs_) / 4;
    }
    latestArrivalNs_ = hasArrival_ ? std::max(latestArrivalNs_, arrivalNs) : arrivalNs;
    hasArrival_ = true;
}

int64_t Tracker::holdNs() const { return std::max(config_.holdNs, arrivalGapNs_ * kHoldGaps / 2); }

int64_t Tracker::coastNs() const { return std::max(config_.coastNs, arrivalGapNs_ * kCoastGaps / 2); }

void Tracker::predictTrack(Track& track, int64_t toNs) const {
    if (toNs <= track.stateNs) {
        return;
    }
    const float dt = std::min(nsToSeconds(toNs - track.stateNs), kMaxStepSeconds);

    // The part of the screen movement we caused by turning the camera is known, not guessed.
    float egoX = 0.0f;
    float egoY = 0.0f;
    if (egoMotion_ != nullptr) {
        egoMotion_->shiftBetween(track.stateNs, toNs, &egoX, &egoY);
    }

    // A target missed for longer than the hold time has probably stopped or gone behind something, so its speed fades
    // from then on. A frame or two without it says nothing of the kind: a target the detector sees in only some frames
    // (half hidden, blurred, small) keeps the speed the filter is sure of, where fading all of it at every miss left
    // the aim behind (a runner at 300 px/s seen in three frames out of five was tracked at 213 px/s). The part of the
    // speed that is no bigger than its own doubt, such as the trace of a misread camera turn, still fades.
    if (track.phase == TrackPhase::kCoasting) {
        const float fade = std::exp(-dt / config_.coastVelocityDecayS);
        if (track.missNs > holdNs()) {
            track.x.scaleVelocity(fade);
            track.y.scaleVelocity(fade);
        } else {
            const float trustX = velocityTrust(track.x.velocity, track.x.pVelocity);
            const float trustY = velocityTrust(track.y.velocity, track.y.pVelocity);
            track.x.scaleVelocity(trustX + (1.0f - trustX) * fade);
            track.y.scaleVelocity(trustY + (1.0f - trustY) * fade);
        }
    }

    // The steady filter holds a still or straight-running target's box steady; the agile one follows it through a turn.
    const float boxHeight = std::max(track.height.value, 1.0f);
    const float steadyVariance = square(config_.steadyAccelerationPerBox * boxHeight);
    const float agileVariance = square(config_.agileAccelerationPerBox * boxHeight);
    track.x.predict(dt, steadyVariance, agileVariance, config_.steadyHoldS, config_.agileHoldS, egoX,
                    square(config_.egoUncertainty * egoX));
    track.y.predict(dt, steadyVariance, agileVariance, config_.steadyHoldS, config_.agileHoldS, egoY,
                    square(config_.egoUncertainty * egoY));
    track.width.predict(dt, square(config_.sizeDriftPerSecond * track.width.value));
    track.height.predict(dt, square(config_.sizeDriftPerSecond * track.height.value));
    track.stateNs = toNs;
}

float Tracker::velocityTrust(float velocity, float velocityVariance) const {
    const float squared = velocity * velocity;
    return squared / (squared + config_.velocityDoubt * velocityVariance + 1e-6f);
}

float Tracker::measurementVariance(float boxHeight, float score) const {
    // A less confident box is a noisier measurement.
    const float scoreNoise = 1.0f + 4.0f * square(1.0f - std::min(score, 1.0f));
    const float sigma = std::max(config_.minPositionSigmaPx, config_.positionSigmaPerBox * boxHeight);
    return square(sigma) * scoreNoise;
}

void Tracker::correctTrack(Track& track, const Detection& detection) const {
    const float centerX = detection.x + detection.width * 0.5f;
    const float centerY = detection.y + detection.height * 0.5f;

    const float scoreNoise = 1.0f + 4.0f * square(1.0f - std::min(detection.score, 1.0f));
    float varianceX = measurementVariance(track.height.value, detection.score);
    float varianceY = varianceX;
    float varianceWidth = square(config_.sizeSigmaPerBox * track.width.value) * scoreNoise;
    float varianceHeight = square(config_.sizeSigmaPerBox * track.height.value) * scoreNoise;

    // Where the box is cut off by the scanned region, the true centre and size lie further out.
    if (detection.clipFlags & (kClipLeft | kClipRight)) {
        varianceX *= kClippedVarianceScale;
        varianceWidth *= kClippedVarianceScale;
    }
    if (detection.clipFlags & (kClipTop | kClipBottom)) {
        varianceY *= kClippedVarianceScale;
        varianceHeight *= kClippedVarianceScale;
    }

    // Missed for longer than the hold time, the target's speed has faded (see predictTrack). The way it went while
    // unseen measures its pace better: a runner coming out from behind cover is followed at its speed at once, where
    // the faded estimate left the aim trailing for a third of a second, and a target that stopped behind the cover is
    // not pushed on.
    const int64_t unseenNs = track.stateNs - track.lastSeenNs;
    const bool returning = track.hits > 0 && track.missNs > holdNs() && unseenNs > 0;

    // A sudden large size change means a different part of the target became visible (it peeked
    // out, ducked behind cover, or a leg or arm came into view). Follow it at once instead of
    // blending, and re-anchor the centre so the jump is not mistaken for the target running.
    const bool widthChanged = track.hits > 0 && std::fabs(detection.width - track.width.value) >
                                                    config_.sizeSnapRatio * track.width.value;
    const bool heightChanged = track.hits > 0 && std::fabs(detection.height - track.height.value) >
                                                     config_.sizeSnapRatio * track.height.value;
    if (widthChanged) {
        track.width.reset(detection.width, varianceWidth);
        track.x.reanchor(centerX, varianceX);
    } else {
        track.x.update(centerX, varianceX);
        track.width.update(detection.width, varianceWidth);
    }
    if (heightChanged) {
        track.height.reset(detection.height, varianceHeight);
        track.y.reanchor(centerY, varianceY);
    } else {
        track.y.update(centerY, varianceY);
        track.height.update(detection.height, varianceHeight);
    }

    if (returning) {
        float egoX = 0.0f;
        float egoY = 0.0f;
        if (egoMotion_ != nullptr) {
            egoMotion_->shiftBetween(track.lastSeenNs, track.stateNs, &egoX, &egoY);
        }
        // Its doubt: the error of two box centres, and of the camera's turn over the gap, spread over the gap. A box
        // that came back another size shows another part of the target, so its centre moved without the target.
        const float gapS = nsToSeconds(unseenNs);
        if (!widthChanged) {
            track.x.restartVelocity((centerX - track.seenX - egoX) / gapS,
                                    (2.0f * varianceX + square(config_.egoUncertainty * egoX)) / square(gapS));
        }
        if (!heightChanged) {
            track.y.restartVelocity((centerY - track.seenY - egoY) / gapS,
                                    (2.0f * varianceY + square(config_.egoUncertainty * egoY)) / square(gapS));
        }
    }

    // A speed no person can reach is a matching mistake; keep it from flinging the box.
    const float maxSpeed = config_.maxSpeedPerBox * track.height.value;
    track.x.clampVelocity(maxSpeed);
    track.y.clampVelocity(maxSpeed);

    track.score = track.hits == 0 ? detection.score : 0.6f * track.score + 0.4f * detection.score;
    track.hits += 1;
    track.lastSeenNs = track.stateNs;
    track.seenX = centerX;
    track.seenY = centerY;
    track.missNs = 0;
    track.unobservedNs = 0;
    track.phase = track.hits >= config_.confirmHits ? TrackPhase::kTracked : TrackPhase::kTentative;
}

void Tracker::startTrack(const Detection& detection, int64_t captureNs) {
    Track& track = tracks_[trackCount_++];
    track = Track();
    track.id = nextId_++;
    track.classId = detection.classId;
    track.phase = TrackPhase::kTentative;
    track.stateNs = captureNs;

    const float positionSigma =
        std::max(config_.minPositionSigmaPx, config_.positionSigmaPerBox * detection.height);
    const float speedSigma = config_.initialSpeedPerBox * detection.height;
    track.x.reset(detection.x + detection.width * 0.5f, square(positionSigma), square(speedSigma));
    track.y.reset(detection.y + detection.height * 0.5f, square(positionSigma), square(speedSigma));
    track.width.reset(detection.width, square(config_.sizeSigmaPerBox * detection.width));
    track.height.reset(detection.height, square(config_.sizeSigmaPerBox * detection.height));

    correctTrack(track, detection);
}

float Tracker::pairCost(const Track& track, const Detection& detection, const MatchGate& gate) const {
    if (track.classId != detection.classId) {
        return kBlockedCost;
    }
    const float trackArea = track.width.value * track.height.value;
    const float detectionArea = detection.width * detection.height;
    const float areaRatio = std::max(trackArea, detectionArea) / std::max(1.0f, std::min(trackArea, detectionArea));
    if (areaRatio > config_.maxAreaRatio) {
        return kBlockedCost;
    }

    const float detCenterX = detection.x + detection.width * 0.5f;
    const float detCenterY = detection.y + detection.height * 0.5f;

    // A target stepping out from cover changes its visible box a lot, but the new box usually
    // lies inside the old one (or the reverse). Counting that containment keeps its identity.
    const Overlap shared = measureOverlap(track.x.position, track.y.position, track.width.value,
                                          track.height.value, detCenterX, detCenterY,
                                          detection.width, detection.height);
    const float overlap = std::max(shared.overUnion(), config_.containmentWeight * shared.overSmaller());
    float cost;
    if (overlap >= gate.minIou) {
        cost = 1.0f - overlap * (gate.fuseScore ? detection.score : 1.0f);
    } else {
        // Small distant boxes can move further than their own size between frames, so overlap
        // alone would lose them. Accept a box that does not overlap only if it lies where the
        // filter expects the target, measured in standard deviations of the filter's own
        // uncertainty. A target stays easy to find after a few missed frames (its uncertainty
        // grows), while a neighbour a body-width away is rejected.
        const float variance = measurementVariance(track.height.value, detection.score);
        const float distanceSq = square((detCenterX - track.x.position)) / (track.x.pPosition + variance) +
                                 square((detCenterY - track.y.position)) / (track.y.pPosition + variance);
        const float sigmas = std::sqrt(distanceSq);
        if (sigmas > gate.gateSigmas) {
            return kBlockedCost;
        }
        cost = 1.0f + 0.5f * sigmas / gate.gateSigmas;
    }
    return cost <= gate.maxCost ? cost : kBlockedCost;
}

int Tracker::matchPass(const int* trackIndices, int trackCount, const int* detectionIndices,
                       int detectionCount, const Detection* detections, const MatchGate& gate,
                       Match* matches) const {
    if (trackCount == 0 || detectionCount == 0) {
        return 0;
    }
    float cost[kMaxTracks * kMaxDetections];
    for (int t = 0; t < trackCount; ++t) {
        for (int d = 0; d < detectionCount; ++d) {
            cost[t * detectionCount + d] =
                pairCost(tracks_[trackIndices[t]], detections[detectionIndices[d]], gate);
        }
    }
    int detectionForTrack[kMaxTracks];
    solveAssignment(cost, trackCount, detectionCount, kBlockedCost, detectionForTrack);

    int matchCount = 0;
    for (int t = 0; t < trackCount; ++t) {
        if (detectionForTrack[t] >= 0) {
            matches[matchCount++] = {trackIndices[t], detectionIndices[detectionForTrack[t]]};
        }
    }
    return matchCount;
}

void Tracker::update(const Detection* detections, int count, int64_t captureNs, const ScanRegion* scanned,
                     int64_t arrivalNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    count = std::max(0, std::min(count, kMaxDetections));
    noteArrival(captureNs, arrivalNs);

    // Time since each target's previous update, used below to age targets that were missed.
    int64_t stepNs[kMaxTracks];
    for (int i = 0; i < trackCount_; ++i) {
        stepNs[i] = std::max<int64_t>(0, captureNs - tracks_[i].stateNs);
        tracks_[i].matchedNow = false;
        predictTrack(tracks_[i], captureNs);
    }

    // Sort usable detections into strong ones (can start and keep targets) and weak ones (keep only).
    int strong[kMaxDetections];
    int weak[kMaxDetections];
    int strongCount = 0;
    int weakCount = 0;
    for (int d = 0; d < count; ++d) {
        if (!isUsable(detections[d])) {
            continue;
        }
        if (detections[d].score >= config_.highScore) {
            strong[strongCount++] = d;
        } else if (detections[d].score >= config_.lowScore) {
            weak[weakCount++] = d;
        }
    }

    bool detectionUsed[kMaxDetections] = {};
    Match matches[kMaxTracks];
    auto applyMatches = [&](int matchCount) {
        for (int m = 0; m < matchCount; ++m) {
            correctTrack(tracks_[matches[m].track], detections[matches[m].detection]);
            tracks_[matches[m].track].matchedNow = true;
            detectionUsed[matches[m].detection] = true;
        }
    };

    // Pass 1: confirmed targets take the strong detections, so a good box always wins.
    int confirmed[kMaxTracks];
    int confirmedCount = 0;
    for (int i = 0; i < trackCount_; ++i) {
        if (tracks_[i].phase != TrackPhase::kTentative) {
            confirmed[confirmedCount++] = i;
        }
    }
    const MatchGate strongGate{config_.minIou, config_.gateSigmas, true, 1.5f};
    applyMatches(matchPass(confirmed, confirmedCount, strong, strongCount, detections, strongGate, matches));

    // Pass 2: targets seen in the previous update may still be held by weak detections (a
    // partly hidden or blurred target). Missed targets are not recovered this way, to avoid
    // latching onto noise.
    int stillTracked[kMaxTracks];
    int stillTrackedCount = 0;
    for (int c = 0; c < confirmedCount; ++c) {
        if (!tracks_[confirmed[c]].matchedNow && tracks_[confirmed[c]].phase == TrackPhase::kTracked) {
            stillTracked[stillTrackedCount++] = confirmed[c];
        }
    }
    int weakAvailable[kMaxDetections];
    int weakAvailableCount = 0;
    for (int w = 0; w < weakCount; ++w) {
        weakAvailable[weakAvailableCount++] = weak[w];
    }
    const MatchGate weakGate{config_.lowStageIou, config_.lowStageGateSigmas, false, 1.5f};
    applyMatches(matchPass(stillTracked, stillTrackedCount, weakAvailable, weakAvailableCount,
                           detections, weakGate, matches));

    // Pass 3: unconfirmed targets need a strong detection that no confirmed target claimed.
    int tentative[kMaxTracks];
    int tentativeCount = 0;
    for (int i = 0; i < trackCount_; ++i) {
        if (tracks_[i].phase == TrackPhase::kTentative) {
            tentative[tentativeCount++] = i;
        }
    }
    int strongLeft[kMaxDetections];
    int strongLeftCount = 0;
    for (int s = 0; s < strongCount; ++s) {
        if (!detectionUsed[strong[s]]) {
            strongLeft[strongLeftCount++] = strong[s];
        }
    }
    const MatchGate tentativeGate{config_.minIou, config_.lowStageGateSigmas, true, 1.25f};
    applyMatches(matchPass(tentative, tentativeCount, strongLeft, strongLeftCount, detections,
                           tentativeGate, matches));

    // Age the targets that found no detection, then drop the ones that stayed lost too long.
    for (int i = trackCount_ - 1; i >= 0; --i) {
        Track& track = tracks_[i];
        if (track.matchedNow) {
            continue;
        }
        if (track.phase == TrackPhase::kTentative) {
            removeTrackAt(i);  // one miss is enough to discard a target that was never confirmed
            continue;
        }
        track.phase = TrackPhase::kCoasting;
        const bool wasScanned = scanned == nullptr || scanned->contains(track.x.position, track.y.position);
        (wasScanned ? track.missNs : track.unobservedNs) += stepNs[i];
        if (track.missNs > coastNs() || track.unobservedNs > config_.unobservedNs) {
            removeTrackAt(i);
        }
    }

    // Start new targets from strong, unclaimed detections that clear the stricter birth score.
    for (int s = 0; s < strongCount && trackCount_ < kMaxTracks; ++s) {
        const Detection& detection = detections[strong[s]];
        if (detectionUsed[strong[s]] || detection.score < config_.birthScore) {
            continue;
        }
        startTrack(detection, captureNs);
        tracks_[trackCount_ - 1].matchedNow = true;
    }

    removeDuplicates();
}

void Tracker::removeDuplicates() {
    for (int i = 0; i < trackCount_; ++i) {
        for (int j = i + 1; j < trackCount_; ++j) {
            const Track& a = tracks_[i];
            const Track& b = tracks_[j];
            // Two targets that each matched their own detection this update are two real targets
            // (one bot standing in front of another), however much their boxes overlap.
            if (a.classId != b.classId || (a.matchedNow && b.matchedNow)) {
                continue;
            }
            const float overlap = measureOverlap(a.x.position, a.y.position, a.width.value, a.height.value,
                                                 b.x.position, b.y.position, b.width.value, b.height.value)
                                      .overUnion();
            if (overlap > config_.duplicateIou) {
                // Keep the one that is still being seen; between two stale ones keep the older.
                const bool dropSecond = a.matchedNow != b.matchedNow ? a.matchedNow : a.hits >= b.hits;
                removeTrackAt(dropSecond ? j : i);
                --i;
                break;
            }
        }
    }
}

void Tracker::removeTrackAt(int index) {
    for (int i = index; i + 1 < trackCount_; ++i) {
        tracks_[i] = tracks_[i + 1];
    }
    --trackCount_;
}

void Tracker::projectTrack(const Track& track, int64_t atNs, TrackView& view) const {
    const int64_t aheadNs = std::max<int64_t>(0, std::min(atNs - track.stateNs, config_.maxExtrapolateNs));
    const float ahead = nsToSeconds(aheadNs);

    float velocityX = track.x.velocity;
    float velocityY = track.y.velocity;
    if (track.phase == TrackPhase::kCoasting) {
        const float keep = std::exp(-ahead / config_.coastVelocityDecayS);
        velocityX *= keep;
        velocityY *= keep;
    }
    // The speed that is carried forward to draw the box is the part of it the filter is sure of.
    const float carriedX = velocityX * velocityTrust(velocityX, track.x.pVelocity);
    const float carriedY = velocityY * velocityTrust(velocityY, track.y.pVelocity);

    // Camera motion is exact, so it is applied over the whole interval; the target's own
    // velocity is only trusted for a short distance beyond the last update.
    float egoX = 0.0f;
    float egoY = 0.0f;
    if (egoMotion_ != nullptr && atNs > track.stateNs) {
        egoMotion_->shiftBetween(track.stateNs, atNs, &egoX, &egoY);
    }

    view.id = track.id;
    view.classId = track.classId;
    view.centerX = track.x.position + carriedX * ahead + egoX;
    view.centerY = track.y.position + carriedY * ahead + egoY;
    view.width = track.width.value;
    view.height = track.height.value;
    view.velocityX = velocityX;
    view.velocityY = velocityY;
    view.velocitySigma = std::sqrt(0.5f * (track.x.pVelocity + track.y.pVelocity));
    view.score = track.score;
    view.phase = track.phase;
    view.hits = track.hits;
    view.lastSeenNs = track.lastSeenNs;

    // Draw at full strength while the target is current, then fade out once it has been unseen
    // longer than the hold time. A confirmed target that stops being updated fades the same way.
    if (track.phase == TrackPhase::kTentative) {
        view.opacity = track.score >= config_.showTentativeScore ? 1.0f : 0.0f;
    } else {
        // What counts as unseen is the time of the frames that missed the target, and the time the detector has
        // been quiet. How old the newest result is for its frame does not count, and neither does the wait for
        // the next one while results come as often as they have: a slow phone would else dim every box between
        // two results, and the assist would let go of a target it is looking right at.
        const int64_t quietNs = std::max<int64_t>(0, atNs - latestArrivalNs_ - kQuietAfterGaps * arrivalGapNs_);
        const int64_t missedNs = track.matchedNow ? 0 : track.missNs + track.unobservedNs;
        const int64_t unseenNs = missedNs + quietNs;
        const int64_t holdLimitNs = holdNs();
        const float fade = nsToSeconds(coastNs() - holdLimitNs);
        view.opacity = unseenNs <= holdLimitNs
                           ? 1.0f
                           : std::max(0.0f, 1.0f - nsToSeconds(unseenNs - holdLimitNs) / std::max(fade, 1e-3f));
    }
}

int Tracker::snapshot(int64_t atNs, TrackView* out, int capacity, bool includeHidden) const {
    std::lock_guard<std::mutex> lock(mutex_);
    int written = 0;
    for (int i = 0; i < trackCount_ && written < capacity; ++i) {
        TrackView view;
        projectTrack(tracks_[i], atNs, view);
        if (includeHidden || view.opacity > 0.0f) {
            out[written++] = view;
        }
    }
    return written;
}

}  // namespace aimbuddy
