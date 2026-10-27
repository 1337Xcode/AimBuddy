#ifndef AIMBUDDY_TEST_SIMULATED_GAME_H
#define AIMBUDDY_TEST_SIMULATED_GAME_H

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

#include "core/aim_controller.h"
#include "core/camera_motion.h"
#include "test_harness.h"

namespace simulation {

using namespace aimbuddy;

constexpr int64_t kMs = 1'000'000;

/**
 * A tiny simulated game for judging the aim controller.
 *
 * A target moves through the world, the camera turns when the assist drags its finger (after a
 * delay and by an amount the controller does not know in advance), and the screen shows the
 * target where the world position plus the accumulated camera slide puts it. Frames are detected
 * with noise, delay and dropouts and fed to a real Tracker. Between frames the slide of the scene
 * is measured, with a little noise, the way the odometry on captured frames reports it.
 */
class SimulatedGame {
public:
    struct Truth {
        float gain = 1.5f;            ///< How much the scene really slides per finger pixel.
        float delayS = 0.05f;         ///< How long the game really takes to show a finger move.
        int detectionPeriodMs = 33;   ///< Time between detector frames (33 is 30 Hz, 16 is 60 Hz).
        float noisePx = 2.0f;         ///< Std. dev. of the detector's error on the box centre.
        int detectionLatencyMs = 15;  ///< Time from capture until the detector's boxes reach the tracker.
        int gameFrameMs = 16;         ///< A finger move is applied by the game over this long.
        bool measureCamera = true;    ///< Whether the slide of the scene between frames is measured.
        float measureNoisePx = 0.7f;  ///< Std. dev. of that measurement.
        float dropoutRate = 0.10f;    ///< Share of frames in which the detector misses the target.
    };

    /// What the controller believes before it has measured anything.
    struct Belief {
        float gain = 1.5f;
        float delayS = 0.05f;
        bool learn = true;
    };

    using Path = std::function<void(float tSeconds, float* x, float* y)>;

    SimulatedGame(Truth truth, AimSettings settings, Path path, uint32_t seed = 1)
        : SimulatedGame(truth, settings, std::move(path), seed, Belief()) {}

    SimulatedGame(Truth truth, AimSettings settings, Path path, uint32_t seed, Belief belief)
        : truth_(truth), controller_(settings, &motion_), path_(std::move(path)), random_(seed) {
        controller_.setCrosshair(kCrosshairX, kCrosshairY);
        motion_.setPrior({belief.gain, belief.gain, belief.delayS, false}, belief.learn);
        tracker_.setEgoMotion(&motion_);
        profile_ = ModelProfile::defaultsFor(1);
    }

    /// Runs for `seconds`; calls onSample(tSeconds, errorX, errorY) once a millisecond.
    void run(float seconds, const std::function<void(float, float, float)>& onSample) {
        const int64_t steps = static_cast<int64_t>(seconds * 1000.0f);
        for (int64_t ms = 0; ms < steps; ++ms) {
            const int64_t nowNs = (ms + kStartMs) * kMs;
            applyDueCameraMotion(nowNs);
            if (ms % truth_.detectionPeriodMs == 0) {
                captureFrame(nowNs);
            }
            deliverDetections(nowNs);
            if (ms % kControlPeriodMs == 0) {
                runController(nowNs);
            }
            float x, y;
            aimPointOnScreen(&x, &y);
            onSample(static_cast<float>(ms) * 0.001f, x - kCrosshairX, y - kCrosshairY);
        }
    }

    int fingerReversals() const { return reversals_; }

    /// Replaces the tracker's tuning, to judge a setting.
    void setTrackerConfig(const TrackerConfig& config) { tracker_.setConfig(config); }

    CameraMotion& camera() { return motion_; }
    AimController& controller() { return controller_; }

private:
    static constexpr float kCrosshairX = 1200.0f;
    static constexpr float kCrosshairY = 540.0f;
    static constexpr float kBoxWidth = 60.0f;
    static constexpr float kBoxHeight = 180.0f;
    static constexpr int kControlPeriodMs = 8;      // 125 Hz
    static constexpr int64_t kStartMs = 1000;       // the clock starts well after zero, like the steady clock does

