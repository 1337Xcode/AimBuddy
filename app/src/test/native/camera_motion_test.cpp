#include <cmath>

#include "core/camera_motion.h"
#include "core/finger_log.h"
#include "core/measured_motion.h"
#include "core/response_estimator.h"
#include "test_harness.h"

using namespace aimbuddy;

namespace {

constexpr int64_t kMs = 1'000'000;

/**
 * Feeds the estimator a scene that slides `gain` times the finger's move, `delayS` after it, as seen
 * by 60 Hz frames. The finger sweeps back and forth, like a controller closing on a target.
 */
void feedEstimator(ResponseEstimator& estimator, FingerLog& log, float gain, float delayS, bool invertY, int seconds,
                   testing::Random& random, float noisePx) {
    const int64_t frameNs = 16 * kMs;
    for (int frame = 1; frame <= seconds * 60; ++frame) {
        const int64_t toNs = frame * frameNs;
        for (int move = 0; move < 2; ++move) {
            const float finger = 12.0f * std::sin(0.078f * static_cast<float>(frame) + static_cast<float>(move));
            log.record(toNs - frameNs + move * 8 * kMs, 8 * kMs, finger, 0.6f * finger);
        }
        float fingerX, fingerY;
        log.landedBetween(delayS, toNs - frameNs, toNs, &fingerX, &fingerY);
        SceneShift shift;
        shift.fromNs = toNs - frameNs;
        shift.toNs = toNs;
        shift.dx = -gain * fingerX + static_cast<float>(random.gaussian(noisePx));
        shift.dy = (invertY ? gain : -gain) * fingerY + static_cast<float>(random.gaussian(noisePx));
        estimator.addSample(log, shift);
    }
}

}  // namespace

TEST(finger_log_counts_the_share_of_a_move_that_lands_in_the_window) {
    FingerLog log;
    log.record(100 * kMs, 100 * kMs, 10.0f, -20.0f);   // spread over 100 ms, landing 50 ms after it was sent
    log.record(300 * kMs, 0, 5.0f, 5.0f);              // an instant move
    float dx, dy;
    log.landedBetween(0.05f, 0, 1000 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 15.0, 1e-4);
    CHECK_NEAR(dy, -15.0, 1e-4);
    log.landedBetween(0.05f, 200 * kMs, 250 * kMs, &dx, &dy);   // the first move lands from 150 to 250 ms: half is in
    CHECK_NEAR(dx, 5.0, 1e-4);
    CHECK_NEAR(dy, -10.0, 1e-4);
    log.landedBetween(0.0f, 299 * kMs, 300 * kMs, &dx, &dy);    // the instant move is in (from, to]
    CHECK_NEAR(dx, 5.0, 1e-4);
    log.landedBetween(0.0f, 300 * kMs, 400 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 0.0, 1e-4);
    log.landedBetween(0.0f, 500 * kMs, 600 * kMs, &dx, &dy);    // nothing there
    CHECK_NEAR(dx, 0.0, 1e-4);
    log.clear();
    CHECK(log.empty());
}

TEST(finger_log_keeps_only_a_bounded_history) {
    FingerLog log;
    for (int i = 0; i < 1000; ++i) {
        log.record(i * kMs, kMs, 1.0f, 0.0f);
    }
    float dx, dy;
    log.landedBetween(0.0f, 0, 2000 * kMs, &dx, &dy);
    CHECK(dx <= 256.5f && dx >= 255.5f);   // only the newest 256 entries remain
}

TEST(measured_motion_adds_up_the_slides_inside_a_window) {
    MeasuredMotion motion;
    motion.add(0, 100 * kMs, 10.0f, 0.0f);
    motion.add(100 * kMs, 200 * kMs, -4.0f, 8.0f);
    float dx, dy;
    motion.shiftBetween(50 * kMs, 150 * kMs, &dx, &dy);   // half of each
    CHECK_NEAR(dx, 3.0, 1e-4);
    CHECK_NEAR(dy, 4.0, 1e-4);
    motion.shiftBetween(0, 1000 * kMs, &dx, &dy);         // time past the newest entry adds nothing
    CHECK_NEAR(dx, 6.0, 1e-4);
    CHECK(motion.latestNs() == 200 * kMs);
}

