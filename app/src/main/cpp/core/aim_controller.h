#ifndef AIMBUDDY_CORE_AIM_CONTROLLER_H
#define AIMBUDDY_CORE_AIM_CONTROLLER_H

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "aim_point.h"
#include "camera_motion.h"
#include "model_profile.h"
#include "tracker.h"

namespace aimbuddy {

/// How the assist behaves. Every field is either a player preference or a property of the screen.
struct AimSettings {
    // --- Player preferences ---
    AimZone zone = AimZone::kHead;      ///< Which part of a target to steer towards.
    float strength = 0.6f;              ///< 0 to 1: how quickly the aim closes on a target.
    float fovRadiusPx = 220.0f;         ///< Targets further than this from the crosshair are left alone.
    int priority = 0;                   ///< Which target to prefer: 0 closest to the crosshair, 1 biggest, 2 most certain.
    float yieldWhileLooking = 0.4f;     ///< Fraction of the assist kept while the player is steering themselves.

    // --- Where the assist's finger rests ---
    float anchorX = 1800.0f;            ///< Display pixels. Should be in the game's look area, away from the player's thumb.
    float anchorY = 400.0f;
    float anchorRadiusPx = 250.0f;      ///< The finger never travels further than this from its anchor.

    // --- Fine tuning ---
    float maxFingerSpeedPxS = 3200.0f;  ///< Ceiling on finger speed at full strength.
    float deadzoneMinPx = 2.0f;         ///< No movement when this close to the aim point...
    float deadzoneBoxFraction = 0.04f;  ///< ...or this fraction of the target's height, whichever is larger.
    float lockLossSeconds = 0.15f;      ///< How long a lost target is waited for before letting go.
    float switchRatio = 0.7f;           ///< A rival must be this much closer to take over the lock...
    float switchHoldSeconds = 0.15f;    ///< ...for this long.
};

/// What the controller wants done with the assist's finger this step.
enum class AimAction : uint8_t {
    kNone,       ///< Nothing to do.
    kTouchDown,  ///< Put the finger down at (x, y).
    kTouchMove,  ///< Slide the finger to (x, y).
    kTouchUp,    ///< Lift the finger.
};

struct AimOutput {
    AimAction action = AimAction::kNone;
    float x = 0.0f;
    float y = 0.0f;
};

/// What the player is doing with their own hands, when it is known.
struct PlayerInput {
    bool lookActive = false;  ///< The player's own finger is steering the camera.
};

/// What the controller saw and did on its latest step. For diagnostics, tests and the overlay.
struct AimTelemetry {
    int targetId = -1;          ///< The target being followed, or -1.
    float errorX = 0.0f;        ///< From the crosshair to the aim point, screen pixels, as the controller judged it.
    float errorY = 0.0f;
    float deadzone = 0.0f;
    float movingShare = 0.0f;   ///< 0 for a target standing still, 1 for one clearly running.
    float stability = 1.0f;
    float fingerX = 0.0f;       ///< Where the assist's finger is, display pixels.
    float fingerY = 0.0f;
    bool touching = false;
    int deadFingers = 0;        ///< Times the finger was lifted and put down again because the game did not answer it.
    float deadFingerPx = 0.0f;  ///< For the latest such time: how far the finger had moved (net, along its main axis)...
    float deadSlidePx = 0.0f;   ///< ...and how far the scene had slid along it.
    int giveUps = 0;            ///< Times a target was given up on because it did not come closer however the camera turned.
};

/**
 * Decides, many times a second, how far to drag the assist's finger.
 *
 * It is a feedback controller built on four ideas that make the aim settle quickly and stay still:
 *
 *  1. Delay compensation. A finger move shows up on screen only after the game has turned the
 *     camera, one or two frames later. Judging by what the screen shows now would keep ordering
 *     the same correction again and again, and the aim would circle the target. Every move is
 *     recorded in the CameraMotion, which tells the tracker how far the scene has slid and will
 *     slide, and the controller aims where the target will be when this step's move takes effect.
 *  2. Lead. The tracker's speed estimate, free of the camera's own movement, is used to predict
 *     where a moving target will be at that moment.
 *  3. Closing, not chasing. The error is reduced by a fraction each step (an exponential
 *     approach), capped by a finger speed limit, with a dead zone around the aim point so
 *     detector noise does not make the finger twitch.
 *  4. Caution while the camera is unknown. How far the scene slides per finger pixel differs
 *     between games and sensitivity settings by a factor of ten or more, and a controller that
 *     assumes the wrong figure overshoots and swings. Until the camera's response has been
 *     measured (see CameraMotion), only a small amount of finger travel is allowed to be on its
 *     way at a time, so the first move is small and its effect teaches the real figure. If the
 *     aim swings anyway, a stability factor takes the strength down until it stops.
 *
 * A finger the game does not answer (the touch started while the screen was turning, or landed
 * where the game does not look around) would be moved for ever for nothing. When the finger has
 * been moved a good way and the scene has not slid with it, the finger is lifted and put down
 * again, waiting a little longer each time it happens again.
 *
 * A target that is not part of the scene would be chased for ever too: the player's own weapon
 * or character, which the detector sometimes takes for an enemy, stays where it is on the screen
 * however the camera turns, so the aim never gets nearer. When the camera has swept a good way and
 * the target has stayed put on the screen, well away from the crosshair, it is given up on for a
 * while, longer each time it happens again at the same place.
 *
 * The class holds no threads or platform calls: call step() at a steady rate and carry out the
 * returned action. That keeps it testable against a simulated game.
 */
class AimController {
public:
    /// `motion` is where moves are recorded and the camera's response is read from; it outlives the controller.
    AimController(const AimSettings& settings, CameraMotion* motion);

