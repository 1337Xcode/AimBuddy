#include "core/look_watch.h"
#include "test_harness.h"

using aimbuddy::LookWatch;
using aimbuddy::touch::PlayerFingers;

namespace {

constexpr float kWidth = 2400.0f;
constexpr float kHeight = 1080.0f;
constexpr float kRightAnchor = 1800.0f;
constexpr float kLeftAnchor = 500.0f;
constexpr int64_t kMs = 1'000'000;

/// A report of fingers down, the quickest on the left half going `leftPxS` and on the right half `rightPxS`.
PlayerFingers::Activity report(float leftPxS, float rightPxS) {
    PlayerFingers::Activity activity;
    activity.fingersDown = (leftPxS > 0.0f ? 1 : 0) + (rightPxS > 0.0f ? 1 : 0);
    activity.leftPxS = leftPxS;
    activity.rightPxS = rightPxS;
    return activity;
}

float steeringAt(LookWatch* watch, int64_t atMs, const PlayerFingers::Activity& activity, float anchorX = kRightAnchor) {
    return watch->update(atMs * kMs, activity, atMs * kMs, anchorX, kWidth, kHeight);
}

}  // namespace

TEST(a_quick_finger_on_the_look_half_is_steering) {
    LookWatch watch;
    CHECK_NEAR(steeringAt(&watch, 1000, report(0.0f, 600.0f)), 1.0, 1e-6);
}

TEST(a_finger_on_the_other_half_is_the_joystick_not_the_look) {
    LookWatch watch;
    CHECK_NEAR(steeringAt(&watch, 1000, report(900.0f, 0.0f)), 0.0, 1e-6);
    // The same report, with the look area on the left instead: now it is the look finger.
    LookWatch leftHanded;
    CHECK_NEAR(steeringAt(&leftHanded, 1000, report(900.0f, 0.0f), kLeftAnchor), 1.0, 1e-6);
}

TEST(a_finger_that_barely_moves_is_not_steering_and_the_speed_in_between_is_in_between) {
    // The ramp runs from 3% of the display's height a second (32.4 px/s here) to 20% (216 px/s).
    LookWatch resting;
    CHECK_NEAR(steeringAt(&resting, 1000, report(0.0f, 30.0f)), 0.0, 1e-6);
    LookWatch halfway;
    CHECK_NEAR(steeringAt(&halfway, 1000, report(0.0f, 124.2f)), 0.5, 1e-3);
    LookWatch quick;
    CHECK_NEAR(steeringAt(&quick, 1000, report(0.0f, 216.0f)), 1.0, 1e-3);
}

TEST(steering_fades_over_a_moment_after_the_finger_stops) {
    LookWatch watch;
    steeringAt(&watch, 1000, report(0.0f, 600.0f));
    // The finger stops: reports keep coming, now with no speed. One time constant (150 ms) leaves 1/e of it.
    float afterOneConstant = 0.0f;
    float afterAWhile = 0.0f;
    for (int64_t atMs = 1010; atMs <= 1700; atMs += 10) {
        const float steering = steeringAt(&watch, atMs, report(0.0f, 0.0f));
        if (atMs == 1150) {
            afterOneConstant = steering;
        }
        afterAWhile = steering;
    }
    CHECK_NEAR(afterOneConstant, 0.368, 0.02);
    CHECK(afterAWhile < 0.02f);
}

TEST(a_report_that_is_no_longer_renewed_is_not_believed) {
    LookWatch watch;
    // The helper reported a quick finger 400 ms ago and has said nothing since.
    const float steering = watch.update(1400 * kMs, report(0.0f, 900.0f), 1000 * kMs, kRightAnchor, kWidth, kHeight);
    CHECK_NEAR(steering, 0.0, 1e-6);
}

TEST(nothing_is_reported_before_the_first_report_or_for_a_display_of_no_size) {
    LookWatch watch;
    CHECK_NEAR(watch.update(1000 * kMs, report(0.0f, 900.0f), 0, kRightAnchor, kWidth, kHeight), 0.0, 1e-6);
    CHECK_NEAR(watch.update(1010 * kMs, report(0.0f, 900.0f), 1010 * kMs, kRightAnchor, 0.0f, 0.0f), 0.0, 1e-6);
}

TEST(resetting_forgets_that_the_player_was_steering) {
    LookWatch watch;
    steeringAt(&watch, 1000, report(0.0f, 600.0f));
    watch.reset();
    CHECK_NEAR(steeringAt(&watch, 1010, report(0.0f, 0.0f)), 0.0, 1e-6);
}
