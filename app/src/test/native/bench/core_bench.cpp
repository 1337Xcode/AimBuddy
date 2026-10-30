// Benchmarks of the pure C++ core on the development machine. Three kinds:
//  - what each step of a frame costs (colour finder, camera look, tracker, aim step, touch merger, overlay fingerprint);
//  - how the aim controller copes with a camera it was not told about, in the simulated game the tests use;
//  - how closely the aim follows targets that strafe, run and stop, on phones that detect 60, 30 and 15 times a second.
// Build and run: sh app/src/test/native/bench/run.sh [runs], or with "camera" or "follow" for one simulation only.
// The timings are for this machine's processor, not a phone's, and the simulation is not a real game.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "core/colour_blobs.h"
#include "core/fingerprint.h"
#include "core/global_motion.h"
#include "core/touch_mux.h"
#include "core/tracker.h"
#include "simulated_game.h"

using namespace aimbuddy;

namespace {

/// Calls done in one timed run of a step that takes about a microsecond, so that the clock's resolution does not show.
constexpr int kBatch = 1000;

/// Milliseconds on the steady clock.
double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Prints the median and the 95th percentile of a step's time, in microseconds.
void report(const char* name, std::vector<double>& samplesUs) {
    std::sort(samplesUs.begin(), samplesUs.end());
    std::printf("| %-48s | %10.3f | %10.3f | %5zu |\n", name, samplesUs[samplesUs.size() / 2], samplesUs[samplesUs.size() * 95 / 100],
                samplesUs.size());
}

/// Times `body` for `runs` runs after ten that are thrown away. A run does `callsPerRun` calls of the step,
/// and the times printed are for one call.
void measure(const char* name, int runs, int callsPerRun, const std::function<void()>& body) {
    for (int i = 0; i < 10; ++i) {
        body();
    }
    std::vector<double> samplesUs;
    for (int i = 0; i < runs; ++i) {
        const double start = nowMs();
        body();
        samplesUs.push_back((nowMs() - start) * 1000.0 / callsPerRun);
    }
    report(name, samplesUs);
}

/// Dull scenery with noise, specks of the vivid colour, and three people outlined in it, side by side.
std::vector<uint8_t> makeColourScene(int side) {
    testing::Random random(7);
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            uint8_t* pixel = &rgba[(static_cast<size_t>(y) * side + x) * 4];
            const int base = 60 + static_cast<int>(random.uniform() * 90.0);
            pixel[0] = static_cast<uint8_t>(base + static_cast<int>(random.uniform() * 30.0));
            pixel[1] = static_cast<uint8_t>(base + static_cast<int>(random.uniform() * 20.0));
            pixel[2] = static_cast<uint8_t>(base - static_cast<int>(random.uniform() * 25.0));
            pixel[3] = 255;
        }
    }
    for (int i = 0; i < side * side / 400; ++i) {
        const int x = static_cast<int>(random.uniform() * side);
        const int y = static_cast<int>(random.uniform() * side);
        uint8_t* pixel = &rgba[(static_cast<size_t>(y) * side + x) * 4];
        pixel[0] = 30;
        pixel[1] = 230;
        pixel[2] = 40;
    }
    const int width = side / 8;
    const int height = side / 3;
    const int top = side / 3;
    for (int person = 0; person < 3; ++person) {
        const int left = side / 10 + person * (side * 3 / 10);
        for (int y = top; y < top + height; ++y) {
            for (int x = left; x < left + width; ++x) {
                const bool onOutline = x < left + 2 || x >= left + width - 2 || y < top + 2 || y >= top + height - 2;
                if (!onOutline) {
                    continue;
                }
                uint8_t* pixel = &rgba[(static_cast<size_t>(y) * side + x) * 4];
                pixel[0] = 25;
                pixel[1] = 235;
                pixel[2] = 35;
            }
        }
    }
    return rgba;
}

/// A textured picture shifted by (dx, dy) pixels, for the camera look to compare.
std::vector<uint8_t> makeTexture(int side, int dx, int dy) {
    std::vector<uint8_t> rgba(static_cast<size_t>(side) * side * 4);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const uint32_t hash = static_cast<uint32_t>((((x + dx) / 6) * 73856093) ^ (((y + dy) / 6) * 19349663));
            const uint8_t value = static_cast<uint8_t>(40 + (hash >> 7) % 180);
            uint8_t* pixel = &rgba[(static_cast<size_t>(y) * side + x) * 4];
            pixel[0] = value;
            pixel[1] = static_cast<uint8_t>(value / 2 + (hash >> 3) % 60);
            pixel[2] = static_cast<uint8_t>(255 - value);
            pixel[3] = 255;
        }
    }
    return rgba;
}

