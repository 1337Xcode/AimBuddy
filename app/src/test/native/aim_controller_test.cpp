#include <algorithm>
#include <cmath>

#include "simulated_game.h"
#include "test_harness.h"

using namespace simulation;

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
    CameraMotion motion;
    AimController controller(baseSettings(), &motion);
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
    CameraMotion motion;
    AimController controller(baseSettings(), &motion);
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

TEST(aim_closes_more_gently_while_the_player_steers) {
    // The same target 40 px from the crosshair, and the first move the finger makes. The more the player's
    // own finger is steering the camera, the less of the gap the assist closes in that step.
    const auto firstMove = [](float steering) {
        AimSettings settings = baseSettings();
        settings.zone = AimZone::kCenter;
        CameraMotion motion;
        AimController controller(settings, &motion);
        controller.setCrosshair(1200.0f, 540.0f);
        Tracker tracker;
        const ModelProfile profile = ModelProfile::defaultsFor(1);
        for (int i = 0; i < 6; ++i) {
            Detection d;
            d.x = 1240.0f;
            d.y = 540.0f;
            d.width = 60.0f;
            d.height = 180.0f;
            d.score = 0.9f;
            tracker.update(&d, 1, i * 16 * kMs, nullptr);
        }
        PlayerInput player;
        player.steering = steering;
        controller.step(100 * kMs, tracker, profile, player);   // the finger goes down at the anchor
        const AimOutput move = controller.step(108 * kMs, tracker, profile, player);
        return move.action == AimAction::kTouchMove ? std::fabs(move.x - settings.anchorX) : -1.0f;
    };
    const float alone = firstMove(0.0f);
    const float halfway = firstMove(0.5f);
    const float steering = firstMove(1.0f);
    CHECK(steering > 0.0f);   // it still helps a little
    CHECK(halfway > steering);
    CHECK(alone > halfway);
}

TEST(aim_ignores_targets_outside_the_field_of_view) {
    AimSettings settings = baseSettings();
    settings.fovRadiusPx = 150.0f;
    CameraMotion motion;
    AimController controller(settings, &motion);
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
    CameraMotion motion;
    AimController controller(baseSettings(), &motion);
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
    settings.strength = 1.0f;
    SimulatedGame::Belief believesMovesLandAtOnce{1.5f, 0.0f, false};
    SimulatedGame naive(truth, settings, stationary(300.0f, 0.0f), 1, believesMovesLandAtOnce);
    const Outcome ringing = measure(naive, 4.0f, 300.0f);

    SimulatedGame::Belief compensates{1.5f, 0.05f, false};
    SimulatedGame compensated(truth, baseSettings(), stationary(300.0f, 0.0f), 1, compensates);
    const Outcome good = measure(compensated, 4.0f, 300.0f);

    CHECK(good.finalRms < 6.0f);
    CHECK(good.maxOvershoot < 0.25f * ringing.maxOvershoot);
    CHECK(ringing.zeroCrossings >= good.zeroCrossings + 4);
    // The stability factor eventually calms even the naive loop, but it takes far longer to settle.
    CHECK(ringing.settleSeconds < 0.0f || ringing.settleSeconds > 2.0f * good.settleSeconds);
}

TEST(aim_settles_whatever_the_real_camera_gain_is) {
    // The controller starts out believing the scene slides 1.5 px per finger px. Real games differ
    // by a factor of ten either way. It measures the camera's answer from its own first moves, so
    // none of these may swing, and none may take long.
    const float truths[] = {0.4f, 0.6f, 1.5f, 2.5f, 4.0f, 6.0f, 8.0f, 12.0f};
    for (const float truth : truths) {
        SimulatedGame game({truth, 0.05f}, baseSettings(), stationary(280.0f, 120.0f));
        const Outcome result = measure(game, 4.0f, 280.0f);
        CHECK(result.settleSeconds >= 0.0f);
        CHECK(result.settleSeconds < 2.0f);
        CHECK(result.finalRms < 6.0f);
        CHECK(result.maxOvershoot < 0.5f * 280.0f);
        CHECK(result.zeroCrossings <= 3);
        CHECK_NEAR(game.camera().response().gainX, truth, 0.12 * truth + 0.05);
    }
}

TEST(aim_settles_whatever_the_real_delay_is) {
    const float delays[] = {0.015f, 0.03f, 0.08f, 0.11f, 0.15f};
    for (const float delay : delays) {
        SimulatedGame game({4.0f, delay}, baseSettings(), stationary(-260.0f, 90.0f));
        const Outcome result = measure(game, 4.0f, -260.0f);
        CHECK(result.finalRms < 6.0f);
        CHECK(result.zeroCrossings <= 3);
        CHECK(result.maxOvershoot < 40.0f);
    }
}

