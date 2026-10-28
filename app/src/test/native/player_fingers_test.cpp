#include <vector>

#include "core/player_fingers.h"
#include "test_harness.h"

using aimbuddy::touch::PlayerFingers;

namespace {

constexpr float kDisplayWidth = 2400.0f;
constexpr int64_t kMs = 1'000'000;

/// A finger that starts at (x, y) and moves by (vx, vy) pixels a second.
struct Mover {
    int32_t id;
    float x;
    float y;
    float vx;
    float vy;
};

/// Feeds the fingers to a tracker every `periodMs` for `milliseconds`, starting at `startMs`; returns the time reached.
int64_t drive(PlayerFingers* fingers, const std::vector<Mover>& movers, int64_t startMs, int milliseconds, int periodMs) {
    int64_t atMs = startMs;
    for (int elapsed = 0; elapsed <= milliseconds; elapsed += periodMs) {
        atMs = startMs + elapsed;
        const float seconds = static_cast<float>(elapsed) * 0.001f;
        std::vector<PlayerFingers::Finger> now;
        for (const Mover& mover : movers) {
            now.push_back({mover.id, mover.x + mover.vx * seconds, mover.y + mover.vy * seconds});
        }
        fingers->update(atMs * kMs, now.data(), static_cast<int>(now.size()), kDisplayWidth);
    }
    return atMs;
}

}  // namespace

TEST(a_finger_moving_steadily_is_measured_at_its_speed) {
    PlayerFingers fingers;
    drive(&fingers, {{1, 1500.0f, 600.0f, 600.0f, 0.0f}}, 1000, 400, 20);
    CHECK(fingers.activity().fingersDown == 1);
    CHECK_NEAR(fingers.activity().rightPxS, 600.0, 30.0);
    CHECK_NEAR(fingers.activity().leftPxS, 0.0, 1e-6);
}

TEST(a_finger_that_stays_still_has_no_speed) {
    PlayerFingers fingers;
    drive(&fingers, {{1, 1900.0f, 500.0f, 0.0f, 0.0f}}, 1000, 1000, 20);
    CHECK(fingers.activity().fingersDown == 1);
    CHECK_NEAR(fingers.activity().rightPxS, 0.0, 1e-6);
}

TEST(a_finger_that_stops_slows_down_within_a_moment) {
    PlayerFingers fingers;
    const int64_t movedUntilMs = drive(&fingers, {{1, 1500.0f, 600.0f, 800.0f, 0.0f}}, 1000, 500, 20);
    CHECK_NEAR(fingers.activity().rightPxS, 800.0, 40.0);
    // It stops where it got to, and the tracker is only told where it is.
    drive(&fingers, {{1, 1500.0f + 800.0f * 0.5f, 600.0f, 0.0f, 0.0f}}, movedUntilMs + 20, 200, 20);
    CHECK(fingers.activity().rightPxS < 40.0f);
}

TEST(the_two_halves_of_the_display_are_told_apart) {
    PlayerFingers fingers;
    // A thumb on the joystick on the left moves fast; the one on the right moves slowly.
    drive(&fingers, {{1, 300.0f, 700.0f, 0.0f, 600.0f}, {2, 1800.0f, 500.0f, 100.0f, 0.0f}}, 1000, 400, 20);
    CHECK(fingers.activity().fingersDown == 2);
    CHECK_NEAR(fingers.activity().leftPxS, 600.0, 30.0);
    CHECK_NEAR(fingers.activity().rightPxS, 100.0, 10.0);
}

TEST(a_lifted_finger_is_forgotten_and_a_new_one_starts_from_rest) {
    PlayerFingers fingers;
    const int64_t atMs = drive(&fingers, {{1, 1500.0f, 600.0f, 900.0f, 0.0f}}, 1000, 300, 20);
    CHECK(fingers.activity().rightPxS > 500.0f);

    fingers.update((atMs + 20) * kMs, nullptr, 0, kDisplayWidth);
    CHECK(fingers.activity().fingersDown == 0);
    CHECK_NEAR(fingers.activity().rightPxS, 0.0, 1e-6);

    // A new finger far away from where the old one was is not mistaken for the old one moving there.
    const PlayerFingers::Finger fresh{2, 300.0f, 200.0f};
    fingers.update((atMs + 40) * kMs, &fresh, 1, kDisplayWidth);
    CHECK(fingers.activity().fingersDown == 1);
    CHECK_NEAR(fingers.activity().leftPxS, 0.0, 1e-6);
    CHECK_NEAR(fingers.activity().rightPxS, 0.0, 1e-6);
}

TEST(the_same_instant_told_twice_changes_nothing) {
    PlayerFingers fingers;
    const int64_t atMs = drive(&fingers, {{1, 1500.0f, 600.0f, 600.0f, 0.0f}}, 1000, 300, 20);
    const float before = fingers.activity().rightPxS;
    const PlayerFingers::Finger moved{1, 1500.0f + 600.0f * 0.3f + 40.0f, 600.0f};
    fingers.update(atMs * kMs, &moved, 1, kDisplayWidth);   // the time of the last update, with the finger somewhere else
    CHECK_NEAR(fingers.activity().rightPxS, before, 1e-3);
}

TEST(a_finger_reported_every_millisecond_is_measured_at_its_speed_too) {
    PlayerFingers fingers;
    drive(&fingers, {{1, 1500.0f, 600.0f, 600.0f, 0.0f}}, 1000, 400, 1);
    CHECK_NEAR(fingers.activity().rightPxS, 600.0, 30.0);
}

TEST(resetting_forgets_every_finger) {
    PlayerFingers fingers;
    drive(&fingers, {{1, 1500.0f, 600.0f, 600.0f, 0.0f}}, 1000, 300, 20);
    fingers.reset();
    CHECK(fingers.activity().fingersDown == 0);
    CHECK_NEAR(fingers.activity().rightPxS, 0.0, 1e-6);
}
