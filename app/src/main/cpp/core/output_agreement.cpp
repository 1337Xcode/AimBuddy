#include "core/output_agreement.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aimbuddy {

namespace {

/// How many ranks of the sorted values are compared.
constexpr int kRanks = 64;

constexpr int kBlockPixels = 16;

/// Least range of values the differences are measured against, so that outputs which are all nearly zero are not judged on noise.
constexpr float kLeastRange = 1e-3f;

bool allFinite(const float* values, int count) {
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool outputsAgree(const float* cpu, const float* gpu, int count, float* error) {
    if (error != nullptr) {
        *error = 0.0f;
    }
    if (cpu == nullptr || gpu == nullptr || count <= 0) {
        return false;
    }
    if (!allFinite(cpu, count) || !allFinite(gpu, count)) {
        return false;
    }
    std::vector<float> sortedCpu(cpu, cpu + count);
    std::vector<float> sortedGpu(gpu, gpu + count);
    std::sort(sortedCpu.begin(), sortedCpu.end());
    std::sort(sortedGpu.begin(), sortedGpu.end());

    const float range = std::max(sortedCpu.back() - sortedCpu.front(), kLeastRange);
    float worst = 0.0f;
    for (int rank = 0; rank < kRanks; ++rank) {
        const size_t index = static_cast<size_t>(rank) * static_cast<size_t>(count - 1) / (kRanks - 1);
        worst = std::max(worst, std::fabs(sortedCpu[index] - sortedGpu[index]) / range);
    }
    if (error != nullptr) {
        *error = worst;
    }
    return worst <= kOutputAgreementTolerance;
}

float agreementPictureValue(int x, int y, int channel) {
    // A hash of the block's place and the channel, so no two neighbouring blocks share a colour.
    uint32_t hash = static_cast<uint32_t>(x / kBlockPixels) * 73856093u ^ static_cast<uint32_t>(y / kBlockPixels) * 19349663u ^
                    static_cast<uint32_t>(channel + 1) * 83492791u;
    hash ^= hash >> 13;
    hash *= 0x5bd1e995u;
    hash ^= hash >> 15;
    return static_cast<float>(hash & 0xFFFFu) / 65535.0f;
}

}  // namespace aimbuddy
