#ifndef AIMBUDDY_CORE_EGO_MOTION_H
#define AIMBUDDY_CORE_EGO_MOTION_H

#include <cstdint>

namespace aimbuddy {

/**
 * Describes how far the whole scene slides across the screen because the camera was turned.
 *
 * The tracker uses it to tell a target's own movement apart from movement caused by aiming.
 * Without it, a target the player is following looks motionless on screen, so its real speed is
 * lost and a moving target can never be led.
 *
 * Implementations must be safe to call from several threads.
 */
class EgoMotion {
public:
    virtual ~EgoMotion() = default;

    /**
     * Scene displacement on screen between two instants, in screen pixels.
     * Positive x means scene content moved right; positive y means it moved down.
     * Times are on the steady clock, in nanoseconds, with fromNs <= toNs.
     */
    virtual void shiftBetween(int64_t fromNs, int64_t toNs, float* dx, float* dy) const = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_EGO_MOTION_H