TEST(aim_follows_a_moving_target_whatever_the_camera_gain_is) {
    const float truths[] = {0.6f, 4.0f, 8.0f};
    for (const float truth : truths) {
        SimulatedGame game({truth, 0.05f}, baseSettings(), strafingPath(0.5f, 140.0f));
        CHECK(rmsError(game, 6.0f, 2.0f) < 40.0);
    }
}

TEST(aim_makes_only_a_small_first_move_while_the_camera_is_unknown) {
    // Nothing has been measured, so the first move may not be one a wrong guess could turn into a
    // swing: the finger travels little while its effect is still unseen.
    CameraMotion motion;
    motion.setPrior({1.5f, 1.5f, 0.05f, false}, true);
    AimController controller(baseSettings(), &motion);
    controller.setCrosshair(1200.0f, 540.0f);
    Tracker tracker;
    tracker.setEgoMotion(&motion);
    const ModelProfile profile = ModelProfile::defaultsFor(1);
    for (int i = 0; i < 6; ++i) {
        Detection d;
        d.x = 1600.0f;   // 400 px to the right of the crosshair
        d.y = 400.0f;
        d.width = 60.0f;
        d.height = 180.0f;
        d.score = 0.9f;
        tracker.update(&d, 1, (1000 + i * 16) * kMs, nullptr);
    }
    float fingerX = 0.0f;
    float travelled = 0.0f;
    for (int i = 0; i < 12; ++i) {
        const AimOutput out = controller.step((1100 + i * 8) * kMs, tracker, profile, PlayerInput());
        if (out.action == AimAction::kTouchDown) {
            fingerX = out.x;
        } else if (out.action == AimAction::kTouchMove) {
            travelled += std::fabs(out.x - fingerX);
            fingerX = out.x;
        }
    }
    CHECK(travelled > 5.0f);     // it does start moving
    CHECK(travelled < 30.0f);    // but only a little, while nothing has shown its effect
}

TEST(the_stability_factor_calms_a_loop_that_swings) {
    // The camera is measured as 1.5 and is really 4: a loop that trusted that would swing. With
    // learning off nothing corrects the figure, so the only protection is the stability factor,
    // which must take the strength down once the aim has crossed the target a few times.
    SimulatedGame::Truth truth{4.0f, 0.05f};
    SimulatedGame::Belief manual{1.5f, 0.05f, false};
    SimulatedGame game(truth, baseSettings(), stationary(280.0f, 120.0f), 1, manual);
    float lowestStability = 1.0f;
    double lateSquares = 0.0;
    int lateSamples = 0;
    float peak = 0.0f;
    game.run(6.0f, [&](float t, float ex, float ey) {
        lowestStability = std::min(lowestStability, game.controller().stability());
        peak = std::max(peak, std::hypot(ex, ey));
        if (t > 5.0f) {
            lateSquares += static_cast<double>(ex) * ex + static_cast<double>(ey) * ey;
            ++lateSamples;
        }
    });
    CHECK(lowestStability < 0.9f);   // it noticed the swinging
    CHECK(peak < 600.0f);            // and the aim was never flung far from the target
    CHECK(std::sqrt(lateSquares / lateSamples) < 12.0);   // it is calm by the end
}

TEST(aim_puts_the_finger_down_again_when_the_game_ignores_it) {
    // The game takes no notice of the first touch (it began while the screen was turning, say).
    // Moving that finger for ever would do nothing; the controller sees the scene is not sliding,
    // lifts it, and the second touch is answered. The target is then reached.
    SimulatedGame::Truth truth;
    truth.gain = 3.0f;
    truth.ignoredTouches = 1;
    AimSettings settings = baseSettings();
    settings.anchorRadiusPx = 900.0f;   // far enough that reaching the edge does not lift the finger first
    SimulatedGame game(truth, settings, stationary(260.0f, 80.0f));
    const Outcome result = measure(game, 8.0f, 260.0f);
    CHECK(result.settleSeconds >= 0.0f);
    CHECK(result.settleSeconds < 5.0f);
    CHECK(result.finalRms < 8.0f);
    CHECK(game.controller().telemetry().deadFingers >= 1);
}

TEST(aim_does_not_lift_a_finger_the_game_answers) {
    // A working finger is never mistaken for a dead one, however slowly it moves.
    SimulatedGame::Truth truth;
    truth.gain = 0.6f;
    SimulatedGame game(truth, baseSettings(), stationary(220.0f, 0.0f));
    const Outcome result = measure(game, 6.0f, 220.0f);
    CHECK(result.settleSeconds >= 0.0f);
    CHECK(game.controller().telemetry().deadFingers == 0);
}

