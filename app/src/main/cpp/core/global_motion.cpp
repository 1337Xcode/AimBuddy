#include "global_motion.h"

#include <algorithm>
#include <cmath>

namespace aimbuddy {

namespace {

constexpr int kGrid = GlobalMotionEstimator::kGrid;
constexpr int kCells = kGrid * kGrid;
constexpr float kPi = 3.14159265358979f;

/// The window is flat over the middle of the picture and fades out over this share of each side,
/// so that most of the picture counts and the edges do not leak into the spectrum.
constexpr float kWindowTaper = 0.4f;

/// Standard deviation, in frequency bins, of the low-pass weight applied to the cross-power spectrum.
constexpr float kLowPassSigma = 12.0f;

/// A pixel has changed between two frames when its grey value (0 to 255) differs by more than this.
constexpr float kChangedLevel = 4.0f;

/// Frames in which fewer than this share of the pixels changed are taken to be the same. Counting
/// the changed pixels, rather than averaging the difference over all of them, keeps a picture that
/// is mostly flat (sky, plain ground) from looking unchanged when its few edges slide.
constexpr float kUnchangedShare = 0.003f;

/// When more than this share of the pixels changed the picture has really moved, and the pixels
/// that did not change are noted as not belonging to the moving scene.
constexpr float kMotionShare = 0.012f;

/// A pixel with an edge or texture (a grey step of at least this much per grid pixel) that differs
/// by less than the second value between two such frames did not move with the scene. Flat pixels
/// say nothing either way, because they do not change however the scene slides.
constexpr float kStaticMinGradient = 4.0f;
constexpr float kStaticMaxDifference = 3.0f;

/// How a pixel's "does not move with the scene" score grows and falls, and where it is ignored.
constexpr int kStaticGain = 3;
constexpr int kStaticLoss = 6;
constexpr int kStaticIgnored = 18;

/// A peak is a candidate for the scene's motion when it is at least this fraction of the strongest one's height.
constexpr float kRivalPeakShare = 0.15f;

/// Peaks closer together than this many grid pixels count as one.
constexpr int kPeakSeparation = 3;

/// Steps of the refinement, and the longest step (in grid pixels) that one of them may take.
constexpr int kRefineSteps = 5;
constexpr float kMaxRefineStep = 2.0f;

/// In the refinement a difference between the frames counts less the larger it is, and not at all
/// beyond this many grey levels: something moving by itself must not drag the answer along.
constexpr float kRejectLevel = 24.0f;

/// How well a shift explains the picture is the average, over the pixels with texture (steps of at
/// least the first value per grid pixel), of how closely the two frames agree there once the shift
/// is applied: one for a perfect match, falling to zero at the rejection limit above. It is at
/// least the first value for quality above zero and at least the second for full quality.
constexpr float kTextureMinGradient = 3.0f;
constexpr float kMinAgreement = 0.3f;
constexpr float kFullAgreement = 0.8f;

/// Share of the picture that must be textured for a match to count at all, and for it to count in full.
/// With less than the second (a mostly flat picture) the agreement is trusted proportionally less.
constexpr float kMinTexturedShare = 0.01f;
constexpr float kFullTexturedShare = 0.05f;

/// A shift is only believed when the textured pixels it can compare (those that stay inside the
/// picture) are at least this share of all the textured pixels, and at least this many matched.
constexpr float kMinOverlapShare = 0.25f;
constexpr float kMinMatchedWeight = 20.0f;

/// Two shifts further apart than this (grid pixels) are different answers.
constexpr float kDistinctShift = 1.5f;

/// A rival shift that fits nearly as well as the best one makes the answer doubtful: the quality
/// falls to this share of itself when the rival ties with the best, starting when it is within the range.
constexpr float kAmbiguousKeep = 0.3f;
constexpr float kAmbiguityRange = 0.2f;

/// A pixel counts as matched in the next look at the picture when the frames agree to within this many grey levels.
constexpr float kExplainedDifference = 9.0f;

/// When the best shift explains less than this much, the picture holds something else that moves
/// differently; the pixels it does not explain are looked at again, up to this many times.
constexpr float kGoodAgreement = 0.8f;
constexpr int kExtraLayers = 2;

/// Pixels the best shift explains keep this share of their weight in the next look.
constexpr float kExplainedWeightKept = 0.1f;

/// Wraps an index in [0, kGrid) to a signed offset in [-kGrid / 2, kGrid / 2).
int signedOffset(int index) { return index < kGrid / 2 ? index : index - kGrid; }

/// The value of a grid picture between its pixels (bilinear), or false when the point is outside it.
bool sampleAt(const std::array<float, kCells>& picture, float x, float y, float* value) {
    if (x < 0.0f || y < 0.0f || x > static_cast<float>(kGrid - 1) - 1e-3f || y > static_cast<float>(kGrid - 1) - 1e-3f) {
        return false;
    }
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float top = picture[y0 * kGrid + x0] * (1.0f - fx) + picture[y0 * kGrid + x0 + 1] * fx;
    const float bottom = picture[(y0 + 1) * kGrid + x0] * (1.0f - fx) + picture[(y0 + 1) * kGrid + x0 + 1] * fx;
    *value = top * (1.0f - fy) + bottom * fy;
    return true;
}

}  // namespace

GlobalMotionEstimator::GlobalMotionEstimator() {
    for (int k = 0; k < kGrid / 2; ++k) {
        cosine_[k] = std::cos(2.0f * kPi * static_cast<float>(k) / static_cast<float>(kGrid));
        sine_[k] = std::sin(2.0f * kPi * static_cast<float>(k) / static_cast<float>(kGrid));
    }
    int bits = 0;
    while ((1 << bits) < kGrid) {
        ++bits;
    }
    for (int i = 0; i < kGrid; ++i) {
        int reversed = 0;
        for (int b = 0; b < bits; ++b) {
            reversed |= ((i >> b) & 1) << (bits - 1 - b);
        }
        bitReversed_[i] = reversed;
    }

    // The window is flat over the middle and fades out towards the border.
    const auto taper = [](int index) {
        const float u = std::fabs((static_cast<float>(index) + 0.5f) - static_cast<float>(kGrid) * 0.5f) / (static_cast<float>(kGrid) * 0.5f);
        const float flatUntil = 1.0f - kWindowTaper;
        return u <= flatUntil ? 1.0f : 0.5f + 0.5f * std::cos(kPi * (u - flatUntil) / kWindowTaper);
    };
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            window_[y * kGrid + x] = taper(x) * taper(y);
        }
    }
    for (int v = 0; v < kGrid; ++v) {
        for (int u = 0; u < kGrid; ++u) {
            const float fu = static_cast<float>(signedOffset(u));
            const float fv = static_cast<float>(signedOffset(v));
            lowPass_[v * kGrid + u] = std::exp(-(fu * fu + fv * fv) / (2.0f * kLowPassSigma * kLowPassSigma));
        }
    }
}