/// The colour finder on a square picture with three outlined people in it.
void benchColourFinder(int runs) {
    for (const int side : {256, 320, 448}) {
        const std::vector<uint8_t> scene = makeColourScene(side);
        ColourBlobFinder finder;
        ColourRange range;
        range.hueDegrees = 123.0f;
        range.minSaturation = 0.6f;
        range.minValue = 0.5f;
        BlobFilter filter;
        filter.minHeightPx = 30;
        ColourBlob blobs[ColourBlobFinder::kMaxBlobs];
        int found = 0;
        char name[96];
        std::snprintf(name, sizeof(name), "colour finder, %d px square (3 people)", side);
        measure(name, runs, 1, [&] { found = finder.find(scene.data(), side, side * 4, range, filter, blobs, ColourBlobFinder::kMaxBlobs); });
        if (found != 3) {
            std::printf("  (the finder found %d patches at %d px, not 3)\n", found, side);
        }
    }
}

/// One camera look: two pictures reduced, compared by phase correlation and polished.
void benchCameraLook(int runs) {
    for (const int side : {256, 448}) {
        GlobalMotionEstimator estimator;
        std::vector<std::vector<uint8_t>> frames;
        for (int i = 0; i < 8; ++i) {
            frames.push_back(makeTexture(side, i * 3, i * 2));
        }
        const MovingBox enemy{side * 0.4f, side * 0.3f, side * 0.6f, side * 0.7f};
        char name[96];
        std::snprintf(name, sizeof(name), "camera look, %d px square (1 enemy left out)", side);
        int next = 0;
        measure(name, runs, 1, [&] { estimator.push(frames[next++ % 8].data(), side, side * 4, &enemy, 1); });
    }
}

/// Six detections that drift a little from one batch to the next.
std::vector<std::vector<Detection>> makeDetectionBatches() {
    std::vector<std::vector<Detection>> batches;
    for (int batch = 0; batch < 16; ++batch) {
        std::vector<Detection> detections;
        for (int i = 0; i < 6; ++i) {
            Detection detection;
            detection.x = 200.0f + i * 90.0f + batch * 2.0f;
            detection.y = 300.0f + (i % 3) * 40.0f;
            detection.width = 60.0f;
            detection.height = 150.0f;
            detection.score = 0.8f;
            detections.push_back(detection);
        }
        batches.push_back(detections);
    }
    return batches;
}

/// The tracker taking a batch of six detections, and handing out a snapshot of its targets.
void benchTracker(int runs) {
    Tracker tracker;
    const std::vector<std::vector<Detection>> batches = makeDetectionBatches();
    const ScanRegion region{0.0f, 0.0f, 2400.0f, 1080.0f};
    int64_t captureNs = 1'000'000'000;
    int next = 0;
    measure("tracker update, 6 detections", runs, kBatch, [&] {
        for (int i = 0; i < kBatch; ++i) {
            captureNs += 16'000'000;
            const std::vector<Detection>& batch = batches[next++ % 16];
            tracker.update(batch.data(), static_cast<int>(batch.size()), captureNs, &region, captureNs + 10'000'000);
        }
    });
    TrackView views[kMaxTracks];
    measure("tracker snapshot, 6 targets", runs, kBatch, [&] {
        for (int i = 0; i < kBatch; ++i) {
            tracker.snapshot(captureNs, views, kMaxTracks);
        }
    });
}

/// One step of the aim controller, with three targets near the crosshair and the finger on its way to one of them.
void benchAimStep(int runs) {
    CameraMotion motion;
    motion.setPrior({1.5f, 1.5f, 0.05f, false}, true);
    // The targets are held for far longer than the run lasts, so that no tracker update is needed between steps.
    TrackerConfig holdLong;
    holdLong.holdNs = 1'000'000'000'000;
    holdLong.coastNs = 2'000'000'000'000;
    holdLong.maxExtrapolateNs = 1'000'000'000'000;
    Tracker tracker(holdLong);
    tracker.setEgoMotion(&motion);
    int64_t nowNs = 1'000'000'000;
    for (int update = 0; update < 6; ++update) {
        Detection detections[3];
        for (int i = 0; i < 3; ++i) {
            detections[i].x = 1100.0f + i * 160.0f;
            detections[i].y = 420.0f + i * 20.0f;
            detections[i].width = 60.0f;
            detections[i].height = 180.0f;
            detections[i].score = 0.8f;
        }
        nowNs += 33'000'000;
        tracker.update(detections, 3, nowNs, nullptr, nowNs);
    }
    AimController controller(simulation::baseSettings(), &motion);
    controller.setCrosshair(1200.0f, 540.0f);
    const ModelProfile profile = ModelProfile::defaultsFor(1);
    const PlayerInput player;
    measure("aim controller step, 3 targets", runs, kBatch, [&] {
        for (int i = 0; i < kBatch; ++i) {
            nowNs += 8'000'000;
            controller.step(nowNs, tracker, profile, player);
        }
    });
}

