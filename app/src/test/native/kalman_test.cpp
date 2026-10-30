#include <cmath>

#include "core/kalman.h"
#include "test_harness.h"

using namespace aimbuddy;

namespace {

/// A target 180 px tall, as the tracker sees one: measured 30 times a second with 3 px of noise.
constexpr float kBoxHeight = 180.0f;
constexpr float kStepS = 0.033f;
constexpr float kNoisePx = 3.0f;
const float kMeasurementVariance = (0.03f * kBoxHeight) * (0.03f * kBoxHeight);
const float kSteadyVariance = (4.0f * kBoxHeight) * (4.0f * kBoxHeight);
const float kAgileVariance = (20.0f * kBoxHeight) * (20.0f * kBoxHeight);

/// A target that strafes at 300 px/s one way for a second and then the other way. Returns when the filter's speed has
/// passed -150 px/s after the turn (seconds after it), or -1 if it never did within half a second.
template <typename Step>
float secondsToFollowTurn(Step step) {
    testing::Random random(5);
    float position = 0.0f;
    for (int i = 0; i < 60; ++i) {
        const float t = i * kStepS;
        position += (t < 1.0f ? 300.0f : -300.0f) * kStepS;
        const float velocity = step(position + static_cast<float>(random.gaussian(kNoisePx)));
        if (t >= 1.0f && velocity < -150.0f) {
            return t - 1.0f;
        }
    }
    return -1.0f;
}

/// RMS of the speed estimate of a target that stands still, after the first second.
template <typename Step>
float stillSpeedRms(Step step) {
    testing::Random random(9);
    double squares = 0.0;
    int count = 0;
    for (int i = 0; i < 300; ++i) {
        const float velocity = step(static_cast<float>(random.gaussian(kNoisePx)));
        if (i * kStepS > 1.0f) {
            squares += static_cast<double>(velocity) * velocity;
            ++count;
        }
    }
    return static_cast<float>(std::sqrt(squares / count));
}

}  // namespace

TEST(maneuvering_filter_follows_a_strafe_that_turns_faster_than_a_steady_filter) {
    ManeuveringFilter both;
    both.reset(0.0f, kMeasurementVariance, 300.0f * 300.0f);
    const float bothSeconds = secondsToFollowTurn([&](float measured) {
        both.predict(kStepS, kSteadyVariance, kAgileVariance, 1.0f, 0.4f);
        both.update(measured, kMeasurementVariance);
        return both.velocity;
    });
    PositionVelocityFilter steady;
    steady.reset(0.0f, kMeasurementVariance, 300.0f * 300.0f);
    const float steadySeconds = secondsToFollowTurn([&](float measured) {
        steady.predict(kStepS, kSteadyVariance);
        steady.update(measured, kMeasurementVariance);
        return steady.velocity;
    });
    CHECK(bothSeconds > 0.0f);
    CHECK(bothSeconds < 0.15f);                                       // within a few frames of the turn
    CHECK(steadySeconds < 0.0f || steadySeconds > bothSeconds + 0.05f);  // a filter that only knows steady motion trails it
}

TEST(maneuvering_filter_keeps_a_still_target_about_as_quiet_as_a_steady_filter) {
    ManeuveringFilter both;
    both.reset(0.0f, kMeasurementVariance, 300.0f * 300.0f);
    const float bothRms = stillSpeedRms([&](float measured) {
        both.predict(kStepS, kSteadyVariance, kAgileVariance, 1.0f, 0.4f);
        both.update(measured, kMeasurementVariance);
        return both.velocity;
    });
    PositionVelocityFilter steady;
    steady.reset(0.0f, kMeasurementVariance, 300.0f * 300.0f);
    const float steadyRms = stillSpeedRms([&](float measured) {
        steady.predict(kStepS, kSteadyVariance);
        steady.update(measured, kMeasurementVariance);
        return steady.velocity;
    });
    CHECK(bothRms < 1.5f * steadyRms + 5.0f);
    CHECK(both.agileShare < 0.3f);   // and it knows the target is not turning
}

TEST(maneuvering_filter_is_not_flung_by_one_box_far_off) {
    // A box that belongs to something else lands 120 px from a target that stood still: the speed must not jump to
    // where it would carry the box across the screen.
    ManeuveringFilter both;
    both.reset(0.0f, kMeasurementVariance, 300.0f * 300.0f);
    testing::Random random(3);
    for (int i = 0; i < 60; ++i) {
        both.predict(kStepS, kSteadyVariance, kAgileVariance, 1.0f, 0.4f);
        both.update(static_cast<float>(random.gaussian(kNoisePx)), kMeasurementVariance);
    }
    both.predict(kStepS, kSteadyVariance, kAgileVariance, 1.0f, 0.4f);
    both.update(120.0f, kMeasurementVariance);
    CHECK(std::fabs(both.velocity) < 600.0f);
    CHECK(std::fabs(both.position) < 60.0f);
}