void GlobalMotionEstimator::reset() {
    hasPrevious_ = false;
    previousSide_ = 0;
    staticScore_.fill(0);
}

void GlobalMotionEstimator::reduceToGrey(const uint8_t* rgba, int side, int strideBytes) {
    // Each grid pixel is the average of a few samples spread over its block of the frame.
    constexpr int kSamplesPerAxis = 3;
    const float block = static_cast<float>(side) / static_cast<float>(kGrid);
    double total = 0.0;
    for (int gy = 0; gy < kGrid; ++gy) {
        const float top = static_cast<float>(gy) * block;
        for (int gx = 0; gx < kGrid; ++gx) {
            const float left = static_cast<float>(gx) * block;
            float sum = 0.0f;
            for (int sy = 0; sy < kSamplesPerAxis; ++sy) {
                const int y = std::min(side - 1, static_cast<int>(top + (static_cast<float>(sy) + 0.5f) * block / kSamplesPerAxis));
                const uint8_t* row = rgba + static_cast<size_t>(y) * static_cast<size_t>(strideBytes);
                for (int sx = 0; sx < kSamplesPerAxis; ++sx) {
                    const int x = std::min(side - 1, static_cast<int>(left + (static_cast<float>(sx) + 0.5f) * block / kSamplesPerAxis));
                    const uint8_t* pixel = row + x * 4;
                    sum += static_cast<float>(77 * pixel[0] + 150 * pixel[1] + 29 * pixel[2]) * (1.0f / 256.0f);
                }
            }
            const float value = sum / static_cast<float>(kSamplesPerAxis * kSamplesPerAxis);
            grey_[gy * kGrid + gx] = value;
            total += value;
        }
    }
    // Only changes of brightness carry the motion, so the average is taken out.
    const float mean = static_cast<float>(total / kCells);
    for (float& value : grey_) {
        value -= mean;
    }
}

