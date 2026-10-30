#include "detection_decoder.h"

#include <algorithm>
#include <cmath>

namespace aimbuddy {

namespace {

/// Candidates kept for merging; a frame never has more than a handful of real targets.
constexpr int kCandidateLimit = 128;

/// A coordinate this close to the model border counts as touching it.
constexpr float kClipMarginPx = 1.5f;

/// Boxes narrower or shorter than this are noise.
constexpr float kMinBoxSidePx = 2.0f;

/// Coordinates at or below this, over the whole tensor, mean the model outputs fractions.
constexpr float kNormalizedCoordinateLimit = 2.0f;

/// Reads element (row, col) of a row-major tensor.
float at(const float* data, int cols, int row, int col) { return data[row * cols + col]; }

/// Largest absolute box coordinate in the tensor, used to tell fractions from pixels.
float largestCoordinate(const float* data, int rows, int cols, OutputLayout layout) {
    float largest = 0.0f;
    if (layout == OutputLayout::kChannelsFirst) {
        for (int c = 0; c < 4; ++c) {
            for (int i = 0; i < cols; ++i) {
                largest = std::max(largest, std::fabs(at(data, cols, c, i)));
            }
        }
    } else {
        for (int i = 0; i < rows; ++i) {
            for (int c = 0; c < 4; ++c) {
                largest = std::max(largest, std::fabs(at(data, cols, i, c)));
            }
        }
    }
    return largest;
}

/// Flags the sides of a corner-form box that lie on or beyond the model input border.
uint8_t clipFlagsFor(float left, float top, float right, float bottom, float size) {
    uint8_t flags = kClipNone;
    if (left <= kClipMarginPx) flags |= kClipLeft;
    if (right >= size - kClipMarginPx) flags |= kClipRight;
    if (top <= kClipMarginPx) flags |= kClipTop;
    if (bottom >= size - kClipMarginPx) flags |= kClipBottom;
    return flags;
}

/// Overlap of two corner-form boxes as intersection over union.
float cornerIou(const ModelBox& a, const ModelBox& b) {
    const float w = std::min(a.right, b.right) - std::max(a.left, b.left);
    const float h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
    if (w <= 0.0f || h <= 0.0f) {
        return 0.0f;
    }
    const float intersection = w * h;
    const float unionArea = (a.right - a.left) * (a.bottom - a.top) + (b.right - b.left) * (b.bottom - b.top) - intersection;
    return unionArea > 0.0f ? intersection / unionArea : 0.0f;
}

/**
 * Collects candidate boxes. The best `kCandidateLimit` are kept, so a flood of weak
 * candidates cannot cost time or hide a strong one.
 */
class CandidateList {
public:
    /// Adds a candidate, replacing the weakest one when the list is full.
    void add(const ModelBox& box) {
        if (count_ < kCandidateLimit) {
            boxes_[count_++] = box;
            return;
        }
        int weakest = 0;
        for (int i = 1; i < kCandidateLimit; ++i) {
            if (boxes_[i].score < boxes_[weakest].score) {
                weakest = i;
            }
        }
        if (box.score > boxes_[weakest].score) {
            boxes_[weakest] = box;
        }
    }

