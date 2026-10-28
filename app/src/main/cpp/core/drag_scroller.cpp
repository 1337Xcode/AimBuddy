#include "core/drag_scroller.h"

#include <cmath>

namespace aimbuddy {

namespace {

ScrollDecision pass() { return ScrollDecision{}; }

ScrollDecision hold() {
    ScrollDecision decision;
    decision.kind = ScrollDecision::Kind::kHold;
    return decision;
}

ScrollDecision press(float x, float y) {
    ScrollDecision decision;
    decision.kind = ScrollDecision::Kind::kPress;
    decision.pressX = x;
    decision.pressY = y;
    return decision;
}

ScrollDecision scroll(float px) {
    ScrollDecision decision;
    decision.kind = ScrollDecision::Kind::kScroll;
    decision.scrollPx = px;
    return decision;
}

}  // namespace

ScrollDecision DragScroller::down(float x, float y) {
    // A new finger ends whatever the last one left unfinished.
    if (!inArea(x, y)) {
        state_ = State::kPassing;
        return pass();
    }
    state_ = State::kUndecided;
    startX_ = x;
    startY_ = y;
    lastY_ = y;
    return hold();
}

ScrollDecision DragScroller::move(float x, float y) {
    switch (state_) {
        case State::kUndecided: {
            const float dx = x - startX_;
            const float dy = y - startY_;
            if (std::hypot(dx, dy) < slopPx_) {
                return hold();
            }
            // Up or down more than sideways is a scroll; anything else is a press on a control, a slider being dragged.
            if (std::fabs(dy) > std::fabs(dx)) {
                state_ = State::kScrolling;
                lastY_ = y;
                return scroll(startY_ - y);
            }
            state_ = State::kPassing;
            return press(startX_, startY_);
        }
        case State::kScrolling: {
            const float step = lastY_ - y;
            lastY_ = y;
            return scroll(step);
        }
        case State::kPassing:
        case State::kIdle:
            break;
    }
    return pass();
}

ScrollDecision DragScroller::up(float /* x */, float y) {
    const State was = state_;
    state_ = State::kIdle;
    switch (was) {
        case State::kUndecided:
            return press(startX_, startY_);   // a tap: the controls get the press and then this release
        case State::kScrolling:
            return scroll(lastY_ - y);
        case State::kPassing:
        case State::kIdle:
            break;
    }
    return pass();
}

}  // namespace aimbuddy
