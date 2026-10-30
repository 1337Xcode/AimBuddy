#ifndef AIMBUDDY_CORE_KALMAN_H
#define AIMBUDDY_CORE_KALMAN_H

#include <algorithm>
#include <cmath>

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
 * A position and its speed on one axis, for a target that holds its course most of the time and now and then turns
 * sharply: a player strafing left and right, stopping, or starting to run.
 *
 * Two constant-velocity filters run side by side and are blended by how well each has explained the measurements (an
 * interacting multiple model filter). The steady one expects little acceleration, so a target that stands still or
 * runs straight is smoothed hard and its box does not wander. The agile one expects a lot, so after a turn it finds
 * the new speed within a frame or two, where one filter tuned for both would trail the target for a tenth of a second
 * or more. Before each step the two are mixed by the chance that the target switched between the two kinds of
 * motion, which keeps the agile one ready while the target is calm.
 *
 * The blended estimate is kept in `position`, `velocity`, `pPosition`, `pCross` and `pVelocity`; set the state only
 * through the methods, which keep both filters and the blend in step.
 */
struct ManeuveringFilter {
    PositionVelocityFilter steady;
    PositionVelocityFilter agile;
    float agileShare = 0.5f;   ///< How likely it is that the target is turning now, 0 to 1.

    float position = 0.0f;
    float velocity = 0.0f;
    float pPosition = 1.0f;
    float pCross = 0.0f;
    float pVelocity = 1.0f;

    /// Starts both filters at a measured position with unknown velocity.
    void reset(float measuredPosition, float positionVariance, float velocityVariance) {
        steady.reset(measuredPosition, positionVariance, velocityVariance);
        agile = steady;
        agileShare = 0.5f;
        blend();
    }

    /// Jumps the position to a new value while keeping the speed estimate (see PositionVelocityFilter::reanchor).
    void reanchor(float newPosition, float positionVariance) {
        steady.reanchor(newPosition, positionVariance);
        agile.reanchor(newPosition, positionVariance);
        blend();
    }

    /// Multiplies the speed by `keep`, for a target that was missed and is probably slowing down.
    void scaleVelocity(float keep) {
        steady.velocity *= keep;
        agile.velocity *= keep;
        blend();
    }

    /// Keeps the speed within plus or minus `limit`.
    void clampVelocity(float limit) {
        steady.velocity = std::max(-limit, std::min(steady.velocity, limit));
        agile.velocity = std::max(-limit, std::min(agile.velocity, limit));
        blend();
    }

    /**
     * Moves the estimate forward by dt seconds. The two acceleration variances are the steady and the agile filter's,
     * in (units/s^2)^2. A target goes from steady to agile at the rate 1 / steadyHoldS and back at 1 / agileHoldS. The
     * known shift and the extra variance are as for PositionVelocityFilter::predict.
     */
    void predict(float dt, float steadyAccelerationVariance, float agileAccelerationVariance, float steadyHoldS,
                 float agileHoldS, float knownShift = 0.0f, float extraPositionVariance = 0.0f) {
        // How likely each filter is to have been right at the start of the step, given the chance of a switch in it.
        const float toAgile = 1.0f - std::exp(-dt / std::max(steadyHoldS, 1e-3f));
        const float toSteady = 1.0f - std::exp(-dt / std::max(agileHoldS, 1e-3f));
        const float steadyShare = 1.0f - agileShare;
        const float steadyAfter = (1.0f - toAgile) * steadyShare + toSteady * agileShare;
        const float agileAfter = toAgile * steadyShare + (1.0f - toSteady) * agileShare;
        const PositionVelocityFilter steadyStart =
            mixed((1.0f - toAgile) * steadyShare / steadyAfter, steady, toSteady * agileShare / steadyAfter, agile);
        const PositionVelocityFilter agileStart =
            mixed(toAgile * steadyShare / agileAfter, steady, (1.0f - toSteady) * agileShare / agileAfter, agile);
        steady = steadyStart;
        agile = agileStart;
        agileShare = clampShare(agileAfter / (steadyAfter + agileAfter));
        steady.predict(dt, steadyAccelerationVariance, knownShift, extraPositionVariance);
        agile.predict(dt, agileAccelerationVariance, knownShift, extraPositionVariance);
        blend();
    }

    /**
     * Blends in a position measurement, and shifts the weight towards the filter that expected it more.
     *
     * A measurement further off than even a sharp turn explains (several standard deviations of the agile filter's
     * expectation) is more likely a box that belongs to something else than a target that jumped: it is trusted
     * less the further off it is, so one wrong box cannot fling the estimate and its speed across the screen.
     */
    void update(float measuredPosition, float measurementVariance) {
        const float agileSpread = agile.pPosition + measurementVariance;
        const float surprise = (measuredPosition - agile.position) * (measuredPosition - agile.position) /
                               (kOutlierSigmas * kOutlierSigmas * std::max(agileSpread, 1e-6f));
        if (surprise > 1.0f) {
            measurementVariance *= surprise;
        }
        const float steadyFit = logLikelihood(steady, measuredPosition, measurementVariance);
        const float agileFit = logLikelihood(agile, measuredPosition, measurementVariance);
        const float best = std::max(steadyFit, agileFit);
        const float steadyWeight = (1.0f - agileShare) * std::exp(steadyFit - best);
        const float agileWeight = agileShare * std::exp(agileFit - best);
        agileShare = clampShare(agileWeight / (steadyWeight + agileWeight));
        steady.update(measuredPosition, measurementVariance);
        agile.update(measuredPosition, measurementVariance);
        blend();
    }

private:
    /// A measurement this many standard deviations from what the agile filter expects starts to be doubted.
    static constexpr float kOutlierSigmas = 4.0f;

    /// Neither filter is ever ruled out, so the other can take over again within a step.
    static float clampShare(float share) { return std::max(0.01f, std::min(share, 0.99f)); }

    /// Log of how likely the measurement is under one filter's prediction, up to a constant the two share.
    static float logLikelihood(const PositionVelocityFilter& filter, float measuredPosition, float measurementVariance) {
        const float variance = std::max(filter.pPosition + measurementVariance, 1e-6f);
        const float innovation = measuredPosition - filter.position;
        return -0.5f * innovation * innovation / variance - 0.5f * std::log(variance);
    }

    /// The weighted mix of two filters, with the spread between them added to the uncertainty.
    static PositionVelocityFilter mixed(float weightA, const PositionVelocityFilter& a, float weightB,
                                        const PositionVelocityFilter& b) {
        PositionVelocityFilter out;
        out.position = weightA * a.position + weightB * b.position;
        out.velocity = weightA * a.velocity + weightB * b.velocity;
        const float dpA = a.position - out.position;
        const float dvA = a.velocity - out.velocity;
        const float dpB = b.position - out.position;
        const float dvB = b.velocity - out.velocity;
        out.pPosition = weightA * (a.pPosition + dpA * dpA) + weightB * (b.pPosition + dpB * dpB);
        out.pCross = weightA * (a.pCross + dpA * dvA) + weightB * (b.pCross + dpB * dvB);
        out.pVelocity = weightA * (a.pVelocity + dvA * dvA) + weightB * (b.pVelocity + dvB * dvB);
        return out;
    }

    void blend() {
        const PositionVelocityFilter both = mixed(1.0f - agileShare, steady, agileShare, agile);
        position = both.position;
        velocity = both.velocity;
        pPosition = both.pPosition;
        pCross = both.pCross;
        pVelocity = both.pVelocity;
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