    ModelBox* begin() { return boxes_; }
    ModelBox* end() { return boxes_ + count_; }
    int size() const { return count_; }

private:
    ModelBox boxes_[kCandidateLimit];
    int count_ = 0;
};

/// Converts a centre-form box (cx, cy, w, h) to a candidate, or returns false if it is unusable.
bool makeCandidate(float cx, float cy, float w, float h, float score, int classId, float scale,
                   float modelSize, ModelBox* box) {
    const float halfW = w * scale * 0.5f;
    const float halfH = h * scale * 0.5f;
    float left = cx * scale - halfW;
    float top = cy * scale - halfH;
    float right = cx * scale + halfW;
    float bottom = cy * scale + halfH;
    if (!(std::isfinite(left) && std::isfinite(top) && std::isfinite(right) && std::isfinite(bottom))) {
        return false;
    }
    box->clipFlags = clipFlagsFor(left, top, right, bottom, modelSize);
    box->left = std::max(0.0f, left);
    box->top = std::max(0.0f, top);
    box->right = std::min(modelSize, right);
    box->bottom = std::min(modelSize, bottom);
    box->score = score;
    box->classId = classId;
    return box->right - box->left >= kMinBoxSidePx && box->bottom - box->top >= kMinBoxSidePx;
}

/// Reads rows of [x1, y1, x2, y2, score, class] that the model already filtered. Boxes of ignored classes go to
/// `ignored`: they are never reported, but a surer one takes out a body box on the same figure (see decodeDetections).
void collectEndToEnd(const float* data, int rows, int cols, const DecodeSpec& spec,
                     const DecodeParams& params, const ModelProfile& profile, CandidateList& list,
                     CandidateList& ignored) {
    const float scale = spec.normalized ? static_cast<float>(params.modelSize) : 1.0f;
    const float size = static_cast<float>(params.modelSize);
    for (int i = 0; i < rows; ++i) {
        const float* row = data + i * cols;
        const float score = row[4];
        const int classId = static_cast<int>(row[5] + 0.5f);
        if (!(score >= params.minScore)) {
            continue;
        }
        ModelBox box;
        const float w = row[2] - row[0];
        const float h = row[3] - row[1];
        if (makeCandidate((row[0] + row[2]) * 0.5f, (row[1] + row[3]) * 0.5f, w, h, score, classId, scale, size, &box)) {
            (profile.roleOf(classId) == ClassRole::kIgnore ? ignored : list).add(box);
        }
    }
}

/// Reads the raw export: every anchor carries a centre-form box and one score per class. Anchors whose best class is
/// ignored go to `ignored`, as for collectEndToEnd.
void collectAnchors(const float* data, int rows, int cols, const DecodeSpec& spec,
                    const DecodeParams& params, const ModelProfile& profile, CandidateList& list,
                    CandidateList& ignored) {
    const bool channelsFirst = spec.layout == OutputLayout::kChannelsFirst;
    const int anchors = channelsFirst ? cols : rows;
    const float scale = spec.normalized ? static_cast<float>(params.modelSize) : 1.0f;
    const float size = static_cast<float>(params.modelSize);

    for (int i = 0; i < anchors; ++i) {
        // Best class of this anchor.
        float bestScore = 0.0f;
        int bestClass = 0;
        for (int c = 0; c < spec.classCount; ++c) {
            const float score = channelsFirst ? at(data, cols, 4 + c, i) : at(data, cols, i, 4 + c);
            if (score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }
        if (bestScore < params.minScore) {
            continue;
        }
        float cx, cy, w, h;
        if (channelsFirst) {
            cx = at(data, cols, 0, i);
            cy = at(data, cols, 1, i);
            w = at(data, cols, 2, i);
            h = at(data, cols, 3, i);
        } else {
            cx = at(data, cols, i, 0);
            cy = at(data, cols, i, 1);
            w = at(data, cols, i, 2);
            h = at(data, cols, i, 3);
        }
        ModelBox box;
        if (makeCandidate(cx, cy, w, h, bestScore, bestClass, scale, size, &box)) {
            (profile.roleOf(bestClass) == ClassRole::kIgnore ? ignored : list).add(box);
        }
    }
}

/// True when a box of an ignored class, surer than this body box, lies on the same figure: the model took the figure
/// for, say, the player's own character more than for an enemy, and the weaker reading must not make it a target.
bool coveredByIgnored(const ModelBox& body, CandidateList& ignored, float overlap) {
    for (const ModelBox& other : ignored) {
        if (other.score > body.score && cornerIou(other, body) > overlap) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool inferDecodeSpec(const float* data, int rows, int cols, bool endToEnd, DecodeSpec* spec) {
    if (data == nullptr || spec == nullptr || rows <= 0 || cols <= 0) {
        return false;
    }
    if (endToEnd) {
        if (cols < 6) {
            return false;
        }
        spec->layout = OutputLayout::kEndToEnd;
        spec->classCount = 0;  // unknown from the shape; the profile decides which classes matter
    } else if (rows < cols) {
        if (rows < 5) {
            return false;
        }
        spec->layout = OutputLayout::kChannelsFirst;
        spec->classCount = rows - 4;
    } else {
        if (cols < 5) {
            return false;
        }
        spec->layout = OutputLayout::kRowsFirst;
        spec->classCount = cols - 4;
    }
    spec->normalized = largestCoordinate(data, rows, cols, spec->layout) <= kNormalizedCoordinateLimit;
    return true;
}

int inferInputSize(int anchors) {
    constexpr int kSmallestInput = 64;
    constexpr int kLargestInput = 1280;
    constexpr int kStride = 32;
    for (int size = kSmallestInput; size <= kLargestInput; size += kStride) {
        const int expected = (size / 8) * (size / 8) + (size / 16) * (size / 16) + (size / 32) * (size / 32);
        if (expected == anchors) {
            return size;
        }
    }
    return 0;
}

int decodeDetections(const float* data, int rows, int cols, const DecodeSpec& spec,
                     const DecodeParams& params, const ModelProfile& profile,
                     ModelBox* out, int capacity) {
    if (data == nullptr || out == nullptr || capacity <= 0) {
        return 0;
    }

    CandidateList candidates;
    CandidateList ignored;
    if (spec.layout == OutputLayout::kEndToEnd) {
        collectEndToEnd(data, rows, cols, spec, params, profile, candidates, ignored);
    } else {
        collectAnchors(data, rows, cols, spec, params, profile, candidates, ignored);
    }

    // Strongest first, then keep a box unless a stronger box of the same class covers it, or, for a body, a stronger
    // box of an ignored class: neighbouring anchors on the player's own character can disagree on its class, and
    // merging class by class kept the weaker body box among them.
    std::sort(candidates.begin(), candidates.end(),
              [](const ModelBox& a, const ModelBox& b) { return a.score > b.score; });
    int written = 0;
    for (const ModelBox& candidate : candidates) {
        if (written >= capacity) {
            break;
        }
        bool duplicate = false;
        if (spec.layout != OutputLayout::kEndToEnd) {
            for (int k = 0; k < written && !duplicate; ++k) {
                duplicate = out[k].classId == candidate.classId && cornerIou(out[k], candidate) > params.nmsIou;
            }
        }
        if (!duplicate && profile.roleOf(candidate.classId) == ClassRole::kBody) {
            duplicate = coveredByIgnored(candidate, ignored, params.nmsIou);
        }
        if (!duplicate) {
            out[written++] = candidate;
        }
    }
    return written;
}

Detection mapToScreen(const ModelBox& box, const RoiRect& roi, const CaptureGeometry& geometry,
                      int modelSize) {
    const float regionPerModel = static_cast<float>(roi.side) / static_cast<float>(modelSize);
    const float screenPerCapture = geometry.screenPerCapture();
    const float scale = regionPerModel * screenPerCapture;
    const float offsetX = static_cast<float>(roi.x) * screenPerCapture;
    const float offsetY = static_cast<float>(roi.y) * screenPerCapture;

    Detection detection;
    detection.x = box.left * scale + offsetX;
    detection.y = box.top * scale + offsetY;
    detection.width = (box.right - box.left) * scale;
    detection.height = (box.bottom - box.top) * scale;
    detection.score = box.score;
    detection.classId = box.classId;
    detection.clipFlags = box.clipFlags;
    return detection;
}

}  // namespace aimbuddy
