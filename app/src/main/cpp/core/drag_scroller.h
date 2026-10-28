/**
 * drag_scroller.h - Tells a finger that scrolls a page from one that presses a control on it.
 *
 * The menu's page is full of controls (switches, sliders, lists), so a finger that goes down on the page
 * does not yet say what it means to do. This class keeps the touch back until it does: a finger that moves
 * mostly up or down scrolls the page, and one that stays put or moves mostly sideways is pressing a control
 * (a switch, or a slider being dragged), which then gets the press from where the finger first went down.
 *
 * Pure logic: it knows nothing of ImGui, the screen or the clock. The menu feeds it what the finger does and
 * does what it says.
 */
#ifndef AIMBUDDY_CORE_DRAG_SCROLLER_H
#define AIMBUDDY_CORE_DRAG_SCROLLER_H

namespace aimbuddy {

/// What the menu should do with one touch event.
struct ScrollDecision {
    enum class Kind {
        kPass,     ///< Give the event to the controls as it is.
        kHold,     ///< Give the controls nothing yet: it is not known whether the finger presses or scrolls.
        kPress,    ///< The finger presses: give the controls a press at (pressX, pressY), where it went down, and then this event.
        kScroll,   ///< The finger scrolls: move the page by scrollPx and give the controls nothing.
    };

    Kind kind = Kind::kPass;
    float pressX = 0.0f;
    float pressY = 0.0f;
    float scrollPx = 0.0f;   ///< How far the page's contents move up (positive) or down (negative) in this step.
};

class DragScroller {
public:
    /// `slopPx` is how far a finger has to move before it is more than a press.
    explicit DragScroller(float slopPx) : slopPx_(slopPx) {}

    /// Where a finger can start a scroll, in the same pixels as the events. An empty area means nowhere.
    void setArea(float left, float top, float right, float bottom) {
        left_ = left;
        top_ = top;
        right_ = right;
        bottom_ = bottom;
    }

    ScrollDecision down(float x, float y);
    ScrollDecision move(float x, float y);
    ScrollDecision up(float x, float y);

    /// Forgets the finger, for when the menu closes while it is down.
    void cancel() { state_ = State::kIdle; }

private:
    enum class State {
        kIdle,        ///< No finger.
        kUndecided,   ///< A finger went down in the area and has not moved far.
        kScrolling,   ///< The finger scrolls the page.
        kPassing,     ///< The finger goes to the controls.
    };

    bool inArea(float x, float y) const { return x >= left_ && x < right_ && y >= top_ && y < bottom_; }

    float slopPx_;
    float left_ = 0.0f;
    float top_ = 0.0f;
    float right_ = 0.0f;
    float bottom_ = 0.0f;
    State state_ = State::kIdle;
    float startX_ = 0.0f;
    float startY_ = 0.0f;
    float lastY_ = 0.0f;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_DRAG_SCROLLER_H