void GlobalMotionEstimator::fft1d(Complex* data, int stride, bool inverse) const {
    for (int i = 0; i < kGrid; ++i) {
        const int j = bitReversed_[i];
        if (j > i) {
            std::swap(data[i * stride], data[j * stride]);
        }
    }
    for (int length = 2; length <= kGrid; length <<= 1) {
        const int half = length >> 1;
        const int step = kGrid / length;
        for (int start = 0; start < kGrid; start += length) {
            for (int k = 0; k < half; ++k) {
                const float c = cosine_[k * step];
                const float s = inverse ? sine_[k * step] : -sine_[k * step];
                Complex& a = data[(start + k) * stride];
                Complex& b = data[(start + k + half) * stride];
                const float tr = b.re * c - b.im * s;
                const float ti = b.re * s + b.im * c;
                b.re = a.re - tr;
                b.im = a.im - ti;
                a.re += tr;
                a.im += ti;
            }
        }
    }
}

void GlobalMotionEstimator::transform(Grid& grid, bool inverse) const {
    for (int row = 0; row < kGrid; ++row) {
        fft1d(&grid[row * kGrid], 1, inverse);
    }
    for (int column = 0; column < kGrid; ++column) {
        fft1d(&grid[column], kGrid, inverse);
    }
    if (inverse) {
        const float scale = 1.0f / static_cast<float>(kCells);
        for (Complex& value : grid) {
            value.re *= scale;
            value.im *= scale;
        }
    }
}

void GlobalMotionEstimator::learnStaticPixels() {
    for (int y = 1; y < kGrid - 1; ++y) {
        for (int x = 1; x < kGrid - 1; ++x) {
            const int i = y * kGrid + x;
            const float gradient = 0.5f * (std::fabs(grey_[i + 1] - grey_[i - 1]) + std::fabs(grey_[i + kGrid] - grey_[i - kGrid]));
            if (gradient < kStaticMinGradient) {
                continue;
            }
            const int score = staticScore_[i];
            staticScore_[i] = static_cast<uint8_t>(std::fabs(grey_[i] - previousGrey_[i]) < kStaticMaxDifference
                                                       ? std::min(255, score + kStaticGain)
                                                       : std::max(0, score - kStaticLoss));
        }
    }
}

