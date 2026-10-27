#ifndef AIMBUDDY_PIPELINE_AIM_LOOP_H
#define AIMBUDDY_PIPELINE_AIM_LOOP_H

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "../core/aim_controller.h"
#include "../core/camera_motion.h"
#include "../core/model_profile.h"
#include "../core/tracker.h"
#include "../touch/touch_link.h"

namespace aimbuddy {

/// What the app wants from the assist at this moment.
struct AimLoopConfig {
    bool enabled = false;          ///< The player's switch for the assist.
    AimSettings aim;
    CameraResponse cameraGuess;    ///< How the camera is believed to answer the finger until it has been measured.
    bool learnCamera = true;       ///< Measure the camera's answer instead of trusting the guess.
    bool hasLearned = false;       ///< An earlier run measured the camera: the two fields below say what it found.
    CameraResponse learned;
    float learnedConfidence = 0.0f;
};

/**
 * Runs the aim controller at a steady rate and carries out what it decides through the touch helper.
 *
 * Each step it reads the targets from the tracker, lets the controller decide how to move the
 * extra finger, and sends that to the helper. It also looks after the connection's manners:
 * asking the helper to take the touchscreen over when the assist is switched on, giving it back
 * when the assist is switched off or anything goes wrong, and keeping the heartbeat going, which
 * the helper requires before it lets an extra finger stay on the screen.
 *
 * It does nothing at all while the assist is off or the helper is not connected.
 */
class AimLoop {
public:
    /// Asked for the current wishes a few times a second; must be cheap and safe to call from this loop's thread.
    using ConfigSource = std::function<AimLoopConfig()>;

    /// Told now and then what the camera's answer to the finger turned out to be, so it can be kept for next time.
    using LearnedSink = std::function<void(const CameraResponse& response, float confidence)>;

    /// What the loop is doing, for the app to show.
    struct Stats {
        bool helperConnected = false;
        touch::protocol::State helperState = touch::protocol::State::kStopped;
        touch::protocol::Reason helperReason = touch::protocol::Reason::kNone;
        bool fingerDown = false;
        int lockedTrack = -1;
    };

    AimLoop(Tracker* tracker, const ModelProfile* profile, touch::TouchLink* link, CameraMotion* motion);
    ~AimLoop() { stop(); }
    AimLoop(const AimLoop&) = delete;
    AimLoop& operator=(const AimLoop&) = delete;

    /// Sets where the loop reads the player's wishes from. Call while stopped.
    void setConfigSource(ConfigSource source) { source_ = std::move(source); }

    /// Sets where the measured camera response is reported. Call while stopped.
    void setLearnedSink(LearnedSink sink) { learnedSink_ = std::move(sink); }

    /// Tells the loop the display's size in pixels and rotation (0 to 3, as Android numbers them). Thread safe.
    void setDisplay(int width, int height, int rotation);

    /// Starts the loop thread. Does nothing if it is running.
    void start();

    /// Stops the loop thread, lifts the extra finger and gives the touchscreen back. Safe to call when stopped.
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_acquire); }
    Stats stats() const;

private:
    struct Display {
        int width = 0;
        int height = 0;
        int rotation = 0;
        uint32_t version = 0;   ///< Counts up with every change, so the loop knows to resend it.
    };

    void run();
    void refreshConfig(int64_t nowNs);
    void reportLearned(int64_t nowNs);
    void syncDisplayWithHelper();
    void stepWhileActive(int64_t nowNs);
    void standDown(int64_t nowNs);
    void forgetFinger(int64_t nowNs);
    void sendOutput(const AimOutput& output);
    void keepHeartbeat(int64_t nowNs, int64_t maxSilenceNs);
    Display currentDisplay() const;

    Tracker* tracker_;
    const ModelProfile* profile_;
    touch::TouchLink* link_;
    CameraMotion* motion_;
    ConfigSource source_;
    LearnedSink learnedSink_;

    AimController controller_;
    AimLoopConfig config_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex displayMutex_;
    Display display_;

    // State owned by the loop thread.
    int64_t lastConfigNs_ = 0;
    int64_t lastLearnedReportNs_ = 0;
    int64_t lastEngageNs_ = 0;
    uint32_t configuredSession_ = 0;
    uint32_t configuredDisplayVersion_ = 0;
    bool wasActive_ = false;    ///< The assist was working on the previous step, so there is something to undo.
    bool wasMerging_ = false;

    std::atomic<bool> fingerDown_{false};
    std::atomic<int> lockedTrack_{-1};
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_PIPELINE_AIM_LOOP_H
