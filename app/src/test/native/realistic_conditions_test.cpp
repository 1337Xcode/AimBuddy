#include <algorithm>
#include <cmath>

#include "simulated_game.h"
#include "test_harness.h"

using namespace simulation;

namespace {

/// What a phone in a real match manages: 15 detections a second, each arriving 70 ms after its frame, with a
/// box that is a few pixels off and a target that is missed now and then. The camera is looked at 30 times a
/// second on the captured frames, whether or not the detector sees them (the pipeline does it that way).
SimulatedGame::Truth phoneInAMatch() {
    SimulatedGame::Truth truth;
    truth.detectionPeriodMs = 66;
    truth.detectionLatencyMs = 70;
    truth.noisePx = 3.0f;
    truth.dropoutRate = 0.10f;
    truth.cameraPeriodMs = 33;
    return truth;
}

/// A slow phone: 8 detections a second, each arriving 130 ms after its frame.
SimulatedGame::Truth slowPhone() {
    SimulatedGame::Truth truth = phoneInAMatch();
    truth.detectionPeriodMs = 125;
    truth.detectionLatencyMs = 130;
    return truth;
}

/// The player swings the camera from side to side with their thumb: `peakPxPerSecond` at the fastest.
std::function<void(float, float*, float*)> playerSwinging(float hertz, float peakPxPerSecond) {
    return [=](float t, float* speedX, float* speedY) {
        *speedX = peakPxPerSecond * std::sin(6.283185f * hertz * t);
        *speedY = 0.0f;
    };
}

/// The player's own weapon, as the detector sometimes sees it: a person-sized box that never moves on the
/// screen, whichever way the camera turns, and sits nearer the crosshair than a real target does.
FalseBox weaponStuckToTheScreen() {
    FalseBox box;
    box.fixedToScreen = true;
    box.x = 1270.0f;
    box.y = 630.0f;
    box.width = 160.0f;
    box.height = 220.0f;
    box.presence = 0.95f;
    box.scoreLow = 0.60f;
    box.scoreHigh = 0.95f;
    return box;
}

/// Settings with which the assist never moves, so only what the tracker draws is judged.
AimSettings assistOff() {
    AimSettings settings = baseSettings();
    settings.fovRadiusPx = 0.0f;
    return settings;
}

}  // namespace

TEST(drawn_boxes_stay_on_a_target_while_the_player_turns) {
    // The player turns the camera by hand, at up to 600 px/s. The picture a detection was made from is
    // about 90 ms old by the time its box is drawn, so a box that only knows the camera up to that picture
    // trails the target by as far as the camera turned since: 45 px all the time, before the camera was
    // looked at on every frame and carried on past the newest one.
    SimulatedGame::Truth truth = phoneInAMatch();
    truth.playerTurn = playerSwinging(0.8f, 600.0f);
    SimulatedGame game(truth, assistOff(), stationary(0.0f, 0.0f));
    const SimulatedGame::DrawnBoxes drawn = game.judgeDrawnBoxes(8.0f, 1.0f);
    CHECK(drawn.missingShare < 0.02);
    CHECK(drawn.errorRms < 14.0);
}

TEST(a_slow_phone_keeps_a_box_on_a_target_and_the_assist_keeps_following_it) {
    // At 8 detections a second the newest result is over 130 ms old and the next is 125 ms away. Counting that
    // wait as time the target went unseen dimmed the box for 41 percent of the time and made the assist let go
    // of the target and touch down again 8 times in 10 seconds, where a phone twice as quick did it once.
    SimulatedGame boxes(slowPhone(), assistOff(), stationary(0.0f, 0.0f));
    const SimulatedGame::DrawnBoxes drawn = boxes.judgeDrawnBoxes(12.0f, 1.0f);
    CHECK(drawn.missingShare < 0.02);

    SimulatedGame aimed(slowPhone(), baseSettings(), stationary(150.0f, -50.0f));
    aimed.run(10.0f, [](float, float, float) {});
    CHECK(aimed.touchCount() <= 2);
}