TEST(camera_motion_answers_from_measurements_then_from_the_response) {
    CameraMotion motion;
    motion.setPrior({2.0f, 2.0f, 0.05f, false}, false);
    // A move of 10 px sent at 100 ms lands at 150 ms and slides the scene 20 px to the left.
    motion.recordFinger(100 * kMs, 0, 10.0f, 0.0f);
    float dx, dy;
    motion.shiftBetween(0, 400 * kMs, &dx, &dy);
    CHECK_NEAR(dx, -20.0, 1e-3);

    // Frames were captured up to 200 ms and saw a slide of -17 px: that replaces the prediction
    // for the time they cover, and the prediction still answers for time after them.
    motion.addMeasurement(0, 200 * kMs, -17.0f, 0.0f, 1.0f);
    motion.recordFinger(250 * kMs, 0, 5.0f, 0.0f);   // lands at 300 ms, after the newest frame
    motion.shiftBetween(0, 400 * kMs, &dx, &dy);
    CHECK_NEAR(dx, -17.0 - 10.0, 1e-3);
    motion.shiftBetween(0, 200 * kMs, &dx, &dy);
    CHECK_NEAR(dx, -17.0, 1e-3);
}

TEST(camera_motion_counts_a_slide_once_when_a_frame_time_steps_back) {
    // After a stall one frame is stamped with the time it arrived and the next with its own, earlier time, so a slide
    // can start before the one before it ends. Over the time the two share, only the first one counts.
    CameraMotion motion;
    motion.addMeasurement(0, 100 * kMs, 10.0f, 0.0f, 1.0f);
    motion.addMeasurement(60 * kMs, 160 * kMs, 6.0f, 0.0f, 1.0f);
    float dx, dy;
    motion.shiftBetween(60 * kMs, 100 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 4.0, 1e-3);
    motion.shiftBetween(0, 160 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 16.0, 1e-3);
}

TEST(camera_motion_fills_a_poor_measurement_from_the_response) {
    CameraMotion motion;
    motion.setPrior({2.0f, 2.0f, 0.0f, false}, false);
    motion.recordFinger(50 * kMs, 0, 10.0f, 0.0f);
    motion.addMeasurement(0, 100 * kMs, 99.0f, 99.0f, 0.1f);   // too poor to believe
    float dx, dy;
    motion.shiftBetween(0, 100 * kMs, &dx, &dy);
    CHECK_NEAR(dx, -20.0, 1e-3);
    CHECK_NEAR(dy, 0.0, 1e-3);
}

TEST(camera_motion_reports_the_finger_travel_no_frame_shows_yet) {
    CameraMotion motion;
    motion.setPrior({1.0f, 1.0f, 0.05f, false}, false);
    motion.recordFinger(100 * kMs, 0, 6.0f, 8.0f);
    float dx, dy;
    motion.unseenFinger(&dx, &dy);
    CHECK_NEAR(dx, 6.0, 1e-4);
    CHECK_NEAR(dy, 8.0, 1e-4);
    motion.addMeasurement(0, 200 * kMs, 0.0f, 0.0f, 1.0f);   // a frame after the move has landed
    motion.unseenFinger(&dx, &dy);
    CHECK_NEAR(dx, 0.0, 1e-4);
    CHECK_NEAR(dy, 0.0, 1e-4);
}

TEST(response_estimator_finds_the_gain_and_the_delay) {
    const float gains[] = {0.5f, 1.5f, 4.0f, 9.0f};
    const float delays[] = {0.02f, 0.05f, 0.09f};
    for (const float gain : gains) {
        for (const float delay : delays) {
            ResponseEstimator estimator;
            FingerLog log;
            testing::Random random(7);
            feedEstimator(estimator, log, gain, delay, false, 4, random, 0.7f);
            const ResponseEstimator::Estimate estimate = estimator.estimate();
            CHECK(estimate.valid);
            CHECK(estimate.hasGainX && estimate.hasGainY);
            CHECK_NEAR(estimate.gainX, gain, 0.06 * gain + 0.05);
            CHECK_NEAR(estimate.gainY, gain, 0.06 * gain + 0.05);
            CHECK_NEAR(estimate.delayS, delay, 0.012);
            CHECK(estimate.confidence > 0.6f);
        }
    }
}

