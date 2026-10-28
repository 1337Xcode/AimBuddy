#ifndef AIMBUDDY_CORE_LOOK_WATCH_H
#define AIMBUDDY_CORE_LOOK_WATCH_H

#include <cstdint>

#include "player_fingers.h"

namespace aimbuddy {

/**
 * Says how much the player is steering the camera with their own finger, from what the touch
 * helper reports of the player's fingers.
 *
 * The assist's finger goes in the game's look area, so the player's finger that steers is the one
 * on the same half of the display as the assist's resting place. A thumb on the other half is the
 * joystick and does not count. A finger that is down but still is not steering either: only
 * movement is, from a slow drift to a flick. The answer rises at once and falls away over about
 * a third of a second, so the brief stops between a player's small movements do not make the assist
 * lurch back to full strength.
 *
 * Pure: it has no clock, no input and no output.
 */
class LookWatch {
public:
    /**
     * Returns 0 (not steering) to 1 (clearly steering) at `nowNs` (steady clock).
     * `activity` is the helper's latest report and `reportedNs` when it came; a report that has not been
     * renewed for a moment is not believed. The look area is on the half of the display that `anchorX` is on.
     */
    float update(int64_t nowNs, const touch::PlayerFingers::Activity& activity, int64_t reportedNs, float anchorX,
                 float displayWidth, float displayHeight);

    /// Forgets what was seen, for a restart of the assist.
    void reset();

private:
    float steering_ = 0.0f;
    int64_t lastNs_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_LOOK_WATCH_H
