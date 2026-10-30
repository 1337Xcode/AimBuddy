#include <algorithm>
#include <cmath>
#include <vector>

#include "core/tracker.h"
#include "test_harness.h"

using aimbuddy::Detection;
using aimbuddy::EgoMotion;
using aimbuddy::ScanRegion;
using aimbuddy::TrackPhase;
using aimbuddy::Tracker;
using aimbuddy::TrackView;

namespace {

constexpr int64_t kMs = 1'000'000;

/// Builds a detection from a centre and size, the way tests think about targets.
Detection box(float cx, float cy, float w, float h, float score = 0.8f, int classId = 0) {
    Detection d;
    d.x = cx - w * 0.5f;
    d.y = cy - h * 0.5f;
    d.width = w;
    d.height = h;
    d.score = score;
    d.classId = classId;
    return d;
}

/// Returns the tracker's view of the target nearest to (x, y), or false if nothing is drawn.
bool viewNear(const Tracker& tracker, int64_t atNs, float x, float y, TrackView* out) {
    TrackView views[aimbuddy::kMaxTracks];
    const int n = tracker.snapshot(atNs, views, aimbuddy::kMaxTracks);
    float best = 1e9f;
    bool found = false;
    for (int i = 0; i < n; ++i) {
        const float d = std::hypot(views[i].centerX - x, views[i].centerY - y);
        if (d < best) {
            best = d;
            *out = views[i];
            found = true;
        }
    }
    return found;
}

int drawnCount(const Tracker& tracker, int64_t atNs) {
    TrackView views[aimbuddy::kMaxTracks];
    return tracker.snapshot(atNs, views, aimbuddy::kMaxTracks);
}

/// Camera that turns at a constant rate, so the scene slides across the screen.
class ConstantPan : public EgoMotion {
public:
    ConstantPan(float pixelsPerSecondX, float pixelsPerSecondY) : vx_(pixelsPerSecondX), vy_(pixelsPerSecondY) {}
    void shiftBetween(int64_t fromNs, int64_t toNs, float* dx, float* dy) const override {
        const float seconds = static_cast<float>(toNs - fromNs) * 1e-9f;
        *dx = vx_ * seconds;
        *dy = vy_ * seconds;
    }

private:
    float vx_;
    float vy_;
};

}  // namespace

TEST(tracker_keeps_a_steady_box_through_dropouts_and_jitter) {
    Tracker tracker;
    testing::Random random(7);
    const float trueX = 500.0f, trueY = 400.0f, trueW = 60.0f, trueH = 180.0f;

    int hiddenFrames = 0;       // frames where the box would have vanished after it first appeared
    int rawHiddenFrames = 0;    // the same for plain per-frame thresholding, as a baseline
    double squaredError = 0.0;
    double outputHeightSquared = 0.0, rawHeightSquared = 0.0;
    int measured = 0;
    bool seenOnce = false;

    for (int frame = 0; frame < 400; ++frame) {
        const int64_t t = frame * 16 * kMs;
        // A quarter of frames miss the target entirely; the rest have a noisy box and score.
        const bool detected = random.uniform() > 0.25;
        const float score = static_cast<float>(0.35 + 0.55 * random.uniform());
        const float noisyH = trueH * static_cast<float>(1.0 + random.gaussian(0.05));
        if (detected) {
            Detection d = box(trueX + static_cast<float>(random.gaussian(2.0)),
                              trueY + static_cast<float>(random.gaussian(2.0)),
                              trueW * static_cast<float>(1.0 + random.gaussian(0.05)), noisyH, score);
            tracker.update(&d, 1, t, nullptr);
        } else {
            tracker.update(nullptr, 0, t, nullptr);
        }

        TrackView view;
        const bool drawn = viewNear(tracker, t + 8 * kMs, trueX, trueY, &view);
        if (drawn) {
            seenOnce = true;
        }
        if (seenOnce && frame > 20) {
            if (!drawn) {
                ++hiddenFrames;
            } else {
                squaredError += std::pow(view.centerX - trueX, 2) + std::pow(view.centerY - trueY, 2);
                outputHeightSquared += std::pow(view.height - trueH, 2);
                ++measured;
            }
            if (!detected || score < 0.5f) {
                ++rawHiddenFrames;
            }
            rawHeightSquared += std::pow(noisyH - trueH, 2);
        }
    }

    CHECK(seenOnce);
    CHECK(hiddenFrames == 0);                      // never blinks
    CHECK(rawHiddenFrames > 60);                   // the naive approach blinks a lot on the same input
    CHECK(measured > 300);
    CHECK_NEAR(std::sqrt(squaredError / measured), 0.0, 2.5);                  // sub-3 px position error
    CHECK(std::sqrt(outputHeightSquared / measured) < 0.6 * std::sqrt(rawHeightSquared / measured));
}