TEST(aim_waits_longer_each_time_the_game_keeps_ignoring_the_finger) {
    // A game that never answers (the touch area is not where it looks around) is not poked every
    // moment: each failed touch is followed by a longer wait.
    SimulatedGame::Truth truth;
    truth.gain = 3.0f;
    truth.ignoredTouches = 1000;
    AimSettings settings = baseSettings();
    settings.anchorRadiusPx = 900.0f;
    SimulatedGame game(truth, settings, stationary(260.0f, 80.0f));
    game.run(20.0f, [](float, float, float) {});
    const int lifts = game.controller().telemetry().deadFingers;
    CHECK(lifts >= 5);
    CHECK(lifts <= 7);   // with a wait that did not grow it would be one every 2 s: about 10
}

TEST(aim_does_not_touch_again_and_again_when_an_ignored_finger_keeps_reaching_the_edge) {
    // The same game that never answers, with a small touch area: the finger reaches its edge in a
    // moment, is lifted to be put down at the anchor, and every new touch used to start the wait for
    // an answer over, so it was never judged dead and was put down hundreds of times.
    SimulatedGame::Truth truth;
    truth.gain = 3.0f;
    truth.ignoredTouches = 100000;
    AimSettings settings = baseSettings();
    settings.anchorRadiusPx = 60.0f;
    SimulatedGame::Belief trusted{3.0f, 0.05f, false};   // figures set by hand are trusted, so the finger moves freely
    SimulatedGame game(truth, settings, stationary(260.0f, 80.0f), 1, trusted);
    int touchesInFirstHalf = -1;
    game.run(20.0f, [&](float seconds, float, float) {
        if (seconds >= 10.0f && touchesInFirstHalf < 0) {
            touchesInFirstHalf = game.touchCount();
        }
    });
    const int touchesInSecondHalf = game.touchCount() - touchesInFirstHalf;
    CHECK(game.controller().telemetry().deadFingers >= 3);
    CHECK(touchesInSecondHalf * 3 <= touchesInFirstHalf * 2);   // the waits grow, so the touches thin out
}

TEST(aim_starts_over_without_a_wait_when_it_is_released) {
    // After a few ignored fingers the wait before the next touch is seconds long. Switching the assist off
    // and on again, or the screen turning, is a new start, and a new start does not wait for what a game
    // did before it.
    SimulatedGame::Truth truth;
    truth.gain = 3.0f;
    truth.ignoredTouches = 100000;
    AimSettings settings = baseSettings();
    settings.anchorRadiusPx = 900.0f;
    SimulatedGame game(truth, settings, stationary(260.0f, 80.0f));
    float fourthLiftAtS = -1.0f;
    float releasedAtS = -1.0f;
    int touchesAtRelease = 0;
    int touchesSoonAfter = -1;
    game.run(12.0f, [&](float seconds, float, float) {
        if (fourthLiftAtS < 0.0f && game.controller().telemetry().deadFingers == 4) {
            fourthLiftAtS = seconds;   // the wait that follows is two seconds
        }
        if (fourthLiftAtS >= 0.0f && releasedAtS < 0.0f && seconds >= fourthLiftAtS + 0.3f) {
            releasedAtS = seconds;
            touchesAtRelease = game.touchCount();
            game.controller().release();
        }
        if (releasedAtS >= 0.0f && touchesSoonAfter < 0 && seconds >= releasedAtS + 0.2f) {
            touchesSoonAfter = game.touchCount();
        }
    });
    CHECK(fourthLiftAtS >= 0.0f);
    CHECK(touchesSoonAfter > touchesAtRelease);
}

TEST(aim_hands_off_lifts_the_finger_once_it_is_on_a_still_target) {
    AimSettings handsOff = baseSettings();
    handsOff.afterLanding = AfterLanding::kHandsOff;
    SimulatedGame lifting({1.5f, 0.05f}, handsOff, stationary(300.0f, -150.0f));
    const Outcome result = measure(lifting, 3.0f, 300.0f);
    CHECK(result.settleSeconds >= 0.0f);
    CHECK(result.settleSeconds < 1.0f);
    CHECK(result.finalRms < 8.0f);               // the one correction landed on it
    CHECK(!lifting.controller().isTouching());   // and the finger is up for good

    // The default stays with the target: its finger is still down.
    SimulatedGame staying({1.5f, 0.05f}, baseSettings(), stationary(300.0f, -150.0f));
    measure(staying, 3.0f, 300.0f);
    CHECK(staying.controller().isTouching());
}