    /// Replaces the settings. Call from the same thread as step().
    void setSettings(const AimSettings& settings) { settings_ = settings; }
    const AimSettings& settings() const { return settings_; }

    /// Where the in-game crosshair is, in display pixels.
    void setCrosshair(float x, float y) {
        crosshairX_ = x;
        crosshairY_ = y;
    }

    /**
     * Runs one control step at time `nowNs` (steady clock) and says what to do with the finger.
     * Reads targets from `tracker`; `profile` says which classes are bodies and heads.
     */
    AimOutput step(int64_t nowNs, const Tracker& tracker, const ModelProfile& profile, const PlayerInput& player);

    /// Lifts the finger at the next step and forgets the current target.
    void release() { releaseRequested_ = true; }

    bool isTouching() const { return touching_; }

    /// Id of the target being followed, or -1. Safe to read from other threads.
    int lockedTrackId() const { return lockedId_.load(std::memory_order_relaxed); }

    /// 0 to 1: how much of its strength the controller currently allows itself (below 1 after swinging).
    float stability() const { return stability_; }

    /// What the latest step saw and did. Read from the thread that calls step().
    const AimTelemetry& telemetry() const { return telemetry_; }

    /**
     * True for a box that was given up on because it stays put on the screen however the camera turns,
     * and for as long as it is likely to be there still: the player's own weapon or character taken
     * for an enemy. Not drawn, not aimed at. Safe to call from any thread, unlike the rest.
     */
    bool isDecoy(const TrackView& view, int64_t nowNs) const;

private:
    /// The target being followed, as it will be when a move made now takes effect.
    struct Pursuit {
        const TrackView* body = nullptr;
        float errorX = 0.0f;       ///< From the crosshair to the aim point, screen pixels.
        float errorY = 0.0f;
        float distance = 0.0f;
        float movingShare = 0.0f;  ///< 0 for a target standing still, 1 for one clearly running.
        float deadzone = 0.0f;     ///< Errors smaller than this are left alone.
        bool onTarget() const { return distance <= deadzone; }
    };

    /// How far to slide the finger this step, in display pixels.
    struct FingerStep {
        float x = 0.0f;
        float y = 0.0f;
    };

    /// A target that could be followed: its place in a snapshot and how it ranks (lower is better).
    struct Candidate {
        int index = -1;
        float metric = 0.0f;
    };

    /// A target that was given up on, and where it was, so that it is not taken up again at once when it is found again.
    struct GivenUp {
        int trackId = -1;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        int64_t untilNs = 0;     ///< Left alone until then.
        int64_t forgottenNs = 0; ///< Forgotten altogether after that, so a later giving up starts from the shortest wait again.
        int times = 0;
    };

