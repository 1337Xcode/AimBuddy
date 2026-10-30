#ifndef AIMBUDDY_CORE_TRACKER_H
#define AIMBUDDY_CORE_TRACKER_H

#include <array>
#include <cstdint>
#include <mutex>

#include "ego_motion.h"
#include "kalman.h"

namespace aimbuddy {

/// Most targets followed at once, and most detections accepted per update.
constexpr int kMaxTracks = 24;
constexpr int kMaxDetections = 64;

/// Sides of the scanned region that a detection touches. The box is cut off there.
enum ClipFlag : uint8_t {
    kClipNone = 0,
    kClipLeft = 1,
    kClipRight = 2,
    kClipTop = 4,
    kClipBottom = 8,
};

/// One detector result in screen pixels; (x, y) is the top-left corner of the box.
struct Detection {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float score = 0.0f;
    int classId = 0;
    uint8_t clipFlags = kClipNone;  ///< ClipFlag bits: edges where the box is cut off.
};

/// The part of the screen the detector looked at during one update, in screen pixels.
struct ScanRegion {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    bool contains(float px, float py) const {
        return px >= left && px <= right && py >= top && py <= bottom;
    }
};

/// How much a track can be trusted.
enum class TrackPhase : uint8_t {
    kTentative,  ///< Seen too few times to rule out a false alarm.
    kTracked,    ///< Confirmed and matched in the latest update.
    kCoasting,   ///< Confirmed but missed recently; its position is a prediction.
};

/// A target as it is expected to look at one instant, ready to draw or aim at.
struct TrackView {
    int id = 0;
    int classId = 0;
    float centerX = 0.0f;     ///< Box centre, screen pixels.
    float centerY = 0.0f;
    float width = 0.0f;       ///< Filtered box size, screen pixels.
    float height = 0.0f;
    float velocityX = 0.0f;   ///< Target's own speed in screen pixels per second (camera motion removed).
    float velocityY = 0.0f;
    float velocitySigma = 0.0f; ///< How uncertain the speed estimate is (std. dev., pixels per second).
    float score = 0.0f;       ///< Smoothed detector confidence.
    float opacity = 0.0f;     ///< 1 while seen or recently seen, fading to 0 while coasting; 0 means do not draw.
    TrackPhase phase = TrackPhase::kTentative;
    int hits = 0;             ///< Detections matched so far.
    int64_t lastSeenNs = 0;   ///< When the last matching detection was captured.
};

/// Tuning of the tracker. The defaults suit a 30 to 60 Hz detector and targets of 20 to 400 px.
struct TrackerConfig {
    // --- Matching ---
    float highScore = 0.40f;          ///< Detections at or above this are matched to existing targets first.
    float lowScore = 0.12f;           ///< Detections between lowScore and highScore only keep tracked targets alive.
    float birthScore = 0.50f;         ///< A new target needs a detection at least this confident.
    int confirmHits = 3;              ///< Matches needed before a new target counts as real.
    float showTentativeScore = 0.90f; ///< A tentative target this confident is drawn right away.
    float minIou = 0.05f;             ///< Boxes overlapping less than this are matched by distance instead.
    float gateSigmas = 4.0f;          ///< Non-overlapping boxes match only within this many standard deviations
                                      ///< of where the target is expected, so a neighbour is never mistaken for it.
    float lowStageIou = 0.20f;        ///< Minimum overlap in the low confidence pass.
    float lowStageGateSigmas = 2.5f;  ///< The same limit, tighter, in the low confidence pass.
    float maxAreaRatio = 25.0f;       ///< Boxes whose areas differ by more than this never match. Wide on
                                      ///< purpose: a target peeking from cover changes area a lot at once.
    float containmentWeight = 0.6f;   ///< Counts a box lying inside the other as this fraction of a full overlap.
    float sizeSnapRatio = 0.25f;      ///< A size change bigger than this is a visibility change, not noise.
    float duplicateIou = 0.85f;       ///< Two targets overlapping this much are the same one; the newer is dropped.

    // --- Timing ---
    int64_t holdNs = 120'000'000;         ///< Keep drawing a missed target at full opacity this long.
    int64_t coastNs = 350'000'000;        ///< Forget a missed target after this long.
    int64_t unobservedNs = 900'000'000;   ///< Forget a target outside the scanned region after this long.
    int64_t maxExtrapolateNs = 150'000'000; ///< Never project a position further ahead than this.
    int64_t recallNs = 1'000'000'000;     ///< A forgotten target seen again within this long of its last sighting, where
                                          ///< its path could have taken it, is the same target: it keeps its identity and
                                          ///< is shown at once, at the speed it covered the gap with.
    float recallReachPerBox = 1.0f;       ///< How far off that path it may show, in box heights.
    float coastVelocityDecayS = 0.15f;    ///< Time constant at which a missed target's velocity fades.
    float velocityDoubt = 1.0f;           ///< How far a target's speed is trusted when carried forward to be drawn: it counts for
                                          ///< speed squared over speed squared plus this many times its own variance. A speed no
                                          ///< bigger than the filter's doubt about it is noise, and carrying it on would make
                                          ///< the box wander; a clear one is carried on whole.