/// Records and counts what the touch merger writes, so the work cannot be left out.
class CountingSink : public touch::EventSink {
public:
    void emit(uint16_t type, uint16_t code, int32_t value) override { checksum_ += static_cast<int64_t>(type) + code + value; }

    int64_t checksum() const { return checksum_; }

private:
    int64_t checksum_ = 0;
};

/// The merger passing on one report of a real finger moving while the assist's finger is down too.
void benchTouchMerger(int runs) {
    using namespace touch::evdev;
    touch::TouchDeviceInfo info;
    info.slotCount = 10;
    info.axis(kAbsMtPositionX) = {true, 0, 8639};
    info.axis(kAbsMtPositionY) = {true, 0, 19199};
    info.axis(kAbsMtTrackingId) = {true, 0, 65535};
    CountingSink sink;
    touch::TouchMux mux(info, &sink);
    mux.onRealEvent(kEvAbs, kAbsMtTrackingId, 1);
    mux.onRealEvent(kEvAbs, kAbsMtPositionX, 4000);
    mux.onRealEvent(kEvAbs, kAbsMtPositionY, 9000);
    mux.onRealEvent(kEvSyn, kSynReport, 0);
    mux.aimDown(6000, 8000);
    measure("touch merger, one report (2 axes + SYN_REPORT)", runs, kBatch, [&] {
        for (int i = 0; i < kBatch; ++i) {
            mux.aimMove(6000 + i % 50, 8000 + i % 30);
            mux.onRealEvent(kEvAbs, kAbsMtPositionX, 4000 + i % 40);
            mux.onRealEvent(kEvAbs, kAbsMtPositionY, 9000 + i % 40);
            mux.onRealEvent(kEvSyn, kSynReport, 0);
        }
    });
    if (sink.checksum() == 0) {
        std::printf("  (the merger wrote nothing)\n");
    }
}

/// The overlay's check whether a frame looks like the last one drawn: the fingerprint of 60 kB of draw data.
void benchFingerprint(int runs) {
    std::vector<uint8_t> drawData(60 * 1024);
    for (size_t i = 0; i < drawData.size(); ++i) {
        drawData[i] = static_cast<uint8_t>(i * 31u);
    }
    uint64_t fingerprint = kFingerprintStart;
    measure("overlay fingerprint, 60 kB of draw data", runs, 1, [&] {
        fingerprint = mixIntoFingerprint(kFingerprintStart, drawData.data(), drawData.size());
    });
    if (fingerprint == 0) {
        std::printf("  (the fingerprint was zero)\n");
    }
}

/// Seeds each simulated case is run with. One seed's detector noise can make a run settle a tenth of a second early or
/// late, which is as large as most changes worth measuring, so the figures printed are over all of them.
constexpr uint32_t kSeeds[] = {1, 2, 3, 4, 5};

/// What the runs of one simulated camera ended with over all seeds: the average settling time of the runs that settled
/// and how many did, the largest overshoot, and the average of what the controller learned of the camera.
struct CameraRow {
    float settleSeconds = -1.0f;
    int settled = 0;
    float maxOvershoot = 0.0f;
    float learnedGain = 0.0f;
    float learnedDelayS = 0.0f;
};

