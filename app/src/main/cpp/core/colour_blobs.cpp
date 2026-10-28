#include "colour_blobs.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aimbuddy {

namespace {

constexpr float kPi = 3.14159265358979f;

/// The most pieces kept while labelling, so a noisy picture cannot make the joining step slow.
constexpr int kMaxPieces = 48;

/// A piece with fewer pixels than this is dropped before the pieces are joined.
constexpr int kMinPiecePixels = 6;

/// Gaps in the mask up to this share of the picture's side (each way) are closed, but never less than one pixel.
constexpr float kClosingShareOfSide = 1.0f / 128.0f;

/// Two pieces are joined when they come within this share of the taller one's height of each other, sideways and
/// up or down. Up and down is the more generous, because a head sits above a body and two people stand side by side.
constexpr float kJoinGapSideways = 0.08f;
constexpr float kJoinGapVertical = 0.20f;
constexpr int kMinJoinGapPx = 3;

/// For learning: a pixel is vivid enough to be looked at when it is at least this saturated and bright, the hues are
/// counted in bins of this many degrees, and the hue is taken from the best bin and its two neighbours.
constexpr float kVividSaturation = 0.35f;
constexpr float kVividValue = 0.35f;
constexpr int kHueBins = 36;
constexpr int kMinVividPixels = 10;

/// What is learned is looser than what was seen: a pixel counts down to this share of the typical saturation and brightness.
constexpr float kLearnedShare = 0.6f;
constexpr float kLearnedFloor = 0.25f;
constexpr float kLearnedCeiling = 0.85f;

/// Hue, saturation and value of a colour whose channels are 0 to 1. The hue is in degrees, 0 to 360.
void rgbToHsv(float red, float green, float blue, float* hue, float* saturation, float* value) {
    const float highest = std::max(red, std::max(green, blue));
    const float lowest = std::min(red, std::min(green, blue));
    const float spread = highest - lowest;
    *value = highest;
    *saturation = highest > 0.0f ? spread / highest : 0.0f;
    if (spread <= 0.0f) {
        *hue = 0.0f;
        return;
    }
    float sector;
    if (highest == red) {
        sector = (green - blue) / spread;
    } else if (highest == green) {
        sector = (blue - red) / spread + 2.0f;
    } else {
        sector = (red - green) / spread + 4.0f;
    }
    float degrees = sector * 60.0f;
    if (degrees < 0.0f) {
        degrees += 360.0f;
    }
    *hue = degrees;
}

/// How far apart two hues are, going the short way round the colour wheel: 0 to 180 degrees.
float hueDistance(float a, float b) {
    const float apart = std::fabs(a - b);
    return std::min(apart, 360.0f - apart);
}

/// How much a pixel counts when a colour is learned: the cube of how vivid and bright it is, so that a thin,
/// pure outline outweighs a large area of moderate colour (wood, skin, sand) behind it.
float vividness(float saturation, float value) {
    const float product = saturation * value;
    return product * product * product;
}

/// The middle of a set of (value, weight) pairs when each value is counted as many times as its weight says.
float weightedMedian(std::vector<std::pair<float, float>>* samples) {
    std::sort(samples->begin(), samples->end());
    float total = 0.0f;
    for (const auto& sample : *samples) {
        total += sample.second;
    }
    float running = 0.0f;
    for (const auto& sample : *samples) {
        running += sample.second;
        if (running >= 0.5f * total) {
            return sample.first;
        }
    }
    return samples->back().first;
}

/// A colour of 5, 6 and 5 bits as the 8 bit channels it stands for.
void unpack565(int index, float* red, float* green, float* blue) {
    const int r5 = index >> 11;
    const int g6 = (index >> 5) & 63;
    const int b5 = index & 31;
    *red = static_cast<float>((r5 << 3) | (r5 >> 2)) / 255.0f;
    *green = static_cast<float>((g6 << 2) | (g6 >> 4)) / 255.0f;
    *blue = static_cast<float>((b5 << 3) | (b5 >> 2)) / 255.0f;
}

/// A place in the picture as one number, for the stack of places still to be looked at (pictures are far smaller than 65536 pixels a side).
int32_t packPlace(int x, int y) { return static_cast<int32_t>((y << 16) | x); }
int placeX(int32_t place) { return place & 0xFFFF; }
int placeY(int32_t place) { return place >> 16; }

int tableIndexOf(const uint8_t* pixel) {
    return ((pixel[0] >> 3) << 11) | ((pixel[1] >> 2) << 5) | (pixel[2] >> 3);
}

}  // namespace