    // Target choice.
    int selectTarget(int64_t nowNs, const TrackView* views, int count, const ModelProfile& profile);
    bool isGivenUp(const TrackView& view, int64_t nowNs) const;
    static bool matchesGivenUp(const GivenUp& entry, const TrackView& view);
    bool cannotCloseOn(int64_t nowNs, const Tracker& tracker, const TrackView& body);
    void giveUpOn(int64_t nowNs, const TrackView& seen);
    void forgetWatch();
    void rankTargets(const TrackView* views, int count, const ModelProfile& profile, Candidate* best,
                     Candidate* locked) const;
    int keepOrSwitchLock(int64_t nowNs, const TrackView* views, const Candidate& best, const Candidate& locked);
    bool waitingForLostTarget(int64_t nowNs);
    void forgetTarget();

    // One control step.
    float advanceClock(int64_t nowNs);
    bool beginPursuit(int64_t nowNs, const CameraResponse& response, const Tracker& tracker,
                      const ModelProfile& profile, Pursuit* pursuit);
    AimOutput putFingerDown(int64_t nowNs, const Pursuit& pursuit);
    bool fingerSeemsDead(int64_t nowNs);
    FingerStep planFingerStep(int64_t nowNs, const CameraResponse& response, const Pursuit& pursuit,
                              const PlayerInput& player, float dt) const;
    AimOutput moveFinger(int64_t nowNs, float dt, const Pursuit& pursuit, FingerStep step);
    AimOutput lift();
    void noteFinger();

    // Keeping the aim from swinging.
    void watchForSwinging(int64_t nowNs, float dt, const Pursuit& pursuit);
    void forgetSwinging();

    AimSettings settings_;
    CameraMotion* motion_;
    float crosshairX_ = 0.0f;
    float crosshairY_ = 0.0f;

    // Snapshots of the tracker, kept as members so a step allocates nothing. Pursuit::body points into them.
    std::array<TrackView, kMaxTracks> nowViews_{};
    std::array<TrackView, kMaxTracks> futureViews_{};

    std::atomic<int> lockedId_{-1};
    int64_t lostSinceNs_ = 0;
    int rivalId_ = -1;
    int64_t rivalSinceNs_ = 0;

    AimTelemetry telemetry_;

    // Watching the target being followed for one that stays put on the screen: when the watch began and
    // when the target was last looked at, where it was then, how far apart its places have been, how far
    // the camera has swept, and how many looks in a row have said it is stuck.
    static constexpr int kMaxGivenUp = 6;
    mutable std::mutex givenUpMutex_;   ///< The list is written by the aim thread and read by the overlay's.
    std::array<GivenUp, kMaxGivenUp> givenUp_{};
    std::array<TrackView, kMaxTracks> watchViews_{};
    int watchedId_ = -1;
    int64_t watchStartNs_ = 0;
    int64_t watchLastSeenNs_ = 0;
    float watchMinX_ = 0.0f;
    float watchMaxX_ = 0.0f;
    float watchMinY_ = 0.0f;
    float watchMaxY_ = 0.0f;
    float watchSweptPx_ = 0.0f;
    float watchErrorSumPx_ = 0.0f;
    int watchLooks_ = 0;
    int futileLooks_ = 0;

    // Is the finger being answered? When it went down, and the wait before it may go down again
    // after the game ignored it.
    int64_t downNs_ = 0;
    int64_t ignoredSinceNs_ = 0;   ///< Since when the game has looked to be ignoring the finger; 0 when it has not.
    int64_t mayTouchAfterNs_ = 0;
    int deadStreak_ = 0;

    bool touching_ = false;
    bool releaseRequested_ = false;
    float fingerX_ = 0.0f;
    float fingerY_ = 0.0f;
    int64_t lastStepNs_ = 0;

    // Swing detection: times the error crossed zero by a clear margin, and the side it was last on.
    static constexpr int kMaxCrossings = 8;
    std::array<int64_t, kMaxCrossings> crossingNs_{};
    int crossingCount_ = 0;
    int sideX_ = 0;
    int sideY_ = 0;
    int swingTrackId_ = -1;
    float stability_ = 1.0f;
    int64_t lastCrossingNs_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_AIM_CONTROLLER_H