TEST(tracker_predicts_a_moving_target_to_the_display_time) {
    Tracker tracker;
    testing::Random random(11);
    const float speed = 400.0f;  // px/s, a fast strafe
    double lastErrorPredicted = 0.0, lastErrorNaive = 0.0;
    int samples = 0;
    double sumPredicted = 0.0, sumNaive = 0.0;

    for (int frame = 0; frame < 120; ++frame) {
        const int64_t captureNs = frame * 33 * kMs;  // 30 Hz detector
        const float trueX = 300.0f + speed * static_cast<float>(captureNs) * 1e-9f;
        Detection d = box(trueX + static_cast<float>(random.gaussian(2.0)),
                          400.0f + static_cast<float>(random.gaussian(2.0)), 60.0f, 180.0f, 0.85f);
        tracker.update(&d, 1, captureNs, nullptr);

        if (frame > 15) {
            // The frame reaches the screen 80 ms after capture. Compare the drawn position with where the
            // target really is at that moment, against simply drawing the last detection.
            const int64_t displayNs = captureNs + 80 * kMs;
            const float trueNow = 300.0f + speed * static_cast<float>(displayNs) * 1e-9f;
            TrackView view;
            if (viewNear(tracker, displayNs, trueNow, 400.0f, &view)) {
                lastErrorPredicted = std::fabs(view.centerX - trueNow);
                lastErrorNaive = std::fabs(trueX - trueNow);
                sumPredicted += lastErrorPredicted;
                sumNaive += lastErrorNaive;
                ++samples;
            }
        }
    }
    CHECK(samples > 90);
    CHECK(sumNaive / samples > 25.0);                   // the naive box trails by about 32 px
    CHECK(sumPredicted / samples < 8.0);                // the predicted box stays on the target
}

TEST(tracker_keeps_identities_when_two_targets_cross) {
    Tracker tracker;
    int leftId = 0, rightId = 0;
    for (int frame = 0; frame < 90; ++frame) {
        const int64_t t = frame * 16 * kMs;
        const float seconds = static_cast<float>(t) * 1e-9f;
        Detection d[2] = {box(200.0f + 250.0f * seconds, 400.0f, 60.0f, 180.0f),
                          box(800.0f - 250.0f * seconds, 410.0f, 60.0f, 180.0f)};
        tracker.update(d, 2, t, nullptr);
        if (frame == 6) {
            TrackView a, b;
            CHECK(viewNear(tracker, t, d[0].x + 30.0f, 400.0f, &a));
            CHECK(viewNear(tracker, t, d[1].x + 30.0f, 410.0f, &b));
            leftId = a.id;
            rightId = b.id;
            CHECK(leftId != rightId);
        }
    }
    // After crossing, the target that started on the left must still carry the same id on the right.
    const int64_t t = 89 * 16 * kMs;
    const float seconds = static_cast<float>(t) * 1e-9f;
    TrackView movingRight, movingLeft;
    CHECK(viewNear(tracker, t, 200.0f + 250.0f * seconds, 400.0f, &movingRight));
    CHECK(viewNear(tracker, t, 800.0f - 250.0f * seconds, 410.0f, &movingLeft));
    CHECK(movingRight.id == leftId);
    CHECK(movingLeft.id == rightId);
}