int GlobalMotionEstimator::findCandidates(Candidate* candidates) {
    // Both frames go through the same window and the same left-out pixels, so only the scene's
    // own motion is seen between them.
    for (int i = 0; i < kCells; ++i) {
        current_[i] = {grey_[i] * layerWeight_[i], 0.0f};
        previous_[i] = {previousGrey_[i] * layerWeight_[i], 0.0f};
    }
    transform(current_, false);
    transform(previous_, false);

    // Cross-power spectrum, keeping only the phase: its inverse transform is a spike at the shift.
    Grid& surface = current_;
    double powerSum = 0.0;
    for (int i = 0; i < kCells; ++i) {
        const Complex a = current_[i];
        const Complex& b = previous_[i];
        surface[i].re = a.re * b.re + a.im * b.im;
        surface[i].im = a.im * b.re - a.re * b.im;
        powerSum += std::hypot(surface[i].re, surface[i].im);
    }
    const float floorPower = 0.01f * static_cast<float>(powerSum / kCells) + 1e-6f;
    for (int i = 0; i < kCells; ++i) {
        const float magnitude = std::hypot(surface[i].re, surface[i].im) + floorPower;
        surface[i].re = surface[i].re / magnitude * lowPass_[i];
        surface[i].im = surface[i].im / magnitude * lowPass_[i];
    }
    transform(surface, true);

    // The strongest few peaks of the correlation surface, each apart from the ones before it.
    struct Peak {
        int x = 0;
        int y = 0;
        float height = -1.0f;
    };
    const auto nearPeak = [](const Peak& peak, int x, int y) {
        return peak.height >= 0.0f && std::abs(signedOffset((x - peak.x + kGrid) % kGrid)) < kPeakSeparation &&
               std::abs(signedOffset((y - peak.y + kGrid) % kGrid)) < kPeakSeparation;
    };
    std::array<Peak, kMaxCandidates> peaks{};
    int count = 0;
    for (int found = 0; found < kMaxCandidates; ++found) {
        for (int y = 0; y < kGrid; ++y) {
            for (int x = 0; x < kGrid; ++x) {
                bool taken = false;
                for (int other = 0; other < found; ++other) {
                    taken = taken || nearPeak(peaks[other], x, y);
                }
                const float value = surface[y * kGrid + x].re;
                if (!taken && value > peaks[found].height) {
                    peaks[found] = {x, y, value};
                }
            }
        }
        if (peaks[found].height <= 0.0f || (found > 0 && peaks[found].height < kRivalPeakShare * peaks[0].height)) {
            break;   // the rest are too weak to be the scene's motion
        }
        ++count;
    }

    const auto valueAt = [&surface](int x, int y) { return surface[((y + kGrid) % kGrid) * kGrid + ((x + kGrid) % kGrid)].re; };
    const auto vertex = [](float before, float at, float after) {
        const float curvature = before - 2.0f * at + after;
        return curvature < -1e-9f ? std::clamp(0.5f * (before - after) / curvature, -0.5f, 0.5f) : 0.0f;
    };
    for (int i = 0; i < count; ++i) {
        const Peak& peak = peaks[i];

        // Between grid pixels: the top of the parabola through the peak and its two neighbours, per axis.
        candidates[i].x = static_cast<float>(signedOffset(peak.x)) + vertex(valueAt(peak.x - 1, peak.y), peak.height, valueAt(peak.x + 1, peak.y));
        candidates[i].y = static_cast<float>(signedOffset(peak.y)) + vertex(valueAt(peak.x, peak.y - 1), peak.height, valueAt(peak.x, peak.y + 1));
    }
    return count;
}

