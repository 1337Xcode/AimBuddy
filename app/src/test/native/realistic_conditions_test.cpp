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
    // A straight fit of one on the other found the wrong gain and the wrong delay, with a confidence of
    // about a third, and the assist stayed as timid as it is for a camera it knows nothing about. The camera
    // is looked at on frames that come a little early or late, and one in seven cannot be read.
    for (float gain : {1.0f, 2.5f, 5.0f}) {
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
        SimulatedGame game(truth, baseSettings(), strafingPath(0.5f, 100.0f));
        game.run(12.0f, [](float, float, float) {});
        const CameraResponse response = game.camera().response();
        CHECK_NEAR(response.gainX, gain, 0.15 * gain);
        CHECK_NEAR(response.gainY, gain, 0.15 * gain);
        CHECK_NEAR(response.delayS, 0.09, 0.02);
        CHECK(game.camera().confidence() > 0.6f);
    }
}