TEST(tracker_ignores_a_single_frame_false_alarm) {
    Tracker tracker;
    Detection d = box(300.0f, 300.0f, 50.0f, 150.0f, 0.62f);
    tracker.update(&d, 1, 0, nullptr);
    CHECK(drawnCount(tracker, 1 * kMs) == 0);     // tentative and not confident enough to show yet
    tracker.update(nullptr, 0, 16 * kMs, nullptr);
    tracker.update(nullptr, 0, 32 * kMs, nullptr);
    CHECK(drawnCount(tracker, 40 * kMs) == 0);    // and it never turned into a target
}

TEST(tracker_shows_a_confident_new_target_immediately) {
    Tracker tracker;
    Detection d = box(300.0f, 300.0f, 50.0f, 150.0f, 0.9f);
    tracker.update(&d, 1, 0, nullptr);
    CHECK(drawnCount(tracker, 1 * kMs) == 1);
}

TEST(tracker_uses_weak_detections_to_hold_a_tracked_target) {
    Tracker tracker;
    for (int frame = 0; frame < 6; ++frame) {
        Detection d = box(400.0f, 300.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, frame * 16 * kMs, nullptr);
    }
    // The target is partly hidden: the detector is only 20% sure, below the strong threshold.
    Detection weak = box(402.0f, 300.0f, 60.0f, 180.0f, 0.2f);
    tracker.update(&weak, 1, 6 * 16 * kMs, nullptr);
    TrackView view;
    CHECK(viewNear(tracker, 6 * 16 * kMs, 400.0f, 300.0f, &view));
    CHECK(view.phase == TrackPhase::kTracked);    // still counted as seen, not coasting
}

TEST(tracker_does_not_start_targets_from_weak_detections) {
    Tracker tracker;
    for (int frame = 0; frame < 10; ++frame) {
        Detection weak = box(400.0f, 300.0f, 60.0f, 180.0f, 0.3f);
        tracker.update(&weak, 1, frame * 16 * kMs, nullptr);
    }
    CHECK(drawnCount(tracker, 160 * kMs) == 0);
}

TEST(tracker_coasts_then_fades_a_target_that_goes_missing) {
    Tracker tracker;
    for (int frame = 0; frame < 8; ++frame) {
        Detection d = box(400.0f, 300.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, frame * 16 * kMs, nullptr);
    }
    const int64_t lastSeen = 7 * 16 * kMs;
    // Detector reports nothing from here on.
    for (int frame = 8; frame < 40; ++frame) {
        tracker.update(nullptr, 0, frame * 16 * kMs, nullptr);
    }
    TrackView view;
    // 50 ms after the last sighting the box is still drawn at full strength.
    tracker.update(nullptr, 0, lastSeen + 50 * kMs, nullptr);
    Tracker held;
    for (int frame = 0; frame < 8; ++frame) {
        Detection d = box(400.0f, 300.0f, 60.0f, 180.0f, 0.9f);
        held.update(&d, 1, frame * 16 * kMs, nullptr);
    }
    held.update(nullptr, 0, lastSeen + 50 * kMs, nullptr);
    CHECK(viewNear(held, lastSeen + 50 * kMs, 400.0f, 300.0f, &view));
    CHECK_NEAR(view.opacity, 1.0, 1e-4);
    CHECK(view.phase == TrackPhase::kCoasting);
    // Halfway through the fade it is partly transparent, and past the coast time it is gone.
    CHECK(viewNear(held, lastSeen + 235 * kMs, 400.0f, 300.0f, &view));
    CHECK(view.opacity > 0.2f && view.opacity < 0.8f);
    CHECK(drawnCount(held, lastSeen + 500 * kMs) == 0);
    // The tracker that kept being updated dropped the target for good.
    CHECK(drawnCount(tracker, 40 * 16 * kMs) == 0);
}

