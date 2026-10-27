#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "core/aim_controller.h"
#include "test_harness.h"

using namespace aimbuddy;

namespace {

constexpr int64_t kMs = 1'000'000;

/**
 * A tiny simulated game for judging the aim controller.
 *
 * A target moves through the world, the camera turns when the assist drags its finger (after a
 * delay and by an amount the controller does not know exactly), and the screen shows the target
 * where the world position plus the accumulated camera slide puts it. Frames are detected at 30 Hz
 * with noise, delay and dropouts and fed to a real Tracker.
 */
class SimulatedGame {
public:
    struct Truth {
        float gain = 1.5f;        ///< How much the scene really slides per finger pixel.
        float delayS = 0.05f;     ///< How long the game really takes to show a finger move.
        int detectionPeriodMs = 33;   ///< Time between detector frames (33 is 30 Hz, 16 is 60 Hz).
        float noisePx = 2.0f;     ///< Std. dev. of the detector's error on the box centre.
        int detectionLatencyMs = 15;  ///< Time from capture until the detector's boxes reach the tracker.
        int gameFrameMs = 16;     ///< A finger move is applied by the game over this long.
    };

    using Path = std::function<void(float tSeconds, float* x, float* y)>;

    SimulatedGame(Truth truth, AimSettings settings, Path path, uint32_t seed = 1)
        : truth_(truth), controller_(settings), path_(std::move(path)), random_(seed) {
        controller_.setCrosshair(kCrosshairX, kCrosshairY);
        tracker_.setEgoMotion(&controller_.commandedMotion());
        profile_ = ModelProfile::defaultsFor(1);
    }