TEST(boxes_that_show_up_for_a_single_frame_are_not_drawn) {
    // Now and then the detector reports a person where there is none, for one frame, with a confidence
    // anywhere from 50 to 85 percent. Drawn, each such box flashes on the screen for a moment: the
    // ghost boxes of a real match.
    SimulatedGame::Truth truth = phoneInAMatch();
    truth.strayRate = 0.5f;
    SimulatedGame game(truth, assistOff(), stationary(0.0f, 0.0f));
    const SimulatedGame::DrawnBoxes drawn = game.judgeDrawnBoxes(12.0f, 1.0f);
    CHECK_NEAR(drawn.ghostShare, 0.0, 0.01);
    CHECK(drawn.missingShare < 0.03);   // and the real target is still there almost all the time
}

TEST(the_assist_is_not_led_away_by_a_box_stuck_to_the_screen) {
    // The weapon box is nearer the crosshair than the real target, so it is the one the assist picks
    // first. It can never be reached: it stays put however far the camera turns. The assist must find
    // that out and go for the real target.
    SimulatedGame::Truth truth = phoneInAMatch();
    truth.falseBoxes.push_back(weaponStuckToTheScreen());
    SimulatedGame game(truth, baseSettings(), stationary(170.0f, -40.0f));
    CHECK_NEAR(rmsError(game, 8.0f, 3.0f), 0.0, 8.0);   // from the third second on the aim sits on the real target
    CHECK_NEAR(game.cameraTravelByAssistPx(), 0.0, 900.0);   // and the camera was not thrown about on the way
    CHECK(game.controller().telemetry().giveUps >= 1);
}

TEST(the_assist_does_not_keep_turning_the_camera_for_a_box_stuck_to_the_screen) {
    // Only the weapon box is in range. Chasing it turns the camera and moves nothing, for ever.
    SimulatedGame::Truth truth = phoneInAMatch();
    truth.falseBoxes.push_back(weaponStuckToTheScreen());
    SimulatedGame game(truth, baseSettings(), stationary(2500.0f, 0.0f));   // the real target is far out of reach
    game.run(12.0f, [](float, float, float) {});
    CHECK_NEAR(game.cameraTravelByAssistPx(), 0.0, 1500.0);
}

TEST(the_assist_does_not_give_up_on_targets_that_run_or_strafe) {
    // A target the camera follows stays near the crosshair on the screen, and one that runs away as
    // fast as the camera turns after it comes no closer for a moment. Neither is a box stuck to the screen.
    SimulatedGame::Path running = [](float t, float* x, float* y) {
        *x = 1100.0f + 300.0f * t;
        *y = 540.0f;
    };
    const SimulatedGame::Path paths[] = {running, strafingPath(0.5f, 140.0f), strafingPath(0.9f, 140.0f)};
    for (const SimulatedGame::Path& path : paths) {
        SimulatedGame game(phoneInAMatch(), baseSettings(), path);
        game.run(8.0f, [](float, float, float) {});
        CHECK(game.controller().telemetry().giveUps == 0);
    }
}

TEST(the_box_of_a_still_target_does_not_wander_with_the_detectors_noise) {
    // The detector's box is a few pixels off from one frame to the next, even for a target that does not
    // move. Drawn as it comes, the box jitters; drawn through a filter that carried each frame's guess of
    // the target's speed forward, it jumped about 6 px at every screen refresh, more than the noise itself.
    SimulatedGame game(phoneInAMatch(), assistOff(), stationary(0.0f, 0.0f));
    const SimulatedGame::DrawnBoxes drawn = game.judgeDrawnBoxes(10.0f, 1.0f);
    CHECK(drawn.jitterRms < 3.2);
    CHECK(drawn.errorRms < 6.5);
    CHECK(drawn.missingShare < 0.01);
}