TEST(tracker_keeps_a_target_at_full_strength_between_the_results_of_a_slow_detector) {
    // Results come every 250 ms and reach the tracker 200 ms after their frame, so the newest one is never less
    // than 200 ms old. The target it found is still the newest word there is until the next result comes.
    Tracker tracker;
    for (int result = 0; result < 6; ++result) {
        Detection d = box(400.0f, 300.0f, 60.0f, 180.0f, 0.9f);
        const int64_t captureNs = result * 250 * kMs;
        tracker.update(&d, 1, captureNs, nullptr, captureNs + 200 * kMs);
    }
    const int64_t lastArrivalNs = (5 * 250 + 200) * kMs;
    TrackView view;
    for (const int64_t afterMs : {0, 100, 249}) {
        CHECK(viewNear(tracker, lastArrivalNs + afterMs * kMs, 400.0f, 300.0f, &view));
        CHECK_NEAR(view.opacity, 1.0, 1e-4);
    }
}

TEST(tracker_holds_a_target_that_one_result_of_a_slow_detector_missed) {
    // Results every 200 ms. One that finds nothing is no reason to let go of a target for a detector that
    // takes this long, where it would be for one that runs 30 times a second.
    Tracker tracker;
    int64_t captureNs = 0;
    for (int result = 0; result < 6; ++result, captureNs += 200 * kMs) {
        Detection d = box(400.0f, 300.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, captureNs, nullptr, captureNs + 150 * kMs);
    }
    tracker.update(nullptr, 0, captureNs, nullptr, captureNs + 150 * kMs);   // one miss
    TrackView view;
    CHECK(viewNear(tracker, captureNs + 150 * kMs, 400.0f, 300.0f, &view));
    CHECK_NEAR(view.opacity, 1.0, 1e-4);
    CHECK(view.phase == TrackPhase::kCoasting);

    // Three misses in a row (600 ms) leave it fading, still there; a fourth (800 ms) is the end of it.
    for (int miss = 1; miss < 3; ++miss) {
        captureNs += 200 * kMs;
        tracker.update(nullptr, 0, captureNs, nullptr, captureNs + 150 * kMs);
    }
    CHECK(viewNear(tracker, captureNs + 150 * kMs, 400.0f, 300.0f, &view));
    CHECK(view.opacity > 0.0f && view.opacity < 0.6f);
    captureNs += 200 * kMs;
    tracker.update(nullptr, 0, captureNs, nullptr, captureNs + 150 * kMs);
    CHECK(drawnCount(tracker, captureNs + 150 * kMs) == 0);
}

TEST(tracker_fades_a_target_when_a_slow_detector_goes_quiet) {
    // The same detector, 200 ms between results, which then stops altogether. The wait for the next one is
    // not counted for two gaps and then for as long as a target is held, so the box is gone about a second later.
    Tracker tracker;
    for (int result = 0; result < 6; ++result) {
        Detection d = box(400.0f, 300.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, result * 200 * kMs, nullptr, result * 200 * kMs + 150 * kMs);
    }
    const int64_t lastArrivalNs = (5 * 200 + 150) * kMs;
    TrackView view;
    CHECK(viewNear(tracker, lastArrivalNs + 500 * kMs, 400.0f, 300.0f, &view));
    CHECK_NEAR(view.opacity, 1.0, 1e-4);
    CHECK(viewNear(tracker, lastArrivalNs + 800 * kMs, 400.0f, 300.0f, &view));
    CHECK(view.opacity > 0.0f && view.opacity < 1.0f);
    CHECK(drawnCount(tracker, lastArrivalNs + 1200 * kMs) == 0);
}