ColourBlobFinder::ColourBlobFinder() { stack_.reserve(4096); }

/// Decides, for every colour the table can be asked about, whether it is the colour looked for.
void ColourBlobFinder::fillTable(const ColourRange& range) {
    for (int index = 0; index < kTableSize; ++index) {
        float red, green, blue, hue, saturation, value;
        unpack565(index, &red, &green, &blue);
        rgbToHsv(red, green, blue, &hue, &saturation, &value);
        const bool matches = saturation >= range.minSaturation && value >= range.minValue &&
                             hueDistance(hue, range.hueDegrees) <= range.hueToleranceDegrees;
        table_[index] = matches ? 1 : 0;
    }
    tableRange_ = range;
    tableFilled_ = true;
}

/// Turns the picture into a mask: 1 where the pixel has the colour, 0 elsewhere.
void ColourBlobFinder::classify(const uint8_t* rgba, int side, int strideBytes) {
    mask_.resize(static_cast<size_t>(side) * static_cast<size_t>(side));
    for (int y = 0; y < side; ++y) {
        const uint8_t* pixel = rgba + static_cast<size_t>(y) * static_cast<size_t>(strideBytes);
        uint8_t* out = &mask_[static_cast<size_t>(y) * static_cast<size_t>(side)];
        for (int x = 0; x < side; ++x, pixel += 4) {
            out[x] = table_[tableIndexOf(pixel)];
        }
    }
}

/**
 * Grows (wantSet) or shrinks the mask along every row or every column: a pixel becomes set when any
 * pixel within `radius` of it is set, or stays set only when every pixel within `radius` of it is. What lies
 * beyond the picture's edge counts as set when shrinking, so the mask does not erode away from the border.
 */
void ColourBlobFinder::slideWindow(int side, int radius, bool horizontal, bool wantSet) {
    scratch_.resize(mask_.size());
    const size_t step = horizontal ? 1 : static_cast<size_t>(side);
    const size_t lineStep = horizontal ? static_cast<size_t>(side) : 1;
    const uint8_t counted = wantSet ? 1 : 0;   // growing counts set pixels, shrinking counts unset ones
    for (int line = 0; line < side; ++line) {
        const uint8_t* in = mask_.data() + static_cast<size_t>(line) * lineStep;
        uint8_t* out = scratch_.data() + static_cast<size_t>(line) * lineStep;
        int inWindow = 0;
        for (int i = 0; i <= std::min(radius, side - 1); ++i) {
            inWindow += in[static_cast<size_t>(i) * step] == counted ? 1 : 0;
        }
        for (int i = 0; i < side; ++i) {
            out[static_cast<size_t>(i) * step] = (wantSet ? inWindow > 0 : inWindow == 0) ? 1 : 0;
            const int entering = i + radius + 1;
            const int leaving = i - radius;
            if (entering < side) {
                inWindow += in[static_cast<size_t>(entering) * step] == counted ? 1 : 0;
            }
            if (leaving >= 0) {
                inWindow -= in[static_cast<size_t>(leaving) * step] == counted ? 1 : 0;
            }
        }
    }
    mask_.swap(scratch_);
}

/// Closes gaps of up to twice the radius: grows the mask, then shrinks it back by as much.
void ColourBlobFinder::closeGaps(int side, int radius) {
    slideWindow(side, radius, true, true);
    slideWindow(side, radius, false, true);
    slideWindow(side, radius, true, false);
    slideWindow(side, radius, false, false);
}

