#include "core/drag_scroller.h"
#include "test_harness.h"

using aimbuddy::DragScroller;
using aimbuddy::ScrollDecision;

namespace {

using Kind = ScrollDecision::Kind;

constexpr float kSlop = 20.0f;

/// A scroller whose area is the page of a menu, 300 wide and 400 tall, at (100, 50).
DragScroller makePage() {
    DragScroller scroller(kSlop);
    scroller.setArea(100.0f, 50.0f, 400.0f, 450.0f);
    return scroller;
}

}  // namespace

TEST(a_finger_that_goes_down_outside_the_page_goes_to_the_controls) {
    DragScroller scroller = makePage();
    CHECK(scroller.down(50.0f, 100.0f).kind == Kind::kPass);
    CHECK(scroller.move(50.0f, 300.0f).kind == Kind::kPass);   // moving in does not start a scroll
    CHECK(scroller.move(200.0f, 300.0f).kind == Kind::kPass);
    CHECK(scroller.up(200.0f, 300.0f).kind == Kind::kPass);
}

TEST(a_finger_that_goes_down_and_stays_is_held_and_then_pressed_where_it_went_down) {
    DragScroller scroller = makePage();
    CHECK(scroller.down(200.0f, 200.0f).kind == Kind::kHold);
    CHECK(scroller.move(203.0f, 204.0f).kind == Kind::kHold);   // inside the slop
    const ScrollDecision tap = scroller.up(203.0f, 204.0f);
    CHECK(tap.kind == Kind::kPress);
    CHECK_NEAR(tap.pressX, 200.0, 1e-6);
    CHECK_NEAR(tap.pressY, 200.0, 1e-6);
}

TEST(a_finger_that_moves_up_scrolls_the_page_down_by_how_far_it_went) {
    DragScroller scroller = makePage();
    scroller.down(200.0f, 300.0f);
    const ScrollDecision first = scroller.move(202.0f, 260.0f);   // 40 px up, past the slop
    CHECK(first.kind == Kind::kScroll);
    CHECK_NEAR(first.scrollPx, 40.0, 1e-6);   // all of it since it went down, so the page does not jump
    const ScrollDecision next = scroller.move(202.0f, 240.0f);
    CHECK(next.kind == Kind::kScroll);
    CHECK_NEAR(next.scrollPx, 20.0, 1e-6);    // then only the new step
    const ScrollDecision end = scroller.up(202.0f, 235.0f);
    CHECK(end.kind == Kind::kScroll);         // the release is not a press on whatever is under the finger
    CHECK_NEAR(end.scrollPx, 5.0, 1e-6);
}

TEST(a_finger_that_moves_down_scrolls_the_page_back_up) {
    DragScroller scroller = makePage();
    scroller.down(200.0f, 200.0f);
    const ScrollDecision first = scroller.move(200.0f, 250.0f);
    CHECK(first.kind == Kind::kScroll);
    CHECK_NEAR(first.scrollPx, -50.0, 1e-6);
}

TEST(a_finger_that_moves_sideways_presses_a_control_and_keeps_it) {
    DragScroller scroller = makePage();
    scroller.down(200.0f, 200.0f);
    const ScrollDecision first = scroller.move(260.0f, 205.0f);   // a slider being dragged
    CHECK(first.kind == Kind::kPress);
    CHECK_NEAR(first.pressX, 200.0, 1e-6);
    CHECK_NEAR(first.pressY, 200.0, 1e-6);
    CHECK(scroller.move(300.0f, 260.0f).kind == Kind::kPass);   // from here on it goes straight through, even downwards
    CHECK(scroller.up(300.0f, 260.0f).kind == Kind::kPass);
}

TEST(a_finger_that_scrolls_stays_a_scroll_when_it_drifts_sideways) {
    DragScroller scroller = makePage();
    scroller.down(200.0f, 300.0f);
    scroller.move(202.0f, 250.0f);
    const ScrollDecision drift = scroller.move(320.0f, 240.0f);
    CHECK(drift.kind == Kind::kScroll);
    CHECK_NEAR(drift.scrollPx, 10.0, 1e-6);
}

TEST(a_diagonal_move_is_a_press_so_that_a_slider_wins_a_tie) {
    DragScroller scroller = makePage();
    scroller.down(200.0f, 200.0f);
    CHECK(scroller.move(230.0f, 230.0f).kind == Kind::kPress);
}

TEST(nothing_is_decided_in_an_empty_area) {
    DragScroller scroller(kSlop);   // no area set
    CHECK(scroller.down(200.0f, 200.0f).kind == Kind::kPass);
    CHECK(scroller.move(200.0f, 100.0f).kind == Kind::kPass);
}

TEST(a_cancelled_finger_is_forgotten_and_the_next_one_starts_fresh) {
    DragScroller scroller = makePage();
    scroller.down(200.0f, 300.0f);
    scroller.move(200.0f, 250.0f);   // scrolling
    scroller.cancel();
    CHECK(scroller.move(200.0f, 200.0f).kind == Kind::kPass);   // nothing is tracked
    CHECK(scroller.down(150.0f, 150.0f).kind == Kind::kHold);
    CHECK(scroller.up(150.0f, 150.0f).kind == Kind::kPress);
}

TEST(a_new_finger_replaces_one_whose_release_was_lost) {
    DragScroller scroller = makePage();
    scroller.down(200.0f, 300.0f);
    scroller.move(200.0f, 250.0f);   // scrolling, and then the up never arrives
    CHECK(scroller.down(180.0f, 180.0f).kind == Kind::kHold);
    const ScrollDecision tap = scroller.up(180.0f, 180.0f);
    CHECK(tap.kind == Kind::kPress);
    CHECK_NEAR(tap.pressX, 180.0, 1e-6);
}