TEST(tracker_does_not_age_a_target_outside_the_scanned_region) {
    Tracker tracker;
    const ScanRegion wide{0.0f, 0.0f, 1000.0f, 800.0f};
    for (int frame = 0; frame < 6; ++frame) {
        Detection d = box(900.0f, 300.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, frame * 16 * kMs, &wide);
    }
    // The detector now looks at a small region elsewhere; the target is not in it.
    const ScanRegion elsewhere{0.0f, 0.0f, 300.0f, 300.0f};
    for (int frame = 6; frame < 30; ++frame) {
        tracker.update(nullptr, 0, frame * 16 * kMs, &elsewhere);  // 24 frames = 384 ms > coast time
    }
    TrackView views[aimbuddy::kMaxTracks];
    // It is still remembered (not deleted just because we were not looking)...
    Tracker probe;
    for (int frame = 0; frame < 6; ++frame) {
        Detection d = box(900.0f, 300.0f, 60.0f, 180.0f, 0.9f);
        probe.update(&d, 1, frame * 16 * kMs, &wide);
    }
    probe.update(nullptr, 0, 6 * 16 * kMs, &elsewhere);
    CHECK(probe.snapshot(7 * 16 * kMs, views, aimbuddy::kMaxTracks) == 1);
    // ...but a target inside the scanned region that nothing detects is deleted after the coast time.
    Tracker inside;
    for (int frame = 0; frame < 6; ++frame) {
        Detection d = box(200.0f, 200.0f, 60.0f, 180.0f, 0.9f);
        inside.update(&d, 1, frame * 16 * kMs, &elsewhere);
    }
    for (int frame = 6; frame < 30; ++frame) {
        inside.update(nullptr, 0, frame * 16 * kMs, &elsewhere);
    }
    CHECK(inside.snapshot(30 * 16 * kMs, views, aimbuddy::kMaxTracks) == 0);
}

TEST(tracker_separates_target_speed_from_camera_turning) {
    // The camera pans right so the whole scene slides left at 300 px/s on screen. A target standing still
    // in the world therefore moves left on screen at 300 px/s, but its own speed is zero.
    ConstantPan pan(-300.0f, 0.0f);
    Tracker tracker;
    tracker.setEgoMotion(&pan);
    for (int frame = 0; frame < 60; ++frame) {
        const int64_t t = frame * 16 * kMs;
        const float seconds = static_cast<float>(t) * 1e-9f;
        Detection d = box(800.0f - 300.0f * seconds, 400.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, t, nullptr);
    }
    TrackView view;
    const int64_t t = 59 * 16 * kMs;
    CHECK(viewNear(tracker, t, 800.0f - 300.0f * 59.0f * 0.016f, 400.0f, &view));
    CHECK_NEAR(view.velocityX, 0.0, 25.0);

    // And a target running right at 200 px/s while the camera follows (so it stays put on screen) must be
    // measured as running at 200 px/s even though it does not move on screen.
    ConstantPan follow(-200.0f, 0.0f);
    Tracker follower;
    follower.setEgoMotion(&follow);
    for (int frame = 0; frame < 60; ++frame) {
        Detection d = box(600.0f, 400.0f, 60.0f, 180.0f, 0.9f);
        follower.update(&d, 1, frame * 16 * kMs, nullptr);
    }
    CHECK(viewNear(follower, 59 * 16 * kMs, 600.0f, 400.0f, &view));
    CHECK_NEAR(view.velocityX, 200.0, 30.0);
}

TEST(tracker_keeps_the_speed_of_a_target_seen_in_only_some_frames) {
    // A target running at 300 px/s that the detector finds in two frames out of five (half hidden, small, blurred):
    // the frames that miss it say nothing of its speed, and must not wear the estimate down.
    Tracker tracker;
    const bool seen[5] = {true, false, true, false, false};
    for (int frame = 0; frame < 120; ++frame) {
        const int64_t t = frame * 16 * kMs;
        if (seen[frame % 5] || frame < 4) {
            Detection d = box(300.0f + 300.0f * static_cast<float>(t) * 1e-9f, 400.0f, 60.0f, 180.0f, 0.9f);
            tracker.update(&d, 1, t, nullptr);
        } else {
            tracker.update(nullptr, 0, t, nullptr);
        }
    }
    const int64_t lastSeen = 115 * 16 * kMs;   // frame 115 is the last one in which it was found
    TrackView view;
    CHECK(viewNear(tracker, lastSeen, 300.0f + 300.0f * static_cast<float>(lastSeen) * 1e-9f, 400.0f, &view));
    CHECK_NEAR(view.velocityX, 300.0, 30.0);
}