/// Labels the connected pieces of the mask and writes their boxes into `pieces`: the biggest `capacity` of them.
int ColourBlobFinder::labelPatches(int side, ColourBlob* pieces, int capacity) {
    int count = 0;
    for (int startY = 0; startY < side; ++startY) {
        for (int startX = 0; startX < side; ++startX) {
            const size_t startIndex = static_cast<size_t>(startY) * static_cast<size_t>(side) + static_cast<size_t>(startX);
            if (mask_[startIndex] != 1) {
                continue;
            }
            ColourBlob piece;
            piece.left = startX;
            piece.top = startY;
            piece.right = startX + 1;
            piece.bottom = startY + 1;
            stack_.clear();
            stack_.push_back(packPlace(startX, startY));
            mask_[startIndex] = 2;   // 2: seen
            while (!stack_.empty()) {
                const int32_t place = stack_.back();
                stack_.pop_back();
                const int x = placeX(place);
                const int y = placeY(place);
                ++piece.pixels;
                piece.left = std::min(piece.left, x);
                piece.top = std::min(piece.top, y);
                piece.right = std::max(piece.right, x + 1);
                piece.bottom = std::max(piece.bottom, y + 1);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= side || ny >= side) {
                            continue;
                        }
                        const size_t next = static_cast<size_t>(ny) * static_cast<size_t>(side) + static_cast<size_t>(nx);
                        if (mask_[next] == 1) {
                            mask_[next] = 2;
                            stack_.push_back(packPlace(nx, ny));
                        }
                    }
                }
            }
            if (piece.pixels < kMinPiecePixels) {
                continue;
            }
            if (count < capacity) {
                pieces[count++] = piece;
                continue;
            }
            // Full: this piece takes the place of the smallest one kept, if it is bigger.
            int smallest = 0;
            for (int i = 1; i < count; ++i) {
                if (pieces[i].pixels < pieces[smallest].pixels) {
                    smallest = i;
                }
            }
            if (piece.pixels > pieces[smallest].pixels) {
                pieces[smallest] = piece;
            }
        }
    }
    return count;
}

/// Joins pieces that touch or lie close together into one, until no two do. Returns how many pieces are left.
int ColourBlobFinder::joinNearbyPatches(ColourBlob* pieces, int count) {
    bool joined = true;
    while (joined) {
        joined = false;
        for (int i = 0; i < count && !joined; ++i) {
            for (int j = i + 1; j < count && !joined; ++j) {
                const int taller = std::max(pieces[i].height(), pieces[j].height());
                const int sideways = std::max(kMinJoinGapPx, static_cast<int>(kJoinGapSideways * static_cast<float>(taller)));
                const int vertical = std::max(kMinJoinGapPx, static_cast<int>(kJoinGapVertical * static_cast<float>(taller)));
                const bool apartSideways = pieces[i].left - sideways > pieces[j].right || pieces[j].left - sideways > pieces[i].right;
                const bool apartVertically = pieces[i].top - vertical > pieces[j].bottom || pieces[j].top - vertical > pieces[i].bottom;
                if (apartSideways || apartVertically) {
                    continue;
                }
                pieces[i].left = std::min(pieces[i].left, pieces[j].left);
                pieces[i].top = std::min(pieces[i].top, pieces[j].top);
                pieces[i].right = std::max(pieces[i].right, pieces[j].right);
                pieces[i].bottom = std::max(pieces[i].bottom, pieces[j].bottom);
                pieces[i].pixels += pieces[j].pixels;
                pieces[j] = pieces[count - 1];
                --count;
                joined = true;
            }
        }
    }
    return count;
}

int ColourBlobFinder::find(const uint8_t* rgba, int side, int strideBytes, const ColourRange& range,
                           const BlobFilter& filter, ColourBlob* out, int capacity) {
    if (rgba == nullptr || side <= 0 || strideBytes < side * 4 || capacity <= 0) {
        return 0;
    }
    if (!tableFilled_ || !(tableRange_ == range)) {
        fillTable(range);
    }
    classify(rgba, side, strideBytes);
    closeGaps(side, std::max(1, static_cast<int>(kClosingShareOfSide * static_cast<float>(side))));

    ColourBlob pieces[kMaxPieces];
    int count = labelPatches(side, pieces, kMaxPieces);
    count = joinNearbyPatches(pieces, count);

    // What is left must be big enough, and shaped like a person.
    int kept = 0;
    for (int i = 0; i < count; ++i) {
        const ColourBlob& piece = pieces[i];
        const float aspect = static_cast<float>(piece.height()) / static_cast<float>(std::max(piece.width(), 1));
        const bool person = piece.height() >= filter.minHeightPx && piece.pixels >= filter.minPixels &&
                            static_cast<float>(piece.height()) <= filter.maxHeightShare * static_cast<float>(side) &&
                            aspect >= filter.minAspect && aspect <= filter.maxAspect;
        if (person) {
            pieces[kept++] = piece;
        }
    }
    std::sort(pieces, pieces + kept, [](const ColourBlob& a, const ColourBlob& b) { return a.pixels > b.pixels; });
    const int reported = std::min(std::min(kept, capacity), kMaxBlobs);
    std::copy(pieces, pieces + reported, out);
    return reported;
}