/// Runs the simulated game for four seconds with a still target (startX, startY) pixels from the crosshair, once per
/// seed. The controller is told 1.5 px of slide per finger px and 50 ms of delay, and with `learn` it may measure better.
CameraRow runCameraRow(float trueGain, float trueDelayS, float startX, float startY, bool learn) {
    simulation::SimulatedGame::Belief belief;
    belief.learn = learn;
    CameraRow row;
    float settleTotal = 0.0f;
    for (const uint32_t seed : kSeeds) {
        simulation::SimulatedGame game({trueGain, trueDelayS}, simulation::baseSettings(), simulation::stationary(startX, startY), seed, belief);
        const simulation::Outcome outcome = simulation::measure(game, 4.0f, startX);
        if (outcome.settleSeconds >= 0.0f) {
            settleTotal += outcome.settleSeconds;
            ++row.settled;
        }
        row.maxOvershoot = std::max(row.maxOvershoot, outcome.maxOvershoot);
        row.learnedGain += game.camera().response().gainX;
        row.learnedDelayS += game.camera().response().delayS;
    }
    const float seeds = static_cast<float>(std::size(kSeeds));
    if (row.settled > 0) {
        row.settleSeconds = settleTotal / static_cast<float>(row.settled);
    }
    row.learnedGain /= seeds;
    row.learnedDelayS /= seeds;
    return row;
}

/// The average settling time as text, with how many runs settled when some never did, or "never" when none did.
void describeSettling(const CameraRow& row, char* text, size_t size) {
    const int seeds = static_cast<int>(std::size(kSeeds));
    if (row.settled == 0) {
        std::snprintf(text, size, "never");
    } else if (row.settled < seeds) {
        std::snprintf(text, size, "%.2f s, %d of %d", row.settleSeconds, row.settled, seeds);
    } else {
        std::snprintf(text, size, "%.2f s", row.settleSeconds);
    }
}

/// Prints one row of the table of simulated cameras.
void printCameraRow(float trueGain, float trueDelayS, float startX, float startY) {
    const CameraRow learning = runCameraRow(trueGain, trueDelayS, startX, startY, true);
    const CameraRow fixed = runCameraRow(trueGain, trueDelayS, startX, startY, false);
    char learningSettle[24];
    char fixedSettle[24];
    describeSettling(learning, learningSettle, sizeof(learningSettle));
    describeSettling(fixed, fixedSettle, sizeof(fixedSettle));
    std::printf("| %4.1f | %3.0f ms | %7s | %4.0f px | %5.2f | %4.0f ms | %7s | %4.0f px |\n", trueGain, trueDelayS * 1000.0f,
                learningSettle, learning.maxOvershoot, learning.learnedGain, learning.learnedDelayS * 1000.0f, fixedSettle,
                fixed.maxOvershoot);
}

/// The aim controller against simulated games whose camera differs from what it was told.
void benchUnknownCamera() {
    std::printf("\n| true gain | true delay | settles, learning | overshoot | learned gain | learned delay | settles, guess kept | overshoot |\n");
    std::printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
    for (const float gain : {0.4f, 0.6f, 1.5f, 2.5f, 4.0f, 6.0f, 8.0f, 12.0f}) {
        printCameraRow(gain, 0.05f, 280.0f, 120.0f);
    }
    for (const float delayS : {0.015f, 0.03f, 0.08f, 0.11f, 0.15f}) {
        printCameraRow(4.0f, delayS, -260.0f, 90.0f);
    }
}

/// A head this far from the crosshair still counts as under it: about the radius of the head of the simulated 180 px figure.
constexpr float kHeadRadiusPx = 12.0f;

/// Moving targets are judged from this far into a run, once the aim has had time to reach them, to the end of the run.
constexpr float kFollowFromS = 1.5f;
constexpr float kFollowRunS = 8.0f;

/// A target that runs at `speedPxS` and turns back every `legSeconds`: a player strafing left and right.
simulation::SimulatedGame::Path strafingLegs(float speedPxS, float legSeconds) {
    return [=](float t, float* x, float* y) {
        const float period = 2.0f * legSeconds;
        const float phase = std::fmod(t, period);
        const float travelled = phase < legSeconds ? phase * speedPxS : (period - phase) * speedPxS;
        *x = 1200.0f - 0.5f * speedPxS * legSeconds + travelled;
        *y = 540.0f;
    };
}

/// A target that runs at `speedPxS` for `legSeconds`, stands for as long, and runs on.
simulation::SimulatedGame::Path stopAndGo(float speedPxS, float legSeconds) {
    return [=](float t, float* x, float* y) {
        const float period = 2.0f * legSeconds;
        const float laps = std::floor(t / period);
        const float phase = t - laps * period;
        *x = 1150.0f + speedPxS * (laps * legSeconds + std::min(phase, legSeconds));
        *y = 540.0f;
    };
}