TEST(tracker_draws_a_runner_missed_in_one_frame_where_it_runs) {
    // One frame without the runner says nothing of its speed: the box drawn ahead of the last update must carry on at
    // the runner's pace, not slow down.
    Tracker tracker;
    const auto runnerAt = [](int64_t t) { return 300.0f + 300.0f * static_cast<float>(t) * 1e-9f; };
    for (int frame = 0; frame < 30; ++frame) {
        const int64_t t = frame * 16 * kMs;
        Detection d = box(runnerAt(t), 400.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, t, nullptr);
    }
    tracker.update(nullptr, 0, 30 * 16 * kMs, nullptr);
    const int64_t drawnAt = 30 * 16 * kMs + 60 * kMs;
    TrackView view;
    CHECK(viewNear(tracker, drawnAt, runnerAt(drawnAt), 400.0f, &view));
    CHECK_NEAR(view.velocityX, 300.0, 30.0);
    CHECK_NEAR(view.centerX, runnerAt(drawnAt), 6.0);
}

TEST(tracker_takes_up_a_runner_at_its_pace_when_it_comes_out_from_behind_cover) {
    // A target running at 300 px/s goes behind a wall for 0.3 s: long enough for the speed of the missed target to fade.
    // Two frames after it shows again, the box must already move at the runner's pace, not at what was left of it.
    Tracker tracker;
    TrackView view;
    for (int frame = 0; frame < 60; ++frame) {
        const int64_t t = frame * 16 * kMs;
        const bool hidden = frame >= 30 && frame < 49;   // 0.48 s to 0.78 s
        if (hidden) {
            tracker.update(nullptr, 0, t, nullptr);
            continue;
        }
        Detection d = box(300.0f + 300.0f * static_cast<float>(t) * 1e-9f, 400.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, t, nullptr);
        if (frame == 50) {
            CHECK(viewNear(tracker, t, d.x + d.width * 0.5f, d.y + d.height * 0.5f, &view));
            CHECK_NEAR(view.velocityX, 300.0, 30.0);
        }
    }
}

TEST(tracker_knows_a_runner_again_after_cover_too_long_to_follow_it_through) {
    // Hidden for 0.6 s, longer than a missed target is kept, the runner comes out where its path leads. It is the same
    // target: drawn on its first detection, under its old identity, at its pace. A box far off that path is someone else.
    Tracker tracker;
    TrackView views[aimbuddy::kMaxTracks];
    const auto runnerAt = [](int64_t t) { return 300.0f + 300.0f * static_cast<float>(t) * 1e-9f; };
    for (int frame = 0; frame < 30; ++frame) {
        const int64_t t = frame * 16 * kMs;
        Detection d = box(runnerAt(t), 400.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, t, nullptr);
    }
    CHECK(tracker.snapshot(29 * 16 * kMs, views, aimbuddy::kMaxTracks) == 1);
    const int id = views[0].id;
    for (int frame = 30; frame < 68; ++frame) {
        tracker.update(nullptr, 0, frame * 16 * kMs, nullptr);
    }
    CHECK(tracker.snapshot(67 * 16 * kMs, views, aimbuddy::kMaxTracks) == 0);   // forgotten by now
    const int64_t back = 68 * 16 * kMs;
    const Detection both[2] = {box(runnerAt(back), 400.0f, 60.0f, 180.0f, 0.9f), box(1500.0f, 400.0f, 60.0f, 180.0f, 0.8f)};
    tracker.update(both, 2, back, nullptr);
    CHECK(tracker.snapshot(back, views, aimbuddy::kMaxTracks) == 1);   // the stranger waits for its third sighting
    CHECK(views[0].id == id);
    CHECK_NEAR(views[0].centerX, runnerAt(back), 10.0);
    CHECK_NEAR(views[0].velocityX, 300.0, 30.0);
}

