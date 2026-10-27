#include "core/aim_point.h"
#include "test_harness.h"

using namespace aimbuddy;

namespace {

TrackView body(float cx, float cy, float w, float h) {
    TrackView view;
    view.centerX = cx;
    view.centerY = cy;
    view.width = w;
    view.height = h;
    return view;
}

}  // namespace

TEST(aim_point_on_a_standing_figure_is_near_the_top) {
    const TrackView standing = body(500, 400, 60, 200);   // spans y 300..500, tall and thin
    const AimPoint head = chooseAimPoint(standing, nullptr, AimZone::kHead);
    CHECK_NEAR(head.x, 500.0, 1e-3);
    CHECK_NEAR(head.y, 300.0 + 0.10 * 200.0, 1e-3);
    const AimPoint chest = chooseAimPoint(standing, nullptr, AimZone::kChest);
    CHECK_NEAR(chest.y, 300.0 + 0.27 * 200.0, 1e-3);
    const AimPoint centre = chooseAimPoint(standing, nullptr, AimZone::kCenter);
    CHECK_NEAR(centre.y, 400.0, 1e-3);
}

TEST(aim_point_moves_towards_the_centre_as_less_of_the_target_shows) {
    // From a full figure down to a wide, low box the fraction must only grow, in small steps,
    // so the aim never jumps when a target slowly ducks behind cover.
    float previous = aimFractionFromTop(3.0f, AimZone::kHead);
    for (float ratio = 3.0f; ratio >= 0.4f; ratio -= 0.02f) {
        const float fraction = aimFractionFromTop(ratio, AimZone::kHead);
        CHECK(fraction >= previous - 1e-6f);
        CHECK(fraction - previous < 0.02f);
        previous = fraction;
    }
    CHECK_NEAR(aimFractionFromTop(0.5f, AimZone::kHead), 0.5, 1e-6);   // lying down or a vehicle window
    CHECK_NEAR(aimFractionFromTop(1.0f, AimZone::kHead), 0.30, 1e-6);  // head and shoulders
}

TEST(aim_point_prefers_a_detected_head) {
    const TrackView target = body(500, 400, 60, 200);
    const TrackView head = body(503, 312, 22, 24);
    const AimPoint aim = chooseAimPoint(target, &head, AimZone::kHead);
    CHECK_NEAR(aim.x, 503.0, 1e-3);
    CHECK_NEAR(aim.y, 312.0, 1e-3);
    // For the chest the head still anchors the point, which sits below it.
    const AimPoint chest = chooseAimPoint(target, &head, AimZone::kChest);
    CHECK(chest.y > head.centerY);
    CHECK_NEAR(chest.x, 503.0, 1e-3);
}

TEST(aim_point_ignores_a_head_that_belongs_to_someone_else) {
    const TrackView target = body(500, 400, 60, 200);
    const TrackView farAway = body(800, 312, 22, 24);
    const AimPoint aim = chooseAimPoint(target, &farAway, AimZone::kHead);
    CHECK_NEAR(aim.x, 500.0, 1e-3);
    CHECK_NEAR(aim.y, 300.0 + 0.10 * 200.0, 1e-3);
    const TrackView tooLow = body(500, 460, 22, 24);   // inside the column but down at the legs
    CHECK_NEAR(chooseAimPoint(target, &tooLow, AimZone::kHead).y, 320.0, 1e-3);
}

TEST(aim_point_handles_degenerate_boxes) {
    const TrackView sliver = body(100, 100, 0.0f, 50.0f);   // zero width must not divide by zero
    const AimPoint aim = chooseAimPoint(sliver, nullptr, AimZone::kHead);
    CHECK(aim.y >= 75.0f && aim.y <= 125.0f);
}