TEST(response_estimator_reads_a_flipped_vertical_axis) {
    ResponseEstimator estimator;
    estimator.setInvertY(true);
    FingerLog log;
    testing::Random random(3);
    feedEstimator(estimator, log, 3.0f, 0.05f, true, 3, random, 0.5f);
    const ResponseEstimator::Estimate estimate = estimator.estimate();
    CHECK(estimate.valid);
    CHECK_NEAR(estimate.gainY, 3.0, 0.3);
}

TEST(response_estimator_learns_nothing_from_a_quiet_finger) {
    ResponseEstimator estimator;
    FingerLog log;   // the finger never moved
    for (int frame = 1; frame <= 200; ++frame) {
        SceneShift shift;
        shift.fromNs = (frame - 1) * 16 * kMs;
        shift.toNs = frame * 16 * kMs;
        shift.dx = 30.0f;   // the player is looking around by themselves
        estimator.addSample(log, shift);
    }
    CHECK(!estimator.estimate().valid);
    CHECK(estimator.sampleCount() == 0);
}

TEST(response_estimator_is_not_fooled_by_slides_the_finger_did_not_cause) {
    // The finger moves as before, but the scene slides by something else entirely.
    ResponseEstimator estimator;
    FingerLog log;
    testing::Random random(5);
    const int64_t frameNs = 16 * kMs;
    for (int frame = 1; frame <= 240; ++frame) {
        const int64_t toNs = frame * frameNs;
        log.record(toNs - frameNs, 8 * kMs, 10.0f * std::sin(0.2f * static_cast<float>(frame)), 0.0f);
        SceneShift shift;
        shift.fromNs = toNs - frameNs;
        shift.toNs = toNs;
        shift.dx = static_cast<float>(random.gaussian(15.0));   // unrelated slides
        estimator.addSample(log, shift);
    }
    const ResponseEstimator::Estimate estimate = estimator.estimate();
    CHECK(!estimate.valid || estimate.confidence < 0.3f);
}

TEST(camera_motion_adopts_the_measured_response_and_keeps_it) {
    CameraMotion motion;
    motion.setPrior({1.5f, 1.5f, 0.05f, false}, true);
    CHECK(!motion.hasLearned());
    CHECK_NEAR(motion.response().gainX, 1.5, 1e-6);

    testing::Random random(11);
    FingerLog truth;   // what really lands, for the true delay
    const int64_t frameNs = 16 * kMs;
    const float trueGain = 6.0f;
    const float trueDelay = 0.06f;
    for (int frame = 1; frame <= 180; ++frame) {
        const int64_t toNs = frame * frameNs;
        const float finger = 10.0f * std::sin(0.1f * static_cast<float>(frame));
        motion.recordFinger(toNs - frameNs, 8 * kMs, finger, finger * 0.5f);
        truth.record(toNs - frameNs, 8 * kMs, finger, finger * 0.5f);
        float fingerX, fingerY;
        truth.landedBetween(trueDelay, toNs - frameNs, toNs, &fingerX, &fingerY);
        motion.addMeasurement(toNs - frameNs, toNs, -trueGain * fingerX + static_cast<float>(random.gaussian(0.5)),
                              -trueGain * fingerY + static_cast<float>(random.gaussian(0.5)), 1.0f);
    }
    CHECK(motion.hasLearned());
    CHECK_NEAR(motion.response().gainX, trueGain, 0.8);
    CHECK_NEAR(motion.response().gainY, trueGain, 0.8);
    CHECK_NEAR(motion.response().delayS, trueDelay, 0.015);
    CHECK(motion.confidence() > 0.6f);

    // The player's own figures no longer override what was measured.
    motion.setPrior({1.5f, 1.5f, 0.05f, false}, true);
    CHECK_NEAR(motion.response().gainX, trueGain, 0.8);
    // Switching the learning off goes back to them.
    motion.setPrior({1.5f, 1.5f, 0.05f, false}, false);
    CHECK_NEAR(motion.response().gainX, 1.5, 1e-6);
}

