#ifndef AIMBUDDY_CORE_AIM_CONTROLLER_H
#define AIMBUDDY_CORE_AIM_CONTROLLER_H

#include <array>
#include <atomic>
#include <cstdint>

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
    float deadzoneBoxFraction = 0.05f;  ///< ...or this fraction of the target's height, whichever is larger.
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

    // Target choice.
    int selectTarget(int64_t nowNs, const TrackView* views, int count, const ModelProfile& profile);
    void rankTargets(const TrackView* views, int count, const ModelProfile& profile, Candidate* best,
                     Candidate* locked) const;
    int keepOrSwitchLock(int64_t nowNs, const TrackView* views, const Candidate& best, const Candidate& locked);
    bool waitingForLostTarget(int64_t nowNs);
    void forgetTarget();

    // One control step.
    float advanceClock(int64_t nowNs);
    bool beginPursuit(int64_t nowNs, const CameraResponse& response, const Tracker& tracker,
                      const ModelProfile& profile, Pursuit* pursuit);
    AimOutput putFingerDown(const Pursuit& pursuit);
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