    /// Runs for `seconds`; calls onSample(tSeconds, errorX, errorY) once a millisecond.
    void run(float seconds, const std::function<void(float, float, float)>& onSample) {
        const int64_t steps = static_cast<int64_t>(seconds * 1000.0f);
        for (int64_t ms = 0; ms < steps; ++ms) {
            const int64_t nowNs = ms * kMs;
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

private:
    static constexpr float kCrosshairX = 1200.0f;
    static constexpr float kCrosshairY = 540.0f;
    static constexpr float kBoxWidth = 60.0f;
    static constexpr float kBoxHeight = 180.0f;
    static constexpr int kControlPeriodMs = 8;      // 125 Hz

    struct PendingMotion { int64_t startNs; float dx, dy; };
    struct PendingDetection { int64_t deliverNs; int64_t captureNs; Detection detection; bool present; };

    void worldPosition(int64_t nowNs, float* x, float* y) const {
        path_(static_cast<float>(nowNs) * 1e-9f, x, y);
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
        pd.present = random_.uniform() > 0.10;                    // 10% of frames miss the target
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
            pending_.push_back({nowNs + static_cast<int64_t>(truth_.delayS * 1e9f),
                                -truth_.gain * dx, -truth_.gain * dy});
            if (dx * lastDx_ < 0.0f) {
                ++reversals_;
            }
            if (dx != 0.0f) {
                lastDx_ = dx;
            }
        }
    }

    Truth truth_;
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
};

/// A target standing still, offset from the crosshair.
SimulatedGame::Path stationary(float offsetX, float offsetY) {
    return [=](float, float* x, float* y) {
        *x = 1200.0f + offsetX;
        *y = 540.0f + offsetY;
    };
}

AimSettings baseSettings() {
    AimSettings settings;
    settings.anchorX = 1800.0f;
    settings.anchorY = 500.0f;
    settings.sceneGain = 1.5f;
    settings.actuationDelayS = 0.05f;
    settings.fovRadiusPx = 500.0f;
    return settings;
}

struct Outcome {
    float settleSeconds = -1.0f;   ///< First time the error stayed under 8 px for good (-1 if never).
    float finalRms = 0.0f;         ///< RMS error over the last second.
    int zeroCrossings = 0;         ///< Times the horizontal error changed sign after first reaching the target.
    float maxOvershoot = 0.0f;     ///< Furthest the error went past zero on the starting side, in px.
};

/// Runs a game and summarises how the aim behaved.
Outcome measure(SimulatedGame& game, float seconds, float startErrorX) {
    Outcome outcome;
    double sumSquares = 0.0;
    int samples = 0;
    float lastError = startErrorX;
    bool underTarget = false;
    float underSince = -1.0f;
    game.run(seconds, [&](float t, float ex, float ey) {
        const float magnitude = std::hypot(ex, ey);
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
double rmsError(SimulatedGame& game, float seconds, float fromSeconds) {
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

/// A target swinging sideways around the crosshair; peak speed is 2 * pi * hertz * amplitude.
SimulatedGame::Path strafingPath(float hertz, float amplitudePx) {
    return [=](float t, float* x, float* y) {
        *x = 1200.0f + amplitudePx * std::sin(6.283185f * hertz * t);
        *y = 540.0f;
    };
}

}  // namespace

TEST(commanded_motion_integrates_overlapping_movements) {
    CommandedMotion motion;
    motion.record(100 * kMs, 200 * kMs, 10.0f, -20.0f);       // spread over 100 ms
    motion.record(300 * kMs, 300 * kMs, 5.0f, 5.0f);          // an instant movement
    float dx, dy;
    motion.shiftBetween(0, 1000 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 15.0, 1e-4);
    CHECK_NEAR(dy, -15.0, 1e-4);
    motion.shiftBetween(150 * kMs, 250 * kMs, &dx, &dy);       // half of the first movement
    CHECK_NEAR(dx, 5.0, 1e-4);
    CHECK_NEAR(dy, -10.0, 1e-4);
    motion.shiftBetween(300 * kMs, 400 * kMs, &dx, &dy);       // the instant one is in (from, to]
    CHECK_NEAR(dx, 0.0, 1e-4);
    motion.shiftBetween(299 * kMs, 300 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 5.0, 1e-4);
    motion.shiftBetween(500 * kMs, 600 * kMs, &dx, &dy);       // nothing there
    CHECK_NEAR(dx, 0.0, 1e-4);
    motion.clear();
    motion.shiftBetween(0, 1000 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 0.0, 1e-4);
}

TEST(commanded_motion_keeps_only_a_bounded_history) {
    CommandedMotion motion;
    for (int i = 0; i < 1000; ++i) {
        motion.record(i * kMs, (i + 1) * kMs, 1.0f, 0.0f);
    }
    float dx, dy;
    motion.shiftBetween(0, 2000 * kMs, &dx, &dy);
    CHECK(dx <= 128.5f && dx >= 127.5f);   // only the newest 128 entries remain
}

TEST(aim_settles_on_a_still_target_without_wobble) {
    SimulatedGame game({1.5f, 0.05f}, baseSettings(), stationary(300.0f, -150.0f));
    const Outcome result = measure(game, 3.0f, 300.0f);
    CHECK(result.settleSeconds >= 0.0f);
    CHECK(result.settleSeconds < 1.0f);        // on target within a second
    CHECK(result.finalRms < 6.0f);             // and then it stays there
    CHECK(result.zeroCrossings <= 1);          // at most one pass through zero
    CHECK(result.maxOvershoot < 25.0f);
}

TEST(aim_stays_stable_when_the_gain_estimate_is_wrong) {
    // The controller believes the scene slides 1.5 px per finger px; in reality it slides from
    // 0.6 to 2.5 px. The loop stays stable while the real gain is under twice the believed one.
    // Being wrong costs time and some overshoot, never a lost target or a growing swing.
    const float truths[] = {0.6f, 1.0f, 1.5f, 2.5f};
    for (const float truth : truths) {
        SimulatedGame game({truth, 0.05f}, baseSettings(), stationary(280.0f, 120.0f));
        const Outcome result = measure(game, 4.0f, 280.0f);
        CHECK(result.settleSeconds >= 0.0f);
        CHECK(result.settleSeconds < 2.5f);
        CHECK(result.finalRms < 8.0f);
        CHECK(result.maxOvershoot < 0.3f * 280.0f);
    }
}

TEST(aim_stays_stable_when_the_delay_estimate_is_wrong) {
    // The controller assumes 50 ms; the game really takes between 15 and 110 ms. A target standing
    // still is settled on in every case; moving targets are covered by the next test.
    const float truths[] = {0.015f, 0.03f, 0.08f, 0.11f};
    for (const float truth : truths) {
        SimulatedGame game({1.5f, truth}, baseSettings(), stationary(-260.0f, 90.0f));
        const Outcome result = measure(game, 4.0f, -260.0f);
        CHECK(result.finalRms < 10.0f);
        CHECK(result.zeroCrossings <= 3);
    }
}

TEST(aim_follows_a_target_running_sideways) {
    // The target runs at 300 px/s. The camera follows it, so on screen it barely moves; its real
    // speed can only be known by removing the camera's own movement.
    SimulatedGame::Path running = [](float t, float* x, float* y) {
        *x = 1100.0f + 300.0f * t;
        *y = 540.0f;
    };
    SimulatedGame game({1.5f, 0.05f}, baseSettings(), running);
    double sumSquares = 0.0;
    int samples = 0;
    game.run(4.0f, [&](float t, float ex, float ey) {
        if (t > 1.2f) {
            sumSquares += static_cast<double>(ex) * ex + static_cast<double>(ey) * ey;
            ++samples;
        }
    });
    CHECK(std::sqrt(sumSquares / samples) < 18.0);   // within about 18 px while it keeps running
}

TEST(aim_tracks_a_target_that_strafes_back_and_forth) {
    // 0.5 Hz with a 140 px swing: the target reverses every second and reaches 440 px/s.
    SimulatedGame game({1.5f, 0.05f}, baseSettings(), strafingPath(0.5f, 140.0f));
    CHECK(rmsError(game, 5.0f, 1.5f) < 40.0);
}

TEST(aim_keeps_following_a_moving_target_when_the_delay_estimate_is_off) {
    // The same strafe, with the game taking 20 ms and 80 ms where 50 ms is assumed. The aim is
    // somewhat worse, never lost.
    const float truths[] = {0.02f, 0.08f};
    for (const float truth : truths) {
        SimulatedGame game({1.5f, truth}, baseSettings(), strafingPath(0.5f, 140.0f));
        CHECK(rmsError(game, 5.0f, 1.5f) < 45.0);
    }
}

TEST(aim_helps_even_against_a_violent_strafe) {
    // 0.9 Hz with the same swing reaches 790 px/s and reverses in a quarter of a second. A detector
    // that sees the target 30 times a second cannot follow it all the way, but the assist must
    // still bring the crosshair clearly closer than leaving the camera alone.
    AimSettings off = baseSettings();
    off.fovRadiusPx = 0.0f;   // nothing is ever in range, so the assist never moves the finger
    SimulatedGame unassisted({1.5f, 0.05f}, off, strafingPath(0.9f, 140.0f));
    SimulatedGame assisted({1.5f, 0.05f}, baseSettings(), strafingPath(0.9f, 140.0f));
    const double without = rmsError(unassisted, 5.0f, 1.5f);
    const double with = rmsError(assisted, 5.0f, 1.5f);
    CHECK(with < 0.85 * without);
}

TEST(aim_lets_go_when_no_target_is_left) {
    AimController controller(baseSettings());
    controller.setCrosshair(1200.0f, 540.0f);
    Tracker tracker;
    const ModelProfile profile = ModelProfile::defaultsFor(1);
    for (int i = 0; i < 6; ++i) {
        Detection d;
        d.x = 1400.0f;
        d.y = 400.0f;
        d.width = 60.0f;
        d.height = 180.0f;
        d.score = 0.9f;
        tracker.update(&d, 1, i * 16 * kMs, nullptr);
    }
    AimOutput out = controller.step(100 * kMs, tracker, profile, PlayerInput());
    CHECK(out.action == AimAction::kTouchDown);
    CHECK(controller.isTouching());
    CHECK(controller.lockedTrackId() >= 0);

    // The target disappears for good; once the grace period is over the finger must be up and the lock gone.
    for (int i = 0; i < 200; ++i) {
        tracker.update(nullptr, 0, (7 + i) * 16 * kMs, nullptr);
        controller.step((101 + i * 8) * kMs, tracker, profile, PlayerInput());
    }
    CHECK(!controller.isTouching());
    CHECK(controller.lockedTrackId() == -1);
}

TEST(aim_release_lifts_the_finger_at_once) {
    AimController controller(baseSettings());
    controller.setCrosshair(1200.0f, 540.0f);
    Tracker tracker;
    const ModelProfile profile = ModelProfile::defaultsFor(1);
    for (int i = 0; i < 6; ++i) {
        Detection d;
        d.x = 1400.0f;
        d.y = 400.0f;
        d.width = 60.0f;
        d.height = 180.0f;
        d.score = 0.9f;
        tracker.update(&d, 1, i * 16 * kMs, nullptr);
    }
    CHECK(controller.step(100 * kMs, tracker, profile, PlayerInput()).action == AimAction::kTouchDown);
    controller.release();
    CHECK(controller.step(108 * kMs, tracker, profile, PlayerInput()).action == AimAction::kTouchUp);
    CHECK(!controller.isTouching());
    controller.release();   // releasing when nothing is held does nothing
    CHECK(controller.step(116 * kMs, tracker, profile, PlayerInput()).action == AimAction::kNone);
}

TEST(aim_ignores_targets_outside_the_field_of_view) {
    AimSettings settings = baseSettings();
    settings.fovRadiusPx = 150.0f;
    AimController controller(settings);
    controller.setCrosshair(1200.0f, 540.0f);
    Tracker tracker;
    const ModelProfile profile = ModelProfile::defaultsFor(1);
    for (int i = 0; i < 6; ++i) {
        Detection d;
        d.x = 1700.0f;   // 500 px from the crosshair
        d.y = 400.0f;
        d.width = 60.0f;
        d.height = 180.0f;
        d.score = 0.9f;
        tracker.update(&d, 1, i * 16 * kMs, nullptr);
    }
    CHECK(controller.step(100 * kMs, tracker, profile, PlayerInput()).action == AimAction::kNone);
    CHECK(!controller.isTouching());
}

TEST(aim_keeps_its_target_when_a_rival_is_only_slightly_closer) {
    AimController controller(baseSettings());
    controller.setCrosshair(1200.0f, 540.0f);
    Tracker tracker;
    const ModelProfile profile = ModelProfile::defaultsFor(1);
    auto feed = [&](int64_t t, float leftX, float rightX) {
        Detection d[2];
        d[0].x = leftX; d[0].y = 400; d[0].width = 60; d[0].height = 180; d[0].score = 0.9f;
        d[1].x = rightX; d[1].y = 400; d[1].width = 60; d[1].height = 180; d[1].score = 0.9f;
        tracker.update(d, 2, t, nullptr);
    };
    for (int i = 0; i < 6; ++i) {
        feed(i * 16 * kMs, 1230.0f, 1380.0f);   // the left one is nearer (centre 30 px right of the crosshair)
    }
    controller.step(100 * kMs, tracker, profile, PlayerInput());
    const int firstTarget = controller.lockedTrackId();
    CHECK(firstTarget >= 0);
    // The other enemy now stands only slightly closer; the lock must stay for a long while.
    for (int i = 6; i < 40; ++i) {
        feed(i * 16 * kMs, 1235.0f, 1225.0f);
        controller.step((101 + i * 8) * kMs, tracker, profile, PlayerInput());
    }
    CHECK(controller.lockedTrackId() == firstTarget);
}

TEST(a_controller_without_delay_compensation_circles_the_target) {
    // Negative control. The same simulated game, steered by the obvious approach: take the newest
    // measured error and move a fraction of the way each step, ignoring that earlier moves have not
    // shown up on screen yet. It must overshoot and ring, which shows the tests above are sharp
    // enough to notice that failure mode. The game takes 80 ms; the real controller assumes 50.
    SimulatedGame::Truth truth{1.5f, 0.08f};
    AimSettings settings = baseSettings();
    settings.actuationDelayS = 0.0f;       // "no delay compensation": it believes moves land at once
    settings.strength = 1.0f;
    SimulatedGame naive(truth, settings, stationary(300.0f, 0.0f));
    const Outcome ringing = measure(naive, 4.0f, 300.0f);

    SimulatedGame compensated(truth, baseSettings(), stationary(300.0f, 0.0f));
    const Outcome good = measure(compensated, 4.0f, 300.0f);

    CHECK(good.finalRms < 6.0f);
    CHECK(good.maxOvershoot < 0.25f * ringing.maxOvershoot);
    CHECK(ringing.zeroCrossings >= good.zeroCrossings + 4);
    CHECK(ringing.finalRms > 5.0f * good.finalRms);
}
