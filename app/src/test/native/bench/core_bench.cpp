// Benchmarks of the pure C++ core on the development machine. Two kinds:
//  - what each step of a frame costs (colour finder, camera look, tracker, aim step, touch merger, overlay fingerprint);
//  - how the aim controller copes with a camera it was not told about, in the simulated game the tests use.
// Build and run: sh app/src/test/native/bench/run.sh [runs]
// The timings are for this machine's processor, not a phone's, and the simulation is not a real game.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
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

/// What one run of the simulated game ended with: how the aim behaved and what the controller learned of the camera.
struct CameraCase {
    simulation::Outcome outcome;
    CameraResponse learned;
};

/// Runs the simulated game for four seconds with a still target (startX, startY) pixels from the crosshair.
/// The controller is told 1.5 px of slide per finger px and 50 ms of delay, and with `learn` it may measure better.
CameraCase runCameraCase(float trueGain, float trueDelayS, float startX, float startY, bool learn) {
    simulation::SimulatedGame::Belief belief;
    belief.learn = learn;
    simulation::SimulatedGame game({trueGain, trueDelayS}, simulation::baseSettings(), simulation::stationary(startX, startY), 1, belief);
    CameraCase result;
    result.outcome = simulation::measure(game, 4.0f, startX);
    result.learned = game.camera().response();
    return result;
}

/// The time a run took to settle as text, or "never" for a run that did not.
void describeSettling(const simulation::Outcome& outcome, char* text, size_t size) {
    if (outcome.settleSeconds < 0.0f) {
        std::snprintf(text, size, "never");
        return;
    }
    std::snprintf(text, size, "%.2f s", outcome.settleSeconds);
}

/// Prints one row of the table of simulated cameras.
void printCameraRow(float trueGain, float trueDelayS, float startX, float startY) {
    const CameraCase learning = runCameraCase(trueGain, trueDelayS, startX, startY, true);
    const CameraCase fixed = runCameraCase(trueGain, trueDelayS, startX, startY, false);
    char learningSettle[16];
    char fixedSettle[16];
    describeSettling(learning.outcome, learningSettle, sizeof(learningSettle));
    describeSettling(fixed.outcome, fixedSettle, sizeof(fixedSettle));
    std::printf("| %4.1f | %3.0f ms | %7s | %4.0f px | %5.2f | %4.0f ms | %7s | %4.0f px |\n", trueGain, trueDelayS * 1000.0f,
                learningSettle, learning.outcome.maxOvershoot, learning.learned.gainX, learning.learned.delayS * 1000.0f,
                fixedSettle, fixed.outcome.maxOvershoot);
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

}  // namespace

/// Runs the step timings, then the simulated cameras.
int main(int argc, char** argv) {
    const int runs = argc > 1 ? std::atoi(argv[1]) : 200;
    std::printf("| step | median (us) | 95th percentile (us) | runs |\n| --- | ---: | ---: | ---: |\n");
    benchColourFinder(runs);
    benchCameraLook(runs);
    benchTracker(runs);
    benchAimStep(runs);
    benchTouchMerger(runs);
    benchFingerprint(runs);
    benchUnknownCamera();
    return 0;
}
