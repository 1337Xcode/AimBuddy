#ifndef AIMBUDDY_CORE_KALMAN_H
#define AIMBUDDY_CORE_KALMAN_H

#include <algorithm>

namespace aimbuddy {

/**
 * Constant-velocity Kalman filter for one axis: tracks a position and its rate of change.
 *
 * The process model assumes the velocity changes by random accelerations, so the filter follows
 * a target that speeds up, slows down or stops, and the estimated velocity can be used to look
 * ahead. Covariance is stored as its three unique entries.
 */
struct PositionVelocityFilter {
    float position = 0.0f;
    float velocity = 0.0f;
    float pPosition = 1.0f;         ///< Variance of the position estimate.
    float pCross = 0.0f;            ///< Covariance between position and velocity.
    float pVelocity = 1.0f;         ///< Variance of the velocity estimate.

    /// Starts the filter at a measured position with unknown velocity.
    void reset(float measuredPosition, float positionVariance, float velocityVariance) {
        position = measuredPosition;
        velocity = 0.0f;
        pPosition = positionVariance;
        pCross = 0.0f;
        pVelocity = velocityVariance;
    }

    /**
     * Jumps the position to a new value while keeping the speed estimate.
     *
     * Used when the thing being measured changed (a target stepped out from behind cover, so the
     * visible box suddenly covers different pixels). The jump is not movement and must not be
     * read as speed.
     */
    void reanchor(float newPosition, float positionVariance) {
        position = newPosition;
        pPosition = positionVariance;
        pCross = 0.0f;
    }

    /**
     * Moves the estimate forward by dt seconds.
     *
     * accelerationVariance is in (units/s^2)^2. knownShift is a displacement the caller can
     * account for exactly, such as the screen scrolling because the camera was turned, so it
     * is added to the position without being mistaken for the target's own velocity.
     * extraPositionVariance widens the position uncertainty by an amount that does not come
     * from the target's motion, such as the known shift itself being slightly wrong. The next
     * measurement then corrects the position firmly instead of being blamed on speed.
     */
    void predict(float dt, float accelerationVariance, float knownShift = 0.0f,
                 float extraPositionVariance = 0.0f) {
        position += velocity * dt + knownShift;
        const float dt2 = dt * dt;
        const float newPPosition = pPosition + 2.0f * dt * pCross + dt2 * pVelocity +
                                   accelerationVariance * dt2 * dt2 * 0.25f;
        const float newPCross = pCross + dt * pVelocity + accelerationVariance * dt2 * dt * 0.5f;
        const float newPVelocity = pVelocity + accelerationVariance * dt2;
        pPosition = newPPosition + extraPositionVariance;
        pCross = newPCross;
        pVelocity = newPVelocity;
    }

    /// Blends in a position measurement with the given variance.
    void update(float measuredPosition, float measurementVariance) {
        const float innovation = measuredPosition - position;
        const float innovationVariance = pPosition + measurementVariance;
        const float gainPosition = pPosition / innovationVariance;
        const float gainVelocity = pCross / innovationVariance;
        position += gainPosition * innovation;
        velocity += gainVelocity * innovation;
        const float newPPosition = (1.0f - gainPosition) * pPosition;
        const float newPCross = (1.0f - gainPosition) * pCross;
        const float newPVelocity = pVelocity - gainVelocity * pCross;
        pPosition = newPPosition;
        pCross = newPCross;
        pVelocity = std::max(newPVelocity, 0.0f);
    }
};

/**
 * Random-walk Kalman filter for a slowly varying value such as a box width or height.
 * Averaging many noisy measurements keeps the drawn box from pulsing with detector noise.
 */
struct LevelFilter {
    float value = 0.0f;
    float variance = 1.0f;

    void reset(float measured, float measurementVariance) {
        value = measured;
        variance = measurementVariance;
    }

    /// Lets the estimate drift: processVariance is the variance added per second.
    void predict(float dt, float processVariance) { variance += processVariance * dt; }

    void update(float measured, float measurementVariance) {
        const float gain = variance / (variance + measurementVariance);
        value += gain * (measured - value);
        variance = (1.0f - gain) * variance;
    }
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_KALMAN_H