TEST(tracker_follows_a_target_that_peeks_out_of_cover) {
    // A bot stands fully visible, ducks so only its head shows, then rises again.
    Tracker tracker;
    int id = 0;
    for (int frame = 0; frame < 30; ++frame) {
        const int64_t t = frame * 16 * kMs;
        Detection d;
        if (frame < 10) {
            d = box(500.0f, 400.0f, 60.0f, 180.0f);             // full body, centre y = 400
        } else if (frame < 20) {
            d = box(500.0f, 322.0f, 28.0f, 30.0f);              // head only: top edge unchanged (y = 310)
        } else {
            d = box(500.0f, 400.0f, 60.0f, 180.0f);
        }
        tracker.update(&d, 1, t, nullptr);
        TrackView view;
        const bool shown = viewNear(tracker, t, d.x + d.width * 0.5f, d.y + d.height * 0.5f, &view);
        if (frame >= 2) {
            CHECK(shown);   // a new target is drawn from its third sighting on, unless the detector is very sure of it
        }
        if (frame == 5) {
            id = view.id;
        }
        if (frame == 12 || frame == 25) {
            CHECK(view.id == id);                                // identity survives the change
            CHECK_NEAR(view.height, d.height, 6.0);              // box snaps to the visible part, stays tight
            CHECK_NEAR(view.centerY, d.y + d.height * 0.5f, 5.0);
            CHECK_NEAR(view.velocityY, 0.0, 120.0);              // a change in what is visible is not running
        }
    }
    TrackView views[aimbuddy::kMaxTracks];
    CHECK(tracker.snapshot(30 * 16 * kMs, views, aimbuddy::kMaxTracks) == 1);  // no ghost duplicate
}

TEST(tracker_keeps_classes_apart) {
    Tracker tracker;
    // A body and a head box occupy the same place but are different classes; each keeps its own track.
    for (int frame = 0; frame < 6; ++frame) {
        Detection d[2] = {box(500.0f, 400.0f, 60.0f, 180.0f, 0.9f, 0), box(500.0f, 330.0f, 24.0f, 28.0f, 0.9f, 1)};
        tracker.update(d, 2, frame * 16 * kMs, nullptr);
    }
    TrackView views[aimbuddy::kMaxTracks];
    const int n = tracker.snapshot(5 * 16 * kMs, views, aimbuddy::kMaxTracks);
    CHECK(n == 2);
    CHECK(n == 2 && views[0].classId != views[1].classId);
}

TEST(tracker_discounts_boxes_cut_off_by_the_scanned_region) {
    Tracker tracker;
    for (int frame = 0; frame < 8; ++frame) {
        Detection d = box(500.0f, 400.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&d, 1, frame * 16 * kMs, nullptr);
    }
    // The target walks off the right edge of the scanned region: the visible box shrinks and its centre moves
    // left, but the real centre does not. A clipped box must pull the estimate much less.
    TrackView before, after;
    CHECK(viewNear(tracker, 7 * 16 * kMs, 500.0f, 400.0f, &before));
    Detection clipped = box(520.0f, 400.0f, 40.0f, 180.0f, 0.9f);
    clipped.clipFlags = aimbuddy::kClipRight;
    // Same width change without the flag, in a second tracker, for comparison.
    Tracker unflagged;
    for (int frame = 0; frame < 8; ++frame) {
        Detection d = box(500.0f, 400.0f, 60.0f, 180.0f, 0.9f);
        unflagged.update(&d, 1, frame * 16 * kMs, nullptr);
    }
    Detection plain = box(520.0f, 400.0f, 40.0f, 180.0f, 0.9f);
    tracker.update(&clipped, 1, 8 * 16 * kMs, nullptr);
    unflagged.update(&plain, 1, 8 * 16 * kMs, nullptr);
    TrackView plainView;
    CHECK(viewNear(tracker, 8 * 16 * kMs, 520.0f, 400.0f, &after));
    CHECK(viewNear(unflagged, 8 * 16 * kMs, 520.0f, 400.0f, &plainView));
    CHECK(std::fabs(after.velocityX) <= std::fabs(plainView.velocityX));
}