    struct PendingMotion { int64_t startNs; float dx, dy; };
    struct PendingDetection {
        int64_t deliverNs;
        int64_t captureNs;
        Detection detection;
        bool present;
        float cameraX;   ///< Where the camera was when the frame was captured.
        float cameraY;
    };

    void worldPosition(int64_t nowNs, float* x, float* y) const {
        path_(static_cast<float>(nowNs - kStartMs * kMs) * 1e-9f, x, y);
    }

    /// Head point of the target as it appears on screen at the current moment.
    void aimPointOnScreen(float* x, float* y) const {
        *x = worldX_ + cameraX_;
        *y = worldY_ + cameraY_;
    }

    void applyDueCameraMotion(int64_t nowNs) {
        worldPosition(nowNs, &worldX_, &worldY_);
        // Moves take effect linearly over one game frame, starting after the true delay.
        for (auto& m : pending_) {
            if (nowNs >= m.startNs && nowNs < m.startNs + truth_.gameFrameMs * kMs) {
                cameraX_ += m.dx / static_cast<float>(truth_.gameFrameMs);
                cameraY_ += m.dy / static_cast<float>(truth_.gameFrameMs);
            }
        }
    }

    void captureFrame(int64_t nowNs) {
        float x, y;
        aimPointOnScreen(&x, &y);
        PendingDetection pd;
        pd.captureNs = nowNs;
        pd.deliverNs = nowNs + truth_.detectionLatencyMs * kMs;
        pd.present = random_.uniform() > truth_.dropoutRate;
        pd.cameraX = cameraX_;
        pd.cameraY = cameraY_;
        const float score = static_cast<float>(0.5 + 0.45 * random_.uniform());
        const float cx = x + static_cast<float>(random_.gaussian(truth_.noisePx));
        const float cy = y + 0.4f * kBoxHeight + static_cast<float>(random_.gaussian(truth_.noisePx));
        pd.detection.x = cx - kBoxWidth * 0.5f;
        pd.detection.y = cy - kBoxHeight * 0.5f;
        pd.detection.width = kBoxWidth * static_cast<float>(1.0 + random_.gaussian(0.04));
        pd.detection.height = kBoxHeight * static_cast<float>(1.0 + random_.gaussian(0.04));
        pd.detection.score = score;
        queue_.push_back(pd);
    }

    void deliverDetections(int64_t nowNs) {
        while (!queue_.empty() && queue_.front().deliverNs <= nowNs) {
            const PendingDetection pd = queue_.front();
            queue_.erase(queue_.begin());
            // The pipeline records how the scene slid since the previous frame before it tracks.
            if (lastCaptureNs_ > 0) {
                const float slideX = pd.cameraX - lastCameraX_ + static_cast<float>(random_.gaussian(truth_.measureNoisePx));
                const float slideY = pd.cameraY - lastCameraY_ + static_cast<float>(random_.gaussian(truth_.measureNoisePx));
                motion_.addMeasurement(lastCaptureNs_, pd.captureNs, slideX, slideY, truth_.measureCamera ? 1.0f : 0.0f);
            }
            lastCaptureNs_ = pd.captureNs;
            lastCameraX_ = pd.cameraX;
            lastCameraY_ = pd.cameraY;
            if (pd.present) {
                Detection d = pd.detection;
                tracker_.update(&d, 1, pd.captureNs, nullptr);
            } else {
                tracker_.update(nullptr, 0, pd.captureNs, nullptr);
            }
        }
    }

    void runController(int64_t nowNs) {
        const AimOutput out = controller_.step(nowNs, tracker_, profile_, PlayerInput());
        if (out.action == AimAction::kTouchDown) {
            fingerX_ = out.x;
            fingerY_ = out.y;
        } else if (out.action == AimAction::kTouchMove) {
            const float dx = out.x - fingerX_;
            const float dy = out.y - fingerY_;
            fingerX_ = out.x;
            fingerY_ = out.y;
            // The scene slides the opposite way to the finger, by the TRUE gain.
            pending_.push_back({nowNs + static_cast<int64_t>(truth_.delayS * 1e9f), -truth_.gain * dx, -truth_.gain * dy});
            if (dx * lastDx_ < 0.0f) {
                ++reversals_;
            }
            if (dx != 0.0f) {
                lastDx_ = dx;
            }
        }
    }