TEST(aim_hands_off_does_not_follow_a_target_that_moves_on) {
    // The target swings 140 px either way, 0.5 times a second. Following it keeps the crosshair within
    // 40 px of it (a test above); with hands off the crosshair stays where the one correction left it, and
    // the target swings round it by about 140 px over the square root of two.
    AimSettings handsOff = baseSettings();
    handsOff.afterLanding = AfterLanding::kHandsOff;
    SimulatedGame lifting({1.5f, 0.05f}, handsOff, strafingPath(0.5f, 140.0f));
    CHECK(rmsError(lifting, 5.0f, 1.5f) > 80.0);
}

TEST(aim_hands_off_makes_a_new_correction_when_the_target_has_been_let_go_of) {
    // The assist lands on a target. The player then turns the camera 700 px away, past the reach of the assist,
    // so that the lock is let go of, and 665 px back, to within 35 px of the same target. It is a new
    // meeting with it: the finger goes down again and lands on it once more.
    SimulatedGame::Truth truth;
    truth.playerTurn = [](float seconds, float* speedX, float* speedY) {
        *speedX = 0.0f;
        *speedY = 0.0f;
        if (seconds >= 2.5f && seconds < 2.85f) {
            *speedX = 2000.0f;
        } else if (seconds >= 3.2f && seconds < 3.55f) {
            *speedX = -1900.0f;
        }
    };
    AimSettings handsOff = baseSettings();
    handsOff.afterLanding = AfterLanding::kHandsOff;
    SimulatedGame game(truth, handsOff, stationary(300.0f, 0.0f));
    float errorBeforeTheTurn = 1e9f;
    int touchesBeforeTheTurn = 0;
    float errorAtTheEnd = 1e9f;
    game.run(6.0f, [&](float seconds, float errorX, float errorY) {
        if (seconds >= 2.4f && seconds < 2.401f) {
            errorBeforeTheTurn = std::hypot(errorX, errorY);
            touchesBeforeTheTurn = game.touchCount();
        }
        if (seconds >= 5.9f && seconds < 5.901f) {
            errorAtTheEnd = std::hypot(errorX, errorY);
        }
    });
    CHECK(errorBeforeTheTurn < 8.0f);
    CHECK(game.touchCount() > touchesBeforeTheTurn);   // a finger went down again for the second correction
    CHECK(errorAtTheEnd < 8.0f);
    CHECK(!game.controller().isTouching());
}

TEST(aim_does_not_run_away_when_a_saved_camera_figure_is_far_wrong) {
    // An earlier run left the belief that the scene slides 0.9 px per finger pixel, with high
    // confidence; this game really slides it 6 px. The figures are only trusted as a guess, so the
    // first moves are small, the camera is measured, and the target is never lost on the way.
    SimulatedGame::Truth truth;
    truth.gain = 6.0f;
    truth.delayS = 0.1f;
    SimulatedGame game(truth, baseSettings(), stationary(100.0f, -60.0f));
    game.camera().seed({0.9f, 0.9f, 0.05f, false}, 0.9f);
    const Outcome result = measure(game, 5.0f, 100.0f);
    CHECK(result.peakError < 220.0f);      // never past the field of view, where the assist would let go
    CHECK(result.settleSeconds >= 0.0f);
    CHECK(result.settleSeconds < 3.5f);
    CHECK(result.finalRms < 8.0f);
}

TEST(aim_does_not_run_away_when_the_camera_cannot_be_watched_and_its_gain_is_wrong) {
    // The odometry reads nothing (a bare sky, a scene too busy to read), so the controller can only
    // go by what the detector shows and by a belief of 1.5 px of slide per finger pixel. The game
    // really slides the scene 6 px, or 12. The assist then moves with less authority: the target
    // stays within reach and is settled on (6), or at least never lost (12).
    for (const float gain : {6.0f, 12.0f}) {
        SimulatedGame::Truth truth;
        truth.gain = gain;
        truth.delayS = 0.1f;
        truth.measureCamera = false;
        SimulatedGame game(truth, baseSettings(), stationary(260.0f, 80.0f));
        const Outcome result = measure(game, 8.0f, 260.0f);
        CHECK(result.peakError < 300.0f);
        if (gain < 8.0f) {
            CHECK(result.settleSeconds >= 0.0f);
            CHECK(result.settleSeconds < 6.0f);
        }
        CHECK(result.finalRms < 120.0f);
    }
}