float GlobalMotionEstimator::refineShift(float* shiftX, float* shiftY, int steps, float frameTextured) const {
    // Gauss-Newton on the sum of squared differences between the current frame and the previous one
    // moved by the shift, over the pixels that count. A large difference (something moving by
    // itself) counts for less, and beyond a limit for nothing. With no steps it only measures how
    // well the shift fits.
    float explained = 0.0f;
    for (int step = 0; step <= steps; ++step) {
        double jxx = 0.0;
        double jxy = 0.0;
        double jyy = 0.0;
        double jxe = 0.0;
        double jye = 0.0;
        double textured = 0.0;
        double matched = 0.0;
        double weights = 0.0;
        for (int y = 1; y < kGrid - 1; ++y) {
            for (int x = 1; x < kGrid - 1; ++x) {
                const int i = y * kGrid + x;
                if (weight_[i] <= 0.0f) {
                    continue;
                }
                const float px = static_cast<float>(x) - *shiftX;
                const float py = static_cast<float>(y) - *shiftY;
                float centre, right, left, down, up;
                if (!sampleAt(previousGrey_, px, py, &centre) || !sampleAt(previousGrey_, px + 1.0f, py, &right) ||
                    !sampleAt(previousGrey_, px - 1.0f, py, &left) || !sampleAt(previousGrey_, px, py + 1.0f, &down) ||
                    !sampleAt(previousGrey_, px, py - 1.0f, &up)) {
                    continue;
                }
                const float gx = 0.5f * (right - left);
                const float gy = 0.5f * (down - up);
                const float difference = grey_[i] - centre;
                const float ratio = difference / kRejectLevel;
                const float robust = ratio * ratio < 1.0f ? (1.0f - ratio * ratio) * (1.0f - ratio * ratio) : 0.0f;
                const double w = static_cast<double>(weight_[i]) * robust;
                jxx += w * gx * gx;
                jxy += w * gx * gy;
                jyy += w * gy * gy;
                jxe += w * gx * difference;
                jye += w * gy * difference;
                if (std::hypot(gx, gy) >= kTextureMinGradient) {
                    textured += weight_[i];
                    matched += static_cast<double>(weight_[i]) * robust;
                }
                weights += weight_[i];
            }
        }
        if (weights < 0.1 * kCells) {
            return 0.0f;   // too little of the picture overlaps to say
        }
        // How well the compared pixels agree, trusted less when the picture as a whole has few edges.
        // The amount of texture that counts is the picture's, not the part this shift overlaps: a
        // large slide leaves less to compare, and that must not make the right shift look weak.
        const float evidence = std::min(1.0f, frameTextured / (kFullTexturedShare * static_cast<float>(kCells)));
        const bool enough = frameTextured > kMinTexturedShare * kCells && textured >= kMinOverlapShare * frameTextured &&
                            matched >= kMinMatchedWeight;
        explained = enough ? static_cast<float>(matched / textured * std::sqrt(evidence)) : 0.0f;
        if (step == steps) {
            break;
        }
        const double determinant = jxx * jyy - jxy * jxy;
        if (determinant < 1e-6 * (jxx + jyy) * (jxx + jyy) || determinant < 1e-9) {
            break;   // no texture in one direction: keep the shift as it is
        }
        // The change that makes the difference go away: -(J^T J)^-1 J^T e, with e = current - previous(x - s).
        const float stepX = static_cast<float>(-(jyy * jxe - jxy * jye) / determinant);
        const float stepY = static_cast<float>(-(jxx * jye - jxy * jxe) / determinant);
        *shiftX += std::clamp(stepX, -kMaxRefineStep, kMaxRefineStep);
        *shiftY += std::clamp(stepY, -kMaxRefineStep, kMaxRefineStep);
        if (std::fabs(stepX) < 0.01f && std::fabs(stepY) < 0.01f) {
            steps = step + 1;   // converged: one more pass reports how well it fits
        }
    }
    return explained;
}

/// How much textured picture there is to compare: the weight of the pixels of the current frame that have an edge.
float GlobalMotionEstimator::measureFrameTexture() const {
    double textured = 0.0;
    for (int y = 1; y < kGrid - 1; ++y) {
        for (int x = 1; x < kGrid - 1; ++x) {
            const int i = y * kGrid + x;
            const float gx = 0.5f * (grey_[i + 1] - grey_[i - 1]);
            const float gy = 0.5f * (grey_[i + kGrid] - grey_[i - kGrid]);
            if (std::hypot(gx, gy) >= kTextureMinGradient) {
                textured += weight_[i];
            }
        }
    }
    return static_cast<float>(textured);
}

void GlobalMotionEstimator::keepUnexplainedPixels(float shiftX, float shiftY) {
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            const int i = y * kGrid + x;
            float centre = 0.0f;
            const bool inside = sampleAt(previousGrey_, static_cast<float>(x) - shiftX, static_cast<float>(y) - shiftY, &centre);
            const bool explained = inside && std::fabs(grey_[i] - centre) < kExplainedDifference;
            layerWeight_[i] = weight_[i] * (explained ? kExplainedWeightKept : 1.0f);
        }
    }
}

GlobalShift GlobalMotionEstimator::push(const uint8_t* rgba, int side, int strideBytes) {
    if (rgba == nullptr || side < kGrid / 2 || strideBytes < side * 4) {
        return GlobalShift();
    }
    reduceToGrey(rgba, side, strideBytes);
    return compareWithPrevious(side);
}

GlobalShift GlobalMotionEstimator::pushGrey(const float* grey) {
    if (grey == nullptr) {
        return GlobalShift();
    }
    std::copy(grey, grey + kCells, grey_.begin());
    return compareWithPrevious(kGrid);
}