TEST(the_box_of_a_strafing_target_keeps_up_with_it) {
    // Smoothing a still target must not make the box trail one that moves: 0.5 Hz with a 100 px swing is 310 px/s at the fastest.
    SimulatedGame game(phoneInAMatch(), assistOff(), strafingPath(0.5f, 100.0f));
    const SimulatedGame::DrawnBoxes drawn = game.judgeDrawnBoxes(10.0f, 1.0f);
    CHECK(drawn.errorRms < 20.0);
    CHECK(drawn.missingShare < 0.01);
}

TEST(the_camera_is_calibrated_while_the_player_turns_it_by_hand) {
    // The player swings the camera, up to 700 px/s, and the assist's finger mostly reacts to that swing, so
    // slide and finger travel move together for a reason that has nothing to do with the camera's response.
    // A straight fit of one on the other finds a delay that stands out, near the right one, and a gain that
    // can be half what it should be. Which way one run goes depends on the noise in it, so twenty runs are
    // judged for each of three cameras, not one. The camera is looked at on frames that come a little early
    // or late, and one in seven cannot be read.
    int runs = 0;
    int closeRuns = 0;
    double worstGainError = 0.0;
    double worstDelayErrorS = 0.0;
    float leastConfidence = 1.0f;
    for (float gain : {1.0f, 2.5f, 5.0f}) {
        for (uint32_t seed = 1; seed <= 20; ++seed) {
            SimulatedGame::Truth truth = phoneInAMatch();
            truth.gain = gain;
            truth.delayS = 0.09f;
            truth.measureNoisePx = 1.5f;
            truth.cameraJitterMs = 12;
            truth.unreadableRate = 0.15f;
            truth.playerTurn = [](float t, float* speedX, float* speedY) {
                *speedX = 500.0f * std::sin(6.283185f * 0.7f * t) + 200.0f * std::sin(6.283185f * 1.9f * t);
                *speedY = 120.0f * std::sin(6.283185f * 0.4f * t);
            };
            SimulatedGame game(truth, baseSettings(), strafingPath(0.5f, 100.0f), seed);
            game.run(12.0f, [](float, float, float) {});
            const CameraResponse response = game.camera().response();
            const double gainError = std::max(std::fabs(response.gainX - gain), std::fabs(response.gainY - gain)) / gain;
            ++runs;
            closeRuns += gainError <= 0.15 ? 1 : 0;
            worstGainError = std::max(worstGainError, gainError);
            worstDelayErrorS = std::max(worstDelayErrorS, static_cast<double>(std::fabs(response.delayS - 0.09f)));
            leastConfidence = std::min(leastConfidence, game.camera().confidence());
        }
    }
    CHECK(closeRuns >= 0.85 * runs);           // both gains within 15% in most runs, however the noise falls
    CHECK_NEAR(worstGainError, 0.0, 0.30);     // and in none is a gain far off
    CHECK_NEAR(worstDelayErrorS, 0.0, 0.02);
    CHECK(leastConfidence > 0.5f);
}

TEST(the_assist_helps_a_person_steering_by_hand_and_never_makes_their_aim_worse) {
    // The person turns the camera towards the target with their thumb: what they see is 200 ms old, they
    // pull in proportion to how far off it looks and match most of the target's speed. On their own they
    // trail a strafing target by about 60 px and a runner by about 20. The assist and the thumb steer the
    // same camera, and each could fight the other.
    const SimulatedGame::Path running = [](float t, float* x, float* y) {
        *x = 1100.0f + 300.0f * t;
        *y = 540.0f;
    };
    SimulatedGame::Truth truth = phoneInAMatch();
    truth.human.aims = true;

    SimulatedGame aloneStill(truth, assistOff(), stationary(170.0f, -40.0f));
    SimulatedGame bothStill(truth, baseSettings(), stationary(170.0f, -40.0f));
    CHECK(rmsError(bothStill, 12.0f, 3.0f) < rmsError(aloneStill, 12.0f, 3.0f) * 1.1 + 1.0);

    SimulatedGame aloneStrafing(truth, assistOff(), strafingPath(0.5f, 100.0f));
    SimulatedGame bothStrafing(truth, baseSettings(), strafingPath(0.5f, 100.0f));
    CHECK(rmsError(bothStrafing, 12.0f, 3.0f) < rmsError(aloneStrafing, 12.0f, 3.0f) * 0.7);

    // The person is already turning the camera along with a runner, and the assist feeds it the runner's whole
    // speed too, so the two overshoot a little. The assist still helps, by about a fifth. It helped by about
    // half while the camera's gain was read three times too high (see the calibration test above), because the
    // assist then fed the camera only the third of the runner's speed that the person left over.
    SimulatedGame aloneRunning(truth, assistOff(), running);
    SimulatedGame bothRunning(truth, baseSettings(), running);
    CHECK(rmsError(bothRunning, 12.0f, 3.0f) < rmsError(aloneRunning, 12.0f, 3.0f) * 0.95);
}

