#include "response_estimator.h"

#include <algorithm>
#include <cmath>

namespace aimbuddy {

namespace {

/// Measurements below this quality are not used.
constexpr float kMinQuality = 0.2f;

/// A slide measured over longer than this says too little about any one finger move.
constexpr int64_t kMaxIntervalNs = 150'000'000;

/// Two samples are compared with each other only when the second one starts no later than this after the first one ended.
constexpr int64_t kMaxGapNs = 40'000'000;

/// Finger travel (pixels) that must land in a sample's window for it to teach anything.
constexpr float kMinSampleTravel = 0.5f;

/// Share of the statistics kept with each informative sample: they fade over about a second.
constexpr double kKeep = 0.985;

/// Finger travel (squared, summed, in pixels per second squared) an axis needs before its gain is looked at at all.
constexpr double kMinAxisEnergy = 2.5e4;

/// The gain is pinned down to this share of itself (one standard error) when confidence is one half.
constexpr double kHalfConfidenceUncertainty = 0.2;

/// Measurements needed before the estimate can be fully trusted.
constexpr double kFullSampleCount = 4.0;

/// Gains outside this range are measurement mistakes.
constexpr float kMinGain = 0.05f;
constexpr float kMaxGain = 80.0f;

/// Least confidence at which a fit is taken to say anything about the gain.
constexpr double kMinConfidence = 0.3;

/// How much worse neighbouring delays must fit, in units of the variance of what the fit leaves
/// unexplained, for the delay to be believed. A fit that is worse by this much is unlikely to be so by chance.
constexpr double kDelayContrast = 9.0;

}  // namespace

void ResponseEstimator::setInvertY(bool invert) {
    if (invert != invertY_) {
        invertY_ = invert;
        straight_.y = Axis();   // what was learned about up and down had the opposite sign
        changes_.y = Axis();
    }
}

void ResponseEstimator::accumulate(Statistics& statistics, double slideX, double slideY, const FingerRates& fingerX, const FingerRates& fingerY, double weight) {
    const auto addTo = [&](Axis& axis, double slide, const FingerRates& finger) {
        for (int j = 0; j < kDelayCount; ++j) {
            axis.slideTimesFinger[j] = axis.slideTimesFinger[j] * kKeep + weight * slide * finger[j];
            axis.fingerSquared[j] = axis.fingerSquared[j] * kKeep + weight * finger[j] * finger[j];
        }
        axis.slideSquared = axis.slideSquared * kKeep + weight * slide * slide;
        axis.weight = axis.weight * kKeep + weight;
    };
    addTo(statistics.x, slideX, fingerX);
    addTo(statistics.y, slideY, fingerY);
}

void ResponseEstimator::addSample(const FingerLog& fingers, const SceneShift& shift) {
    const int64_t intervalNs = shift.toNs - shift.fromNs;
    if (intervalNs <= 0 || intervalNs > kMaxIntervalNs || shift.quality < kMinQuality ||
        !std::isfinite(shift.dx) || !std::isfinite(shift.dy)) {
        return;
    }

    // The finger travel that would reach this window for each delay being tried, as rates, together
    // with the slide's own rate.
    const double seconds = static_cast<double>(intervalNs) * 1e-9;
    Rates current;
    current.valid = true;
    current.toNs = shift.toNs;
    current.quality = shift.quality;
    current.slideX = static_cast<double>(shift.dx) / seconds;
    current.slideY = static_cast<double>(shift.dy) / seconds;
    float mostTravel = 0.0f;
    for (int j = 0; j < kDelayCount; ++j) {
        float travelX = 0.0f;
        float travelY = 0.0f;
        fingers.landedBetween(static_cast<float>(j) * kDelayStepS, shift.fromNs, shift.toNs, &travelX, &travelY);
        mostTravel = std::max(mostTravel, std::hypot(travelX, travelY));
        current.fingerX[j] = static_cast<double>(travelX) / seconds;
        current.fingerY[j] = static_cast<double>(travelY) / seconds;
    }

    const Rates before = previous_;
    previous_ = current;
    if (mostTravel < kMinSampleTravel) {
        return;   // a quiet moment: nothing to learn, and nothing to forget either
    }
    accumulate(straight_, current.slideX, current.slideY, current.fingerX, current.fingerY, shift.quality);
    ++samples_;

    // Against the sample before it, when that one ended about when this one began.
    const bool follows = before.valid && shift.fromNs - before.toNs <= kMaxGapNs && shift.fromNs >= before.toNs - kMaxGapNs;
    if (!follows) {
        return;
    }
    FingerRates fingerChangeX;
    FingerRates fingerChangeY;
    for (int j = 0; j < kDelayCount; ++j) {
        fingerChangeX[j] = current.fingerX[j] - before.fingerX[j];
        fingerChangeY[j] = current.fingerY[j] - before.fingerY[j];
    }
    accumulate(changes_, current.slideX - before.slideX, current.slideY - before.slideY, fingerChangeX, fingerChangeY,
               static_cast<double>(shift.quality) * static_cast<double>(before.quality));
}

ResponseEstimator::Estimate ResponseEstimator::estimate() const {
    // The straight fit is the more exact one while the assist's finger is what turns the camera. Once the
    // player turns it too, the straight fit can still find a believable delay, near the right one, with a
    // gain that is far too low: that its delay stands out says nothing about its gain. What does give it away
    // is how well each fit pins its gain down, because the turning fills the straight fit's misfit. The fit
    // that is more confident is the one used; a tie goes to the straight one.
    const Estimate straight = fit(straight_);
    const Estimate changes = fit(changes_);
    if (changes.valid && (!straight.valid || changes.confidence > straight.confidence)) {
        return changes;
    }
    return straight;
}

ResponseEstimator::Estimate ResponseEstimator::fit(const Statistics& statistics) const {
    const Axis& axisX = statistics.x;
    const Axis& axisY = statistics.y;
    Estimate result;
    const auto mostEnergy = [](const Axis& axis) {
        return *std::max_element(axis.fingerSquared.begin(), axis.fingerSquared.end());
    };
    const bool useX = mostEnergy(axisX) >= kMinAxisEnergy;
    const bool useY = mostEnergy(axisY) >= kMinAxisEnergy;
    if (!useX && !useY) {
        return result;
    }

    // How badly a straight-line fit of slide against finger travel misses, for each delay. The
    // delay with the smallest miss is the one at which the finger's moves line up with the slides.
    const auto residualFor = [](const Axis& axis, int j) {
        if (axis.fingerSquared[j] < 1e-9) {
            return axis.slideSquared;
        }
        return axis.slideSquared - axis.slideTimesFinger[j] * axis.slideTimesFinger[j] / axis.fingerSquared[j];
    };
    std::array<double, kDelayCount> residual{};
    for (int j = 0; j < kDelayCount; ++j) {
        residual[j] = (useX ? residualFor(axisX, j) : 0.0) + (useY ? residualFor(axisY, j) : 0.0);
    }
    const int best = static_cast<int>(std::min_element(residual.begin(), residual.end()) - residual.begin());
    const double totalSlide = (useX ? axisX.slideSquared : 0.0) + (useY ? axisY.slideSquared : 0.0);
    if (totalSlide < 1e-9) {
        return result;
    }

    // Between tried delays: the lowest point of the parabola through the best fit and its neighbours.
    float delaySteps = static_cast<float>(best);
    if (best > 0 && best < kDelayCount - 1) {
        const double below = residual[best - 1];
        const double above = residual[best + 1];
        const double curvature = below - 2.0 * residual[best] + above;
        if (curvature > 1e-9) {
            delaySteps += static_cast<float>(std::clamp(0.5 * (below - above) / curvature, -0.5, 0.5));
        }
    }
    result.delayS = delaySteps * kDelayStepS;

    // A delay is only believed when fits at clearly different delays are worse than chance allows,
    // however much of the slide the player's own turning accounts for and no finger move explains.
    const int farBelow = std::max(best - 2, 0);
    const int farAbove = std::min(best + 2, kDelayCount - 1);
    const double neighbours = 0.5 * (residual[farBelow] + residual[farAbove]);
    const double usedSamples = (useX ? axisX.weight : 0.0) + (useY ? axisY.weight : 0.0);
    const double unexplainedVariance = std::max(residual[best], 0.0) / std::max(usedSamples - 2.0, 1.0);
    result.hasDelay = unexplainedVariance > 1e-12 ? (neighbours - residual[best]) / unexplainedVariance >= kDelayContrast
                                                  : neighbours - residual[best] > 1e-9;

    // How exactly each used axis pins down its gain: the standard error of a straight-line fit,
    // as a share of the gain. A few clean measurements pin it down well; many noisy ones may not.
    double uncertaintySum = 0.0;
    double usedWeight = 0.0;
    int usedAxes = 0;
    const auto addUncertainty = [&](const Axis& axis) {
        const double finger = axis.fingerSquared[best];
        if (finger < 1e-9 || axis.weight < 1.0) {
            return;
        }
        const double gain = std::fabs(axis.slideTimesFinger[best] / finger);
        const double misfit = std::max(residualFor(axis, best), 0.0);
        const double variance = misfit / std::max(axis.weight - 1.0, 1.0) / finger;
        uncertaintySum += std::sqrt(variance) / std::max(gain, 0.05);
        usedWeight += axis.weight;
        ++usedAxes;
    };
    if (useX) {
        addUncertainty(axisX);
    }
    if (useY) {
        addUncertainty(axisY);
    }
    if (usedAxes == 0) {
        return result;
    }
    // The confidence is how exactly the gain is pinned down, not how much of the slide the fit explains:
    // in a real match most of the slide is the player's own turning, which no finger move explains
    // and which averages out of the gain given enough moves.
    const double uncertainty = uncertaintySum / usedAxes;
    const double ratio = uncertainty / kHalfConfidenceUncertainty;
    const double pinnedDown = 1.0 / (1.0 + ratio * ratio);
    const double seen = std::min(1.0, (usedWeight / usedAxes) / kFullSampleCount);
    result.confidence = static_cast<float>(pinnedDown * seen);

    if (useX && axisX.fingerSquared[best] > 1e-9) {
        result.gainX = static_cast<float>(-axisX.slideTimesFinger[best] / axisX.fingerSquared[best]);
        result.hasGainX = result.gainX >= kMinGain && result.gainX <= kMaxGain;
    }
    if (useY && axisY.fingerSquared[best] > 1e-9) {
        const double slope = axisY.slideTimesFinger[best] / axisY.fingerSquared[best];
        result.gainY = static_cast<float>(invertY_ ? slope : -slope);
        result.hasGainY = result.gainY >= kMinGain && result.gainY <= kMaxGain;
    }
    result.valid = result.confidence >= kMinConfidence && (result.hasGainX || result.hasGainY);
    return result;
}

void ResponseEstimator::reset() {
    straight_ = Statistics();
    changes_ = Statistics();
    previous_ = Rates();
    samples_ = 0;
}

}  // namespace aimbuddy
