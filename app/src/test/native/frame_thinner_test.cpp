#include "core/frame_thinner.h"
#include "test_harness.h"

using aimbuddy::FrameThinner;

namespace {

constexpr int64_t kSecondNs = 1'000'000'000;
constexpr int64_t kStartNs = 50 * kSecondNs;   // a steady clock does not start at zero

/// How many of the frames of a screen running at `hz` for `seconds` the thinner accepts, with a little jitter on every frame.
int acceptedFrames(FrameThinner& thinner, int hz, int seconds, int64_t startNs = kStartNs, uint32_t seed = 7) {
    testing::Random random(seed);
    const int64_t interval = kSecondNs / hz;
    int accepted = 0;
    for (int frame = 0; frame < hz * seconds; ++frame) {
        const int64_t jitter = static_cast<int64_t>(random.gaussian(1.5e6));   // about 1.5 ms
        if (thinner.accept(startNs + frame * interval + jitter)) {
            ++accepted;
        }
    }
    return accepted;
}

}  // namespace

TEST(thinner_lets_every_frame_through_when_there_is_no_limit) {
    FrameThinner thinner;
    CHECK(acceptedFrames(thinner, 60, 5) == 300);
    thinner.setMaxRate(0.0f);
    CHECK(acceptedFrames(thinner, 120, 5) == 600);
}

TEST(thinner_holds_a_60_hz_screen_to_the_limit) {
    FrameThinner thinner;
    thinner.setMaxRate(30.0f);
    const int accepted = acceptedFrames(thinner, 60, 10);
    CHECK(accepted >= 298 && accepted <= 302);
}

TEST(thinner_holds_a_120_hz_screen_to_the_limit) {
    FrameThinner thinner;
    thinner.setMaxRate(30.0f);
    const int accepted = acceptedFrames(thinner, 120, 10);
    CHECK(accepted >= 296 && accepted <= 304);
}

TEST(thinner_holds_a_slow_limit_too) {
    FrameThinner thinner;
    thinner.setMaxRate(15.0f);
    const int accepted = acceptedFrames(thinner, 60, 10);
    CHECK(accepted >= 148 && accepted <= 152);
}

TEST(thinner_leaves_a_screen_slower_than_the_limit_alone) {
    FrameThinner thinner;
    thinner.setMaxRate(30.0f);
    CHECK(acceptedFrames(thinner, 24, 5) == 120);
}

TEST(thinner_does_not_make_up_for_a_pause_with_a_burst) {
    FrameThinner thinner;
    thinner.setMaxRate(15.0f);
    acceptedFrames(thinner, 60, 2);
    // Two seconds with no frames at all, then the screen moves again.
    const int afterPause = acceptedFrames(thinner, 60, 1, kStartNs + 4 * kSecondNs);
    CHECK(afterPause >= 14 && afterPause <= 16);
    CHECK(thinner.accept(kStartNs + 20 * kSecondNs));   // and the first frame after a long wait is always taken
}

TEST(thinner_starts_over_when_the_clock_goes_back) {
    FrameThinner thinner;
    thinner.setMaxRate(30.0f);
    CHECK(thinner.accept(kStartNs + 100 * kSecondNs));
    CHECK(thinner.accept(kStartNs));   // far before the last one: the schedule is dropped
    CHECK(!thinner.accept(kStartNs + 5'000'000));   // and a frame soon after is skipped as usual
}

TEST(thinner_follows_a_change_of_limit) {
    FrameThinner thinner;
    thinner.setMaxRate(15.0f);
    const int slow = acceptedFrames(thinner, 60, 4);
    thinner.setMaxRate(0.0f);
    const int open = acceptedFrames(thinner, 60, 4, kStartNs + 10 * kSecondNs);
    CHECK(slow >= 58 && slow <= 62);
    CHECK(open == 240);
}