bool learnColour(const uint8_t* rgba, int side, int strideBytes, int centerX, int centerY, int half, ColourRange* range) {
    if (rgba == nullptr || range == nullptr || side <= 0 || strideBytes < side * 4 || half <= 0) {
        return false;
    }
    const int left = std::max(0, centerX - half);
    const int right = std::min(side - 1, centerX + half);
    const int top = std::max(0, centerY - half);
    const int bottom = std::min(side - 1, centerY + half);

    // Which hues the vivid pixels have, each counted by how vivid and bright it is.
    struct Vivid {
        float hue;
        float saturation;
        float value;
    };
    std::vector<Vivid> vivid;
    float binWeight[kHueBins] = {};
    for (int y = top; y <= bottom; ++y) {
        const uint8_t* pixel = rgba + static_cast<size_t>(y) * static_cast<size_t>(strideBytes) + static_cast<size_t>(left) * 4;
        for (int x = left; x <= right; ++x, pixel += 4) {
            float hue, saturation, value;
            rgbToHsv(static_cast<float>(pixel[0]) / 255.0f, static_cast<float>(pixel[1]) / 255.0f,
                     static_cast<float>(pixel[2]) / 255.0f, &hue, &saturation, &value);
            if (saturation < kVividSaturation || value < kVividValue) {
                continue;
            }
            vivid.push_back({hue, saturation, value});
            const int bin = std::min(kHueBins - 1, static_cast<int>(hue / (360.0f / static_cast<float>(kHueBins))));
            binWeight[bin] += vividness(saturation, value);
        }
    }
    if (static_cast<int>(vivid.size()) < kMinVividPixels) {
        return false;
    }

    // The best hue is where three bins in a row hold the most weight.
    int bestBin = 0;
    float bestWeight = -1.0f;
    for (int bin = 0; bin < kHueBins; ++bin) {
        const float weight = binWeight[(bin + kHueBins - 1) % kHueBins] + binWeight[bin] + binWeight[(bin + 1) % kHueBins];
        if (weight > bestWeight) {
            bestWeight = weight;
            bestBin = bin;
        }
    }
    const float binWidth = 360.0f / static_cast<float>(kHueBins);
    const float centreHue = (static_cast<float>(bestBin) + 0.5f) * binWidth;

    // The hue is the average of those within a bin and a half of the best bin's centre, taken round the colour wheel.
    float sumSin = 0.0f;
    float sumCos = 0.0f;
    std::vector<std::pair<float, float>> saturations;
    std::vector<std::pair<float, float>> values;
    for (const Vivid& pixel : vivid) {
        if (hueDistance(pixel.hue, centreHue) > 1.5f * binWidth) {
            continue;
        }
        const float weight = vividness(pixel.saturation, pixel.value);
        sumSin += weight * std::sin(pixel.hue * kPi / 180.0f);
        sumCos += weight * std::cos(pixel.hue * kPi / 180.0f);
        saturations.emplace_back(pixel.saturation, weight);
        values.emplace_back(pixel.value, weight);
    }
    float hue = std::atan2(sumSin, sumCos) * 180.0f / kPi;
    if (hue < 0.0f) {
        hue += 360.0f;
    }
    // How vivid and bright the colour typically is, again counting the more vivid pixels for more, is what the range asks for.
    range->hueDegrees = hue;
    range->minSaturation = std::clamp(kLearnedShare * weightedMedian(&saturations), kLearnedFloor, kLearnedCeiling);
    range->minValue = std::clamp(kLearnedShare * weightedMedian(&values), kLearnedFloor, kLearnedCeiling);
    return true;
}

}  // namespace aimbuddy