    // --- Motion model (all scaled by the target's box height, so distance does not matter) ---
    float steadyAccelerationPerBox = 4.0f; ///< Std. dev. of acceleration, box heights per second squared, of a target holding its course
                                          ///< (standing, or running straight), which is smoothed hard so its box does not wander...
    float agileAccelerationPerBox = 20.0f; ///< ...and of one turning sharply (a strafe changing direction, a stop, a start).
    float steadyHoldS = 1.0f;             ///< How long a target typically holds its course before it turns...
    float agileHoldS = 0.4f;              ///< ...and how long a turn typically takes. Together they set how readily the filter
                                          ///< switches between the two kinds of motion (see ManeuveringFilter).
    float positionSigmaPerBox = 0.03f;    ///< Std. dev. of the detector's centre error, in box heights.
    float sizeSigmaPerBox = 0.06f;        ///< Std. dev. of the detector's size error, as a fraction of size.
    float sizeDriftPerSecond = 0.20f;     ///< How fast the true box size may change, as a fraction per second.
    float initialSpeedPerBox = 3.0f;      ///< Std. dev. of the unknown starting speed, box heights per second.
    float maxSpeedPerBox = 15.0f;         ///< Speeds above this are treated as errors and clamped.
    float minPositionSigmaPx = 1.0f;      ///< Floor for the centre error so tiny boxes stay well behaved.
    float egoUncertainty = 0.2f;          ///< How far off the known camera movement may be, as a fraction of it.
                                          ///< The game's sensitivity is only estimated, so after a big camera
                                          ///< move the position is corrected from the measurement and the error
                                          ///< is not mistaken for target speed.

    /**
     * Thresholds derived from the single "minimum confidence" setting the player sees.
     *
     * The setting is the score a detection needs to start a new target. Weaker detections are
     * still used, but only to keep targets that are already followed, so a higher setting means
     * fewer false alarms without making real targets flicker.
     */
    static TrackerConfig forConfidence(float confidence);
};

/**
 * Follows detected targets from update to update.
 *
 * Each update takes the detector's boxes and does four things:
 *  1. moves every known target forward to the time the frame was captured;
 *  2. pairs boxes with targets, strongest detections first, then lets weaker detections keep
 *     targets that would otherwise be lost (the idea behind ByteTrack);
 *  3. refines each paired target with a Kalman filter, which smooths the box and measures
 *     the target's speed;
 *  4. starts new targets from unpaired strong detections and retires targets that stayed
 *     unseen for too long.
 *
 * A target that goes unseen for a few frames keeps moving along its predicted path and stays
 * on screen, so a single missed detection never makes a box blink.
 *
 * update() and snapshot() may be called from different threads.
 */
class Tracker {
public:
    explicit Tracker(const TrackerConfig& config = TrackerConfig());

    /// Replaces the tuning. Existing targets keep their state.
    void setConfig(const TrackerConfig& config);

    /// Sets where camera motion comes from. Pass nullptr for a fixed camera. Not owned.
    void setEgoMotion(const EgoMotion* egoMotion);

    /**
     * Feeds one frame's detections into the tracker.
     *
     * `captureNs` is when the frame was on screen (steady clock), which is earlier than now by
     * the pipeline delay. `scanned` is the region the detector examined; targets outside it are
     * not counted as missed. Pass nullptr when the whole screen was scanned. `arrivalNs` is when
     * the result reached the tracker, on the same clock; from it the tracker learns how often
     * results come, and so how long a target that one of them missed can still be trusted. A
     * negative `arrivalNs` says the result arrived the instant the frame was captured.
     */
    void update(const Detection* detections, int count, int64_t captureNs, const ScanRegion* scanned,
                int64_t arrivalNs = -1);

    /**
     * Writes the expected state of every target at `atNs`, in the order the targets were found.
     * Returns how many targets were written. Targets are projected forward from their last
     * update using their speed and the camera motion, so boxes can be drawn at display rate.
     * Targets that are not drawn (a new one not yet sure, a faded one) are left out unless
     * `includeHidden` is set, which is for whoever needs to know where anything might be.
     */
    int snapshot(int64_t atNs, TrackView* out, int capacity, bool includeHidden = false) const;

