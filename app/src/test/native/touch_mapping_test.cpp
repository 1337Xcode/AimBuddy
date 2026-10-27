#include "core/screen_mapping.h"
#include "core/touch_protocol.h"
#include "test_harness.h"

using namespace aimbuddy::touch;

namespace {

// The touchscreen of the phone this was developed on: 8640 x 19200 raw units, built in portrait.
const RawRange kPanel{0, 8639, 0, 19199};

// The same phone's display: 1080 x 2400 pixels upright, so 2400 x 1080 when turned on its side.
constexpr int kLongEdge = 2400;
constexpr int kShortEdge = 1080;

}  // namespace

TEST(mapping_follows_the_panel_when_the_display_is_upright) {
    RawPoint topLeft = displayToRaw(0, 0, kShortEdge, kLongEdge, 0, kPanel);
    CHECK(topLeft.x == 0 && topLeft.y == 0);
    RawPoint bottomRight = displayToRaw(kShortEdge, kLongEdge, kShortEdge, kLongEdge, 0, kPanel);
    CHECK(bottomRight.x == kPanel.maxX && bottomRight.y == kPanel.maxY);
    RawPoint middle = displayToRaw(540, 1200, kShortEdge, kLongEdge, 0, kPanel);
    CHECK_NEAR(middle.x, 4319.5, 1.0);
    CHECK_NEAR(middle.y, 9599.5, 1.0);
}

TEST(mapping_turns_the_corners_for_a_quarter_turn_counter_clockwise) {
    // Rotation 1: the panel's top edge is on the player's left, so the display's top left corner
    // is the panel's top right.
    RawPoint topLeft = displayToRaw(0, 0, kLongEdge, kShortEdge, 1, kPanel);
    CHECK(topLeft.x == kPanel.maxX && topLeft.y == 0);
    RawPoint topRight = displayToRaw(kLongEdge, 0, kLongEdge, kShortEdge, 1, kPanel);
    CHECK(topRight.x == kPanel.maxX && topRight.y == kPanel.maxY);
    RawPoint bottomLeft = displayToRaw(0, kShortEdge, kLongEdge, kShortEdge, 1, kPanel);
    CHECK(bottomLeft.x == 0 && bottomLeft.y == 0);
}

TEST(mapping_turns_the_corners_for_a_quarter_turn_clockwise) {
    // Rotation 3: the display's top left corner is the panel's bottom left.
    RawPoint topLeft = displayToRaw(0, 0, kLongEdge, kShortEdge, 3, kPanel);
    CHECK(topLeft.x == 0 && topLeft.y == kPanel.maxY);
    RawPoint topRight = displayToRaw(kLongEdge, 0, kLongEdge, kShortEdge, 3, kPanel);
    CHECK(topRight.x == 0 && topRight.y == 0);
    RawPoint bottomLeft = displayToRaw(0, kShortEdge, kLongEdge, kShortEdge, 3, kPanel);
    CHECK(bottomLeft.x == kPanel.maxX && bottomLeft.y == kPanel.maxY);
}

TEST(mapping_flips_both_axes_when_upside_down) {
    RawPoint topLeft = displayToRaw(0, 0, kShortEdge, kLongEdge, 2, kPanel);
    CHECK(topLeft.x == kPanel.maxX && topLeft.y == kPanel.maxY);
    RawPoint bottomRight = displayToRaw(kShortEdge, kLongEdge, kShortEdge, kLongEdge, 2, kPanel);
    CHECK(bottomRight.x == 0 && bottomRight.y == 0);
}

TEST(mapping_keeps_positions_in_proportion_after_a_turn) {
    // A point a quarter of the way along the display's width stays a quarter of the way along the
    // panel's height after a quarter turn.
    RawPoint point = displayToRaw(kLongEdge * 0.25f, kShortEdge * 0.5f, kLongEdge, kShortEdge, 1, kPanel);
    CHECK_NEAR(point.y, 0.25 * kPanel.maxY, 1.0);
    CHECK_NEAR(point.x, 0.5 * kPanel.maxX, 1.0);
}

TEST(mapping_clamps_positions_outside_the_display) {
    RawPoint far = displayToRaw(-500, 99999, kShortEdge, kLongEdge, 0, kPanel);
    CHECK(far.x == 0 && far.y == kPanel.maxY);
}

TEST(mapping_respects_a_panel_that_does_not_start_at_zero) {
    const RawRange offset{100, 1099, 200, 2199};
    RawPoint topLeft = displayToRaw(0, 0, 1000, 2000, 0, offset);
    CHECK(topLeft.x == 100 && topLeft.y == 200);
    RawPoint bottomRight = displayToRaw(1000, 2000, 1000, 2000, 0, offset);
    CHECK(bottomRight.x == 1099 && bottomRight.y == 2199);
}

TEST(mapping_treats_odd_rotation_numbers_as_turns) {
    RawPoint a = displayToRaw(10, 20, kShortEdge, kLongEdge, 4, kPanel);
    RawPoint b = displayToRaw(10, 20, kShortEdge, kLongEdge, 0, kPanel);
    CHECK(a.x == b.x && a.y == b.y);
    RawPoint c = displayToRaw(10, 20, kLongEdge, kShortEdge, -1, kPanel);
    RawPoint d = displayToRaw(10, 20, kLongEdge, kShortEdge, 3, kPanel);
    CHECK(c.x == d.x && c.y == d.y);
}

TEST(protocol_packets_keep_their_size_and_floats) {
    using namespace aimbuddy::touch::protocol;
    CHECK(sizeof(RequestPacket) == 16);
    CHECK(sizeof(ReportPacket) == 16);
    const RequestPacket move = makeAimRequest(Request::kAimMove, 1234.5f, -0.25f);
    CHECK(move.type == static_cast<uint8_t>(Request::kAimMove));
    CHECK_NEAR(bitsToFloat(move.a), 1234.5, 0.0);
    CHECK_NEAR(bitsToFloat(move.b), -0.25, 0.0);
    const ReportPacket status = makeStatus(State::kMerging, Reason::kNone);
    CHECK(status.type == static_cast<uint8_t>(Report::kStatus));
    CHECK(status.state == static_cast<uint8_t>(State::kMerging));
    const ReportPacket ready = makeReady(10, 8639, 19199);
    CHECK(ready.a == 10 && ready.b == 8639 && ready.c == 19199);
}
