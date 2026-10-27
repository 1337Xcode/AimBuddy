#include "response_estimator.h"

#include <algorithm>
#include <cmath>

namespace aimbuddy {

namespace {

/// Measurements below this quality are not used.
constexpr float kMinQuality = 0.2f;

/// A slide measured over longer than this says too little about any one finger move.
constexpr int64_t kMaxIntervalNs = 150'000'000;

/// Finger travel (pixels) that must land in a sample's window for it to teach anything.
constexpr float kMinSampleTravel = 0.5f;

/// Share of the statistics kept with each informative sample: they fade over about a second.
constexpr double kKeep = 0.985;

/// Finger travel (squared pixels, summed) an axis needs before its gain is looked at at all.
constexpr double kMinAxisEnergy = 25.0;

/// The gain is pinned down to this share of itself (one standard error) when confidence is one half.
constexpr double kHalfConfidenceUncertainty = 0.2;

/// Measurements needed before the estimate can be fully trusted.
constexpr double kFullSampleCount = 4.0;

/// Gains outside this range are measurement mistakes.
constexpr float kMinGain = 0.05f;
constexpr float kMaxGain = 80.0f;

/// Least share of the measured slide the fit must explain.
constexpr double kMinConfidence = 0.3;

/// How much worse (as a share of all measured slide) neighbouring delays must fit for the delay to be believed.
constexpr double kDelayContrast = 0.02;

}  // namespace

void ResponseEstimator::setInvertY(bool invert) {
    if (invert != invertY_) {
        invertY_ = invert;
        y_ = Axis();   // what was learned about up and down had the opposite sign
    }
}

void ResponseEstimator::fade(Axis& axis, double keep) const {
    for (int j = 0; j < kDelayCount; ++j) {
        axis.slideTimesFinger[j] *= keep;
        axis.fingerSquared[j] *= keep;
    }
    axis.slideSquared *= keep;
    axis.weight *= keep;
}

void ResponseEstimator::addSample(const FingerLog& fingers, const SceneShift& shift) {
    const int64_t intervalNs = shift.toNs - shift.fromNs;
    if (intervalNs <= 0 || intervalNs > kMaxIntervalNs || shift.quality < kMinQuality ||
        !std::isfinite(shift.dx) || !std::isfinite(shift.dy)) {
        return;
    }

    // The finger travel that would reach this window for each delay being tried.
    std::array<float, kDelayCount> fingerX{};
    std::array<float, kDelayCount> fingerY{};
    float mostTravel = 0.0f;
    for (int j = 0; j < kDelayCount; ++j) {
        fingers.landedBetween(static_cast<float>(j) * kDelayStepS, shift.fromNs, shift.toNs, &fingerX[j], &fingerY[j]);
        mostTravel = std::max(mostTravel, std::hypot(fingerX[j], fingerY[j]));
    }
    if (mostTravel < kMinSampleTravel) {
        return;   // a quiet moment: nothing to learn, and nothing to forget either
    }

    fade(x_, kKeep);
    fade(y_, kKeep);
    const double weight = shift.quality;
    for (int j = 0; j < kDelayCount; ++j) {
        x_.slideTimesFinger[j] += weight * shift.dx * fingerX[j];
        x_.fingerSquared[j] += weight * fingerX[j] * fingerX[j];
        y_.slideTimesFinger[j] += weight * shift.dy * fingerY[j];
        y_.fingerSquared[j] += weight * fingerY[j] * fingerY[j];
    }
    x_.slideSquared += weight * shift.dx * shift.dx;
    y_.slideSquared += weight * shift.dy * shift.dy;
    x_.weight += weight;
    y_.weight += weight;
    ++samples_;
}

ResponseEstimator::Estimate ResponseEstimator::estimate() const {
    Estimate result;
    const auto mostEnergy = [](const Axis& axis) {
        return *std::max_element(axis.fingerSquared.begin(), axis.fingerSquared.end());
    };
    const bool useX = mostEnergy(x_) >= kMinAxisEnergy;
    const bool useY = mostEnergy(y_) >= kMinAxisEnergy;
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
        residual[j] = (useX ? residualFor(x_, j) : 0.0) + (useY ? residualFor(y_, j) : 0.0);
    }
    const int best = static_cast<int>(std::min_element(residual.begin(), residual.end()) - residual.begin());
    const double totalSlide = (useX ? x_.slideSquared : 0.0) + (useY ? y_.slideSquared : 0.0);
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

    // A delay is only believed when fits at clearly different delays are worse.
    const int farBelow = std::max(best - 2, 0);
    const int farAbove = std::min(best + 2, kDelayCount - 1);
    const double neighbours = 0.5 * (residual[farBelow] + residual[farAbove]);
    result.hasDelay = (neighbours - residual[best]) / totalSlide >= kDelayContrast;

    const double explained = std::clamp(1.0 - residual[best] / totalSlide, 0.0, 1.0);

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
        addUncertainty(x_);
    }
    if (useY) {
        addUncertainty(y_);
    }
    if (usedAxes == 0) {
        return result;
    }
    const double uncertainty = uncertaintySum / usedAxes;
    const double ratio = uncertainty / kHalfConfidenceUncertainty;
    const double pinnedDown = 1.0 / (1.0 + ratio * ratio);
    const double seen = std::min(1.0, (usedWeight / usedAxes) / kFullSampleCount);
    result.confidence = static_cast<float>(explained * pinnedDown * seen);

    if (useX && x_.fingerSquared[best] > 1e-9) {
        result.gainX = static_cast<float>(-x_.slideTimesFinger[best] / x_.fingerSquared[best]);
        result.hasGainX = result.gainX >= kMinGain && result.gainX <= kMaxGain;
    }
    if (useY && y_.fingerSquared[best] > 1e-9) {
        const double slope = y_.slideTimesFinger[best] / y_.fingerSquared[best];
        result.gainY = static_cast<float>(invertY_ ? slope : -slope);
        result.hasGainY = result.gainY >= kMinGain && result.gainY <= kMaxGain;
    }
    result.valid = result.confidence >= kMinConfidence && (result.hasGainX || result.hasGainY);
    return result;
}

void ResponseEstimator::reset() {
    x_ = Axis();
    y_ = Axis();
    samples_ = 0;
}

}  // namespace aimbuddy
