#ifndef AIMBUDDY_CORE_RESPONSE_ESTIMATOR_H
#define AIMBUDDY_CORE_RESPONSE_ESTIMATOR_H

#include <array>
#include <cstdint>

#include "finger_log.h"

namespace aimbuddy {

/// How the whole scene was seen to slide between two captured frames, in screen pixels.
struct SceneShift {
    int64_t fromNs = 0;
    int64_t toNs = 0;
    float dx = 0.0f;
    float dy = 0.0f;
    float quality = 1.0f;   ///< 0 to 1: how far to trust the measurement.
};

/**
 * Works out how a game's camera answers a dragged finger, from what the assist itself does.
 *
 * Every time the assist drags its finger the scene slides a moment later. Comparing each measured
 * slide with the drags that could have caused it, for a range of possible delays, gives two
 * things by a plain least-squares fit: how far the scene slides per finger pixel (the gain), and
 * the delay at which the fit is best. Nothing has to be set by the player and nothing special has
 * to be done: ordinary aiming is the experiment.
 *
 * Statistics fade slowly, so the estimate follows a change of scope or sensitivity within a few
 * seconds. Quiet moments, when the finger did not move, teach nothing and change nothing.
 *
 * Not thread safe: callers lock.
 */
class ResponseEstimator {
public:
    struct Estimate {
        float gainX = 0.0f;
        float gainY = 0.0f;
        bool hasGainX = false;    ///< The finger moved far enough sideways to say.
        bool hasGainY = false;    ///< The finger moved far enough up and down to say.
        float delayS = 0.0f;
        bool hasDelay = false;    ///< The fit clearly prefers one delay over its neighbours.
        float confidence = 0.0f;  ///< 0 to 1: how much of the measured slide the fit explains, and how exactly the gain is pinned down.
        bool valid = false;       ///< Something can be said about the gain.
    };

    /// Tells the estimator which way up and down go (a game may look up when the finger moves down).
    void setInvertY(bool invert);

    /// Learns from one measured slide, comparing it with the finger moves in `fingers`.
    void addSample(const FingerLog& fingers, const SceneShift& shift);

    /// The best estimate so far.
    Estimate estimate() const;

    /// Forgets everything learned.
    void reset();

    /// How many measurements have taught something since the last reset.
    int sampleCount() const { return samples_; }

    /// Longest delay that is tried, and the step between tried delays.
    static constexpr int kDelayCount = 25;
    static constexpr float kDelayStepS = 0.008f;

private:
    struct Axis {
        std::array<double, kDelayCount> slideTimesFinger{};   ///< Sum of slide * finger travel, for each delay.
        std::array<double, kDelayCount> fingerSquared{};      ///< Sum of finger travel squared, for each delay.
        double slideSquared = 0.0;                            ///< Sum of slide squared.
        double weight = 0.0;                                  ///< How many measurements these sums stand for.
    };

    void fade(Axis& axis, double keep) const;

    bool invertY_ = false;
    Axis x_;
    Axis y_;
    int samples_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_RESPONSE_ESTIMATOR_H
