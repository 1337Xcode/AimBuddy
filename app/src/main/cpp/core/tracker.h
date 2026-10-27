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
    int confirmHits = 2;              ///< Matches needed before a new target counts as real.
    float showTentativeScore = 0.75f; ///< A tentative target this confident is drawn right away.
    float minIou = 0.05f;             ///< Boxes overlapping less than this are matched by distance instead.
    float centerGate = 1.5f;          ///< Largest centre distance, in mean box sizes, for non-overlapping matches.
    float lowStageIou = 0.20f;        ///< Minimum overlap in the low confidence pass.
    float lowStageCenterGate = 0.75f; ///< Largest centre distance, in mean box sizes, in the low confidence pass.
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
    float coastVelocityDecayS = 0.15f;    ///< Time constant at which a missed target's velocity fades.

    // --- Motion model (all scaled by the target's box height, so distance does not matter) ---
    float accelerationPerBox = 8.0f;      ///< Std. dev. of acceleration, box heights per second squared.
    float positionSigmaPerBox = 0.03f;    ///< Std. dev. of the detector's centre error, in box heights.
    float sizeSigmaPerBox = 0.06f;        ///< Std. dev. of the detector's size error, as a fraction of size.
    float sizeDriftPerSecond = 0.20f;     ///< How fast the true box size may change, as a fraction per second.
    float initialSpeedPerBox = 3.0f;      ///< Std. dev. of the unknown starting speed, box heights per second.
    float maxSpeedPerBox = 15.0f;         ///< Speeds above this are treated as errors and clamped.
    float minPositionSigmaPx = 1.0f;      ///< Floor for the centre error so tiny boxes stay well behaved.
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
     * not counted as missed. Pass nullptr when the whole screen was scanned.
     */
    void update(const Detection* detections, int count, int64_t captureNs, const ScanRegion* scanned);

    /**
     * Writes the expected state of every target at `atNs`, in the order the targets were found.
     * Returns how many targets were written. Targets are projected forward from their last
     * update using their speed and the camera motion, so boxes can be drawn at display rate.
     */
    int snapshot(int64_t atNs, TrackView* out, int capacity) const;

    /// Forgets every target.
    void reset();

private:
    struct Track {
        int id = 0;
        int classId = 0;
        TrackPhase phase = TrackPhase::kTentative;
        PositionVelocityFilter x;
        PositionVelocityFilter y;
        LevelFilter width;
        LevelFilter height;
        float score = 0.0f;
        int hits = 0;
        int64_t stateNs = 0;      ///< The instant the filters describe.
        int64_t lastSeenNs = 0;   ///< Capture time of the last matched detection.
        int64_t missNs = 0;       ///< Time unseen while inside the scanned region.
        int64_t unobservedNs = 0; ///< Time unseen while outside the scanned region.
        bool matchedNow = false;  ///< Paired with a detection in the latest update.
    };

    struct Match {
        int track;
        int detection;
    };

    /// How strictly one matching pass pairs targets with detections.
    struct MatchGate {
        float minIou;       ///< Overlap above which a pair is scored by overlap.
        float centerGate;   ///< Distance limit, in mean box sizes, for pairs below minIou.
        bool fuseScore;     ///< Prefer confident detections by weighting overlap with the score.
        float maxCost;      ///< Pairs costing more than this are rejected.
    };

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

    TrackerConfig config_;
    const EgoMotion* egoMotion_ = nullptr;
    std::array<Track, kMaxTracks> tracks_{};
    int trackCount_ = 0;
    int nextId_ = 1;
    mutable std::mutex mutex_;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_TRACKER_H
