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
 * Two fits are kept from the same measurements. The straight one relates each slide to the finger
 * travel that could have caused it, which is exact while the finger is the only thing turning the
 * camera. When the player turns the camera too, and the assist's finger mostly reacts to that
 * turning, it ties slide and finger together by more than the camera's response and misses both the
 * gain and the delay. The second fit relates how the slide changes from one measurement to the next
 * to how the finger travel changes, which drops a smooth turn out and keeps the camera's response.
 * It is the less exact of the two when nobody else turns the camera, so the estimate comes from
 * whichever of the two pins the gain down better.
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
        float confidence = 0.0f;  ///< 0 to 1: how exactly the gain is pinned down, and how many measurements stand behind it.
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

    /// The sums of one fit, for each axis.
    struct Statistics {
        Axis x;
        Axis y;
    };

    /// The rates (per second) of the slide that was seen and of the finger travel that would reach it for each delay, of the sample before this one.
    struct Rates {
        bool valid = false;
        int64_t toNs = 0;
        float quality = 0.0f;
        double slideX = 0.0;
        double slideY = 0.0;
        std::array<double, kDelayCount> fingerX{};
        std::array<double, kDelayCount> fingerY{};
    };

    using FingerRates = std::array<double, kDelayCount>;

    /// Fades what was gathered, then adds one pair of slide and finger rates (or of changes in them) of the given weight.
    static void accumulate(Statistics& statistics, double slideX, double slideY, const FingerRates& fingerX, const FingerRates& fingerY, double weight);

    /// What one fit says.
    Estimate fit(const Statistics& statistics) const;

    bool invertY_ = false;
    Statistics straight_;   ///< Each slide against the finger travel that could have caused it.
    Statistics changes_;    ///< How each slide differs from the one before against how the finger travel differs.
    Rates previous_;
    int samples_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_RESPONSE_ESTIMATOR_H