    Truth truth_;
    CameraMotion motion_;
    AimController controller_;
    Tracker tracker_;
    ModelProfile profile_;
    Path path_;
    testing::Random random_;
    std::vector<PendingMotion> pending_;
    std::vector<PendingDetection> queue_;
    float worldX_ = 0, worldY_ = 0, cameraX_ = 0, cameraY_ = 0;
    float fingerX_ = 0, fingerY_ = 0, lastDx_ = 0;
    int reversals_ = 0;
    int64_t lastCaptureNs_ = 0;
    float lastCameraX_ = 0, lastCameraY_ = 0;
};

/// A target standing still, offset from the crosshair.
inline SimulatedGame::Path stationary(float offsetX, float offsetY) {
    return [=](float, float* x, float* y) {
        *x = 1200.0f + offsetX;
        *y = 540.0f + offsetY;
    };
}

/// A target swinging sideways around the crosshair; peak speed is 2 * pi * hertz * amplitude.
inline SimulatedGame::Path strafingPath(float hertz, float amplitudePx) {
    return [=](float t, float* x, float* y) {
        *x = 1200.0f + amplitudePx * std::sin(6.283185f * hertz * t);
        *y = 540.0f;
    };
}

inline AimSettings baseSettings() {
    AimSettings settings;
    settings.anchorX = 1800.0f;
    settings.anchorY = 500.0f;
    settings.fovRadiusPx = 500.0f;
    return settings;
}

/// How the aim behaved in one run.
struct Outcome {
    float settleSeconds = -1.0f;   ///< First time the error stayed under 8 px for good (-1 if never).
    float finalRms = 0.0f;         ///< RMS error over the last second.
    int zeroCrossings = 0;         ///< Times the horizontal error changed sign after first reaching the target.
    float maxOvershoot = 0.0f;     ///< Furthest the error went past zero on the starting side, in px.
    float peakError = 0.0f;        ///< Largest distance between the aim point and the crosshair.
};

/// Runs a game and summarises how the aim behaved.
inline Outcome measure(SimulatedGame& game, float seconds, float startErrorX) {
    Outcome outcome;
    double sumSquares = 0.0;
    int samples = 0;
    float lastError = startErrorX;
    bool underTarget = false;
    float underSince = -1.0f;
    game.run(seconds, [&](float t, float ex, float ey) {
        const float magnitude = std::hypot(ex, ey);
        outcome.peakError = std::max(outcome.peakError, magnitude);
        if (magnitude < 8.0f) {
            if (!underTarget) {
                underTarget = true;
                underSince = t;
            }
        } else {
            underTarget = false;
            underSince = -1.0f;
        }
        if (lastError * ex < 0.0f && std::fabs(ex) > 3.0f) {
            ++outcome.zeroCrossings;
        }
        if (std::fabs(ex) > 3.0f) {
            lastError = ex;
        }
        if (startErrorX != 0.0f && ex * startErrorX < 0.0f) {
            outcome.maxOvershoot = std::max(outcome.maxOvershoot, std::fabs(ex));
        }
        if (t >= seconds - 1.0f) {
            sumSquares += static_cast<double>(magnitude) * magnitude;
            ++samples;
        }
    });
    outcome.settleSeconds = underTarget ? underSince : -1.0f;
    outcome.finalRms = samples > 0 ? static_cast<float>(std::sqrt(sumSquares / samples)) : 0.0f;
    return outcome;
}

/// RMS distance between the aim point and the crosshair from `fromSeconds` to the end of the run.
inline double rmsError(SimulatedGame& game, float seconds, float fromSeconds) {
    double sumSquares = 0.0;
    int samples = 0;
    game.run(seconds, [&](float t, float ex, float ey) {
        if (t > fromSeconds) {
            sumSquares += static_cast<double>(ex) * ex + static_cast<double>(ey) * ey;
            ++samples;
        }
    });
    return std::sqrt(sumSquares / samples);
}

}  // namespace simulation

#endif  // AIMBUDDY_TEST_SIMULATED_GAME_H