TEST(tracker_survives_invalid_input_and_overload) {
    Tracker tracker;
    std::vector<Detection> many;
    for (int i = 0; i < 200; ++i) {
        many.push_back(box(30.0f + 9.0f * i, 300.0f, 20.0f, 60.0f, 0.9f));
    }
    Detection bad = box(0.0f, 0.0f, -5.0f, 10.0f);
    many.push_back(bad);
    Detection nan = box(NAN, 100.0f, 10.0f, 10.0f);
    many.push_back(nan);
    tracker.update(many.data(), static_cast<int>(many.size()), 0, nullptr);
    TrackView views[aimbuddy::kMaxTracks];
    const int n = tracker.snapshot(1 * kMs, views, aimbuddy::kMaxTracks);
    CHECK(n <= aimbuddy::kMaxTracks);
    for (int i = 0; i < n; ++i) {
        CHECK(std::isfinite(views[i].centerX) && std::isfinite(views[i].centerY));
    }
    tracker.reset();
    CHECK(drawnCount(tracker, 2 * kMs) == 0);
}

TEST(tracker_thresholds_follow_the_confidence_setting) {
    const aimbuddy::TrackerConfig low = aimbuddy::TrackerConfig::forConfidence(0.30f);
    const aimbuddy::TrackerConfig high = aimbuddy::TrackerConfig::forConfidence(0.80f);
    CHECK(low.birthScore < high.birthScore);
    CHECK(low.lowScore <= low.highScore && low.highScore <= low.birthScore);
    CHECK(high.lowScore <= high.highScore && high.highScore <= high.birthScore);
    CHECK(high.showTentativeScore >= high.birthScore);
    CHECK(aimbuddy::TrackerConfig::forConfidence(5.0f).birthScore <= 0.95f);   // out of range values are clamped
    CHECK(aimbuddy::TrackerConfig::forConfidence(-1.0f).birthScore >= 0.20f);
}

TEST(tracker_does_not_slide_onto_a_neighbour_when_its_own_box_is_missed) {
    Tracker tracker;
    // Two people stand side by side, 80 px apart, boxes 60 px wide (a 20 px gap between them).
    for (int frame = 0; frame < 12; ++frame) {
        Detection d[2] = {box(300.0f, 400.0f, 60.0f, 180.0f, 0.9f), box(380.0f, 400.0f, 60.0f, 180.0f, 0.9f)};
        tracker.update(d, 2, frame * 16 * kMs, nullptr);
    }
    TrackView left;
    CHECK(viewNear(tracker, 11 * 16 * kMs, 300.0f, 400.0f, &left));
    const int leftId = left.id;

    // The left person is now missed for several frames while only the right one is detected.
    for (int frame = 12; frame < 18; ++frame) {
        Detection right = box(380.0f, 400.0f, 60.0f, 180.0f, 0.9f);
        tracker.update(&right, 1, frame * 16 * kMs, nullptr);
    }
    TrackView views[aimbuddy::kMaxTracks];
    const int n = tracker.snapshot(17 * 16 * kMs, views, aimbuddy::kMaxTracks);
    for (int i = 0; i < n; ++i) {
        if (views[i].id == leftId) {
            CHECK_NEAR(views[i].centerX, 300.0, 12.0);   // it stayed where the person is, not between the two
        }
    }
    // And a new person appearing 70 px from the missed one must not be absorbed into its track.
    Detection fresh[2] = {box(380.0f, 400.0f, 60.0f, 180.0f, 0.9f), box(236.0f, 400.0f, 60.0f, 180.0f, 0.9f)};
    tracker.update(fresh, 2, 18 * 16 * kMs, nullptr);
    TrackView near;
    CHECK(viewNear(tracker, 18 * 16 * kMs, 236.0f, 400.0f, &near));
    CHECK(near.id != leftId);
}