TEST(camera_motion_starts_from_an_earlier_run_without_trusting_it) {
    CameraMotion motion;
    motion.setPrior({1.5f, 1.5f, 0.05f, false}, true);
    motion.seed({5.0f, 4.0f, 0.07f, false}, 0.8f);
    CHECK(motion.hasLearned());
    CHECK_NEAR(motion.response().gainX, 5.0, 1e-6);
    CHECK_NEAR(motion.response().delayS, 0.07, 1e-6);
    // The figures are only a starting point: however sure the earlier run was, this one has not
    // seen the camera yet, so they are trusted no more than a guess.
    CHECK(motion.confidence() <= 0.36f);
    CHECK(motion.confidence() > 0.3f);
}

TEST(camera_motion_carries_the_players_turn_on_past_the_newest_frame) {
    // The player turns the camera at 600 px/s and every frame sees it: the scene slides left 600 px a
    // second. A box found in a frame is drawn a tenth of a second after it, so the slide between the
    // newest frame and that moment must be answered with the turn carried on, not with nothing.
    CameraMotion motion;
    motion.setPrior({2.0f, 2.0f, 0.05f, false}, false);   // the assist never moves here
    for (int frame = 1; frame <= 30; ++frame) {
        motion.addMeasurement((frame - 1) * 33 * kMs, frame * 33 * kMs, -19.8f, 0.0f, 1.0f);
    }
    const int64_t newestNs = 30 * 33 * kMs;
    float dx, dy;
    motion.shiftBetween(newestNs - 330 * kMs, newestNs, &dx, &dy);   // time that was seen is answered as measured
    CHECK_NEAR(dx, -198.0, 0.5);
    motion.shiftBetween(newestNs, newestNs + 100 * kMs, &dx, &dy);
    CHECK(dx < -30.0f);    // most of the 60 px the turn goes on to cover
    CHECK(dx > -60.0f);    // but not more than it would: the turn is only believed to carry on, fading out
    CHECK_NEAR(dy, 0.0, 1e-4);
    // Further out it adds less and less: a turn does not go on for ever.
    float later;
    motion.shiftBetween(newestNs + 100 * kMs, newestNs + 1000 * kMs, &later, &dy);
    CHECK(later > -0.0f - 100.0f);
    CHECK(later < -10.0f);
}

TEST(camera_motion_does_not_carry_on_a_turn_it_did_not_see) {
    // Frames whose pictures could not be compared give no rate to go by, and a camera that was still stays still.
    CameraMotion blind;
    blind.setPrior({2.0f, 2.0f, 0.05f, false}, false);
    for (int frame = 1; frame <= 30; ++frame) {
        blind.addMeasurement((frame - 1) * 33 * kMs, frame * 33 * kMs, -19.8f, 0.0f, 0.1f);   // too poor to use
    }
    float dx, dy;
    blind.shiftBetween(30 * 33 * kMs, 30 * 33 * kMs + 100 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 0.0, 1e-4);

    CameraMotion still;
    still.setPrior({2.0f, 2.0f, 0.05f, false}, false);
    for (int frame = 1; frame <= 30; ++frame) {
        still.addMeasurement((frame - 1) * 33 * kMs, frame * 33 * kMs, 0.0f, 0.0f, 1.0f);
    }
    still.shiftBetween(30 * 33 * kMs, 30 * 33 * kMs + 100 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 0.0, 1e-4);
    CHECK_NEAR(dy, 0.0, 1e-4);
}

TEST(camera_motion_does_not_count_the_assists_own_slide_as_the_players_turn) {
    // The assist's finger moves 10 px a frame at a gain of 2, so the scene slides 20 px a frame, and
    // the frames see exactly that. The slide is already answered from the finger's moves; reading it
    // as the player's own turn as well would count it twice.
    CameraMotion motion;
    motion.setPrior({2.0f, 2.0f, 0.0f, false}, false);
    for (int frame = 1; frame <= 30; ++frame) {
        const int64_t fromNs = (frame - 1) * 33 * kMs;
        const int64_t toNs = frame * 33 * kMs;
        motion.recordFinger(fromNs, 33 * kMs, 10.0f, 0.0f);   // the finger moved through the whole of this frame
        motion.addMeasurement(fromNs, toNs, -20.0f, 0.0f, 1.0f);
    }
    float dx, dy;
    motion.shiftBetween(30 * 33 * kMs, 30 * 33 * kMs + 100 * kMs, &dx, &dy);
    CHECK_NEAR(dx, 0.0, 0.5);   // nothing more is coming: the finger stopped
}