/// A target running one way at `speedPxS`.
simulation::SimulatedGame::Path running(float speedPxS) {
    return [=](float t, float* x, float* y) {
        *x = 1150.0f + speedPxS * t;
        *y = 540.0f;
    };
}

/// How closely the aim stayed on a moving target: the RMS distance from the head to the crosshair, and the share of the time it was under it.
struct FollowScore {
    double rmsPx = 0.0;
    double onHeadShare = 0.0;
};

/// Runs one moving-target case with each seed and averages what the aim did.
FollowScore runFollowCase(const simulation::SimulatedGame::Truth& truth, const simulation::SimulatedGame::Path& path) {
    FollowScore total;
    for (const uint32_t seed : kSeeds) {
        simulation::SimulatedGame game(truth, simulation::baseSettings(), path, seed);
        double squares = 0.0;
        int samples = 0;
        int onHead = 0;
        game.run(kFollowRunS, [&](float t, float errorX, float errorY) {
            if (t < kFollowFromS) {
                return;
            }
            const double distanceSq = static_cast<double>(errorX) * errorX + static_cast<double>(errorY) * errorY;
            squares += distanceSq;
            ++samples;
            onHead += distanceSq <= kHeadRadiusPx * kHeadRadiusPx ? 1 : 0;
        });
        total.rmsPx += std::sqrt(squares / samples);
        total.onHeadShare += static_cast<double>(onHead) / samples;
    }
    const double seeds = static_cast<double>(std::size(kSeeds));
    total.rmsPx /= seeds;
    total.onHeadShare /= seeds;
    return total;
}

/// A phone that detects `perSecond` times a second, each result ready `latencyMs` after its frame, with the camera looked at 30 times a second.
simulation::SimulatedGame::Truth phoneDetecting(int periodMs, int latencyMs, float noisePx) {
    simulation::SimulatedGame::Truth truth;
    truth.detectionPeriodMs = periodMs;
    truth.detectionLatencyMs = latencyMs;
    truth.noisePx = noisePx;
    truth.dropoutRate = 0.10f;
    truth.cameraPeriodMs = 33;
    return truth;
}

/// The aim controller following targets that move, on phones that detect at three rates.
void benchMovingTargets() {
    struct Phone {
        const char* name;
        simulation::SimulatedGame::Truth truth;
    };
    const Phone phones[] = {
        {"60/s, 20 ms", phoneDetecting(16, 20, 2.0f)},
        {"30/s, 40 ms", phoneDetecting(33, 40, 2.5f)},
        {"15/s, 70 ms", phoneDetecting(66, 70, 3.0f)},
    };
    struct Motion {
        const char* name;
        simulation::SimulatedGame::Path path;
    };
    const Motion motions[] = {
        {"still, 160 px off", simulation::stationary(150.0f, -50.0f)},
        {"strafe 0.5 Hz, 150 px", simulation::strafingPath(0.5f, 150.0f)},
        {"strafe 1 Hz, 100 px", simulation::strafingPath(1.0f, 100.0f)},
        {"strafe 400 px/s, turns every 0.5 s", strafingLegs(400.0f, 0.5f)},
        {"run 300 px/s", running(300.0f)},
        {"run 400 px/s, stop 0.6 s", stopAndGo(400.0f, 0.6f)},
    };
    std::printf("\n| target | detections | RMS error | head under the crosshair |\n");
    std::printf("| --- | --- | ---: | ---: |\n");
    for (const Motion& motion : motions) {
        for (const Phone& phone : phones) {
            const FollowScore score = runFollowCase(phone.truth, motion.path);
            std::printf("| %-36s | %-11s | %6.1f px | %5.1f%% |\n", motion.name, phone.name, score.rmsPx, 100.0 * score.onHeadShare);
        }
    }
}

}  // namespace

/// Runs the step timings, then the simulated cameras and the moving targets. With "camera" or "follow" only that
/// simulation runs.
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "camera") {
        benchUnknownCamera();
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "follow") {
        benchMovingTargets();
        return 0;
    }
    const int runs = argc > 1 ? std::atoi(argv[1]) : 200;
    std::printf("| step | median (us) | 95th percentile (us) | runs |\n| --- | ---: | ---: | ---: |\n");
    benchColourFinder(runs);
    benchCameraLook(runs);
    benchTracker(runs);
    benchAimStep(runs);
    benchTouchMerger(runs);
    benchFingerprint(runs);
    benchUnknownCamera();
    benchMovingTargets();
    return 0;
}