    /// Forgets every target.
    void reset();

private:
    struct Track {
        int id = 0;
        int classId = 0;
        TrackPhase phase = TrackPhase::kTentative;
        ManeuveringFilter x;
        ManeuveringFilter y;
        LevelFilter width;
        LevelFilter height;
        float score = 0.0f;
        int hits = 0;
        int64_t stateNs = 0;      ///< The instant the filters describe.
        int64_t lastSeenNs = 0;   ///< Capture time of the last matched detection.
        int64_t bornNs = 0;       ///< Capture time of its first detection...
        int64_t bornAfterNs = 0;  ///< ...and of the update before that one (0 for none).
        float seenX = 0.0f;       ///< Where the box centre was then, screen pixels.
        float seenY = 0.0f;
        float seenVelocityX = 0.0f; ///< Its own speed then, screen pixels per second, before any fading.
        float seenVelocityY = 0.0f;
        int64_t missNs = 0;       ///< Time unseen while inside the scanned region.
        int64_t unobservedNs = 0; ///< Time unseen while outside the scanned region.
        bool matchedNow = false;  ///< Paired with a detection in the latest update.
    };

    /// A confirmed target that was forgotten, kept for a while in case it shows again (see TrackerConfig::recallNs).
    struct LostTrack {
        int id = 0;
        int classId = 0;
        int hits = 0;
        int64_t seenNs = 0;
        float seenX = 0.0f;
        float seenY = 0.0f;
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    /// Two sightings of one target with a gap between them, from which its speed is measured again.
    struct Gap {
        float fromX = 0.0f;      ///< Box centre at the earlier sighting, screen pixels.
        float fromY = 0.0f;
        int64_t fromNs = 0;      ///< Capture time of the earlier sighting.
        float toX = 0.0f;        ///< Box centre now.
        float toY = 0.0f;
        float varianceX = 0.0f;  ///< Variance of one box centre on each axis.
        float varianceY = 0.0f;
        bool keepX = true;       ///< The box kept its width: the change of its centre along x is movement.
        bool keepY = true;
    };

    struct Match {
        int track;
        int detection;
    };

    /// How strictly one matching pass pairs targets with detections.
    struct MatchGate {
        float minIou;       ///< Overlap above which a pair is scored by overlap.
        float gateSigmas;   ///< Distance limit, in standard deviations, for pairs below minIou.
        bool fuseScore;     ///< Prefer confident detections by weighting overlap with the score.
        float maxCost;      ///< Pairs costing more than this are rejected.
    };

    float velocityTrust(float velocity, float velocityVariance) const;
    float measurementVariance(float boxHeight, float score) const;
    void predictTrack(Track& track, int64_t toNs) const;
    void correctTrack(Track& track, const Detection& detection) const;
    void startTrack(const Detection& detection, int64_t captureNs);
    float pairCost(const Track& track, const Detection& detection, const MatchGate& gate) const;
    int matchPass(const int* trackIndices, int trackCount, const int* detectionIndices,
                  int detectionCount, const Detection* detections, const MatchGate& gate,
                  Match* matches) const;
    void projectTrack(const Track& track, int64_t atNs, TrackView& view) const;
    void removeDuplicates();
    void removeTrackAt(int index);
    void rememberLost(const Track& track, int64_t nowNs);
    float pathDistance(const LostTrack& lost, const Detection& detection, int64_t captureNs, float reachPerBox) const;
    void takeOverJumpedTargets(int64_t captureNs);
    bool findJumpedTarget(int64_t captureNs, int* freshIndex, int* missingIndex) const;
    bool isFirstDrawn(const Track& track) const;
    static LostTrack lostEntryFor(const Track& track);
    void recall(Track& track, const LostTrack& lost, const Detection& detection) const;
    void restartVelocityFromGap(Track& track, const Gap& gap) const;

    /// Learns how often results arrive (see update()).
    void noteArrival(int64_t captureNs, int64_t arrivalNs);
    /// How long a missed target is still drawn at full strength, and how long until it is forgotten. The
    /// configured times are for a detector that runs about 30 times a second; a slower one gets longer ones.
    int64_t holdNs() const;
    int64_t coastNs() const;

    TrackerConfig config_;
    const EgoMotion* egoMotion_ = nullptr;
    std::array<Track, kMaxTracks> tracks_{};
    int trackCount_ = 0;
    std::array<LostTrack, kMaxTracks> lost_{};
    int lostCount_ = 0;
    int nextId_ = 1;
    bool hasArrival_ = false;
    int64_t latestArrivalNs_ = 0;   ///< When the newest result arrived.
    int64_t lastUpdateNs_ = 0;      ///< Capture time of the latest update (0 before the first).
    int64_t arrivalGapNs_ = 0;      ///< Smoothed time between two results; 0 until there have been two.
    mutable std::mutex mutex_;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_TRACKER_H