TEST(a_box_stuck_to_the_screen_is_not_drawn_once_the_assist_has_found_it_out) {
    // The player's weapon taken for an enemy is a person-sized box that never moves on the screen. Once the
    // assist has given up on it the overlay must stop drawing it, so the screen does not show a gun outlined
    // as a person for the rest of the match. The weapon is the only thing in reach, so the assist goes for it.
    SimulatedGame::Truth truth = phoneInAMatch();
    truth.falseBoxes.push_back(weaponStuckToTheScreen());
    SimulatedGame game(truth, baseSettings(), stationary(2500.0f, 0.0f));
    const SimulatedGame::DrawnBoxes drawn = game.judgeDrawnBoxes(14.0f, 5.0f);
    CHECK(game.controller().telemetry().giveUps >= 1);
    CHECK(drawn.ghostShare < 0.1);   // from the fifth second on, nothing but the weapon is there to be drawn, and it is not
}

TEST(the_assist_forgets_the_boxes_it_gave_up_on_when_the_screen_turns) {
    // A box given up on is remembered by where it is on the screen. Once the screen has turned that place is
    // somewhere else, and a person standing in it would be left alone for as long as the box would have been.
    SimulatedGame::Truth truth = phoneInAMatch();
    truth.falseBoxes.push_back(weaponStuckToTheScreen());
    SimulatedGame game(truth, baseSettings(), stationary(2500.0f, 0.0f));
    game.run(14.0f, [](float, float, float) {});
    CHECK(game.controller().telemetry().giveUps >= 1);

    const int64_t nowNs = SimulatedGame::clockAt(14.0f);
    auto decoysIn = [&] {
        TrackView views[kMaxTracks];
        const int count = game.tracker().snapshot(nowNs, views, kMaxTracks, true);
        int decoys = 0;
        for (int i = 0; i < count; ++i) {
            decoys += game.controller().isDecoy(views[i], nowNs) ? 1 : 0;
        }
        return decoys;
    };
    CHECK(decoysIn() >= 1);
    game.controller().forgetScene();
    CHECK(decoysIn() == 0);
}

TEST(a_box_given_up_on_is_hidden_for_a_while_and_not_for_as_long_as_it_is_remembered) {
    // The assist remembers a box it gave up on for far longer than it leaves the box alone, so that it is not
    // led by it again. A person who then stands where the weapon was must not stay out of the picture that long.
    SimulatedGame::Truth truth = phoneInAMatch();
    truth.falseBoxes.push_back(weaponStuckToTheScreen());
    SimulatedGame game(truth, baseSettings(), stationary(2500.0f, 0.0f));
    game.run(10.0f, [](float, float, float) {});
    CHECK(game.controller().telemetry().giveUps >= 1);

    const FalseBox weaponBox = weaponStuckToTheScreen();
    TrackView weapon;   // a box in the weapon's place, about its size
    weapon.id = 999;
    weapon.centerX = weaponBox.x;
    weapon.centerY = weaponBox.y;
    weapon.width = weaponBox.width;
    weapon.height = weaponBox.height;
    CHECK(game.controller().isDecoy(weapon, SimulatedGame::clockAt(11.0f)));
    CHECK(!game.controller().isDecoy(weapon, SimulatedGame::clockAt(30.0f)));
}