/// Measures how far the picture in `grey_` slid compared with `previousGrey_`, then keeps it as the next frame's reference.
GlobalShift GlobalMotionEstimator::compareWithPrevious(int side) {
    GlobalShift result;
    const bool comparable = hasPrevious_ && side == previousSide_;
    int changedPixels = 0;
    if (comparable) {
        for (int i = 0; i < kCells; ++i) {
            changedPixels += std::fabs(grey_[i] - previousGrey_[i]) > kChangedLevel ? 1 : 0;
        }
    }
    // Whatever happens next, this frame is the one the next frame is compared with.
    struct Remember {
        GlobalMotionEstimator* self;
        int side;
        ~Remember() {
            std::swap(self->grey_, self->previousGrey_);
            self->previousSide_ = side;
            self->hasPrevious_ = true;
        }
    } remember{this, side};
    if (!comparable) {
        return result;
    }
    if (static_cast<float>(changedPixels) < kUnchangedShare * kCells) {
        result.unchanged = true;
        result.quality = 1.0f;
        return result;
    }
    // When the picture as a whole has clearly moved, whatever stayed the same is not part of the scene.
    if (static_cast<float>(changedPixels) > kMotionShare * kCells) {
        learnStaticPixels();
    }

    // Every pixel counts by its place in the window, less so the more it has proved not to move with the scene.
    for (int i = 0; i < kCells; ++i) {
        const float left = static_cast<float>(staticScore_[i]) / static_cast<float>(kStaticIgnored);
        weight_[i] = window_[i] * std::clamp(1.0f - left, 0.0f, 1.0f);
        layerWeight_[i] = weight_[i];
    }

    // The scene covers most of the picture, so the shift that explains the picture best is the
    // scene's motion; something moving by itself, or the parts that stay put, explain far less.
    // When even the best explains too little, the pixels it did not explain are looked at again by themselves.
    const float block = static_cast<float>(side) / static_cast<float>(kGrid);
    const float frameTextured = measureFrameTexture();
    bool found = false;
    float bestX = 0.0f;
    float bestY = 0.0f;
    float bestFit = 0.0f;
    std::array<Candidate, kMaxCandidates * (kExtraLayers + 1)> tried{};
    std::array<float, kMaxCandidates * (kExtraLayers + 1)> triedFit{};
    int triedCount = 0;
    for (int layer = 0; layer <= kExtraLayers; ++layer) {
        Candidate candidates[kMaxCandidates];
        const int count = findCandidates(candidates);
        for (int i = 0; i < count; ++i) {
            float x = candidates[i].x;
            float y = candidates[i].y;
            const float fit = refineShift(&x, &y, kRefineSteps, frameTextured);
            tried[triedCount] = {x, y};
            triedFit[triedCount++] = fit;
            if (fit > bestFit) {
                bestX = x;
                bestY = y;
                bestFit = fit;
                found = true;
            }
            if (bestFit >= kGoodAgreement) {
                break;   // the strongest peak that explains the picture will do: no need to try the weaker ones
            }
        }
        if (!found || bestFit >= kGoodAgreement || layer == kExtraLayers) {
            break;
        }
        keepUnexplainedPixels(bestX, bestY);
    }
    if (!found) {
        return result;
    }

    // A different shift that fits almost as well means the picture does not say which is right.
    float rivalFit = 0.0f;
    for (int i = 0; i < triedCount; ++i) {
        if (std::hypot(tried[i].x - bestX, tried[i].y - bestY) > kDistinctShift) {
            rivalFit = std::max(rivalFit, triedFit[i]);
        }
    }
    const float doubt = std::clamp((rivalFit - (bestFit - kAmbiguityRange)) / kAmbiguityRange, 0.0f, 1.0f);

    result.dx = bestX * block;
    result.dy = bestY * block;
    const float fit = std::clamp((bestFit - kMinAgreement) / (kFullAgreement - kMinAgreement), 0.0f, 1.0f);
    result.quality = fit * (1.0f - (1.0f - kAmbiguousKeep) * doubt);
    return result;
}

}  // namespace aimbuddy
