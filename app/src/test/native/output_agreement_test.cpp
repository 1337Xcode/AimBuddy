#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/output_agreement.h"
#include "test_harness.h"

using aimbuddy::agreementPictureValue;
using aimbuddy::outputsAgree;

namespace {

/// A tensor shaped like a detector's raw output: box coordinates in pixels and class scores in 0 to 1, row after row.
std::vector<float> detectorOutput(int rows, uint32_t seed) {
    testing::Random random(seed);
    std::vector<float> values;
    values.reserve(static_cast<size_t>(rows) * 6);
    for (int row = 0; row < rows; ++row) {
        values.push_back(static_cast<float>(random.uniform() * 640.0));   // centre x
        values.push_back(static_cast<float>(random.uniform() * 640.0));   // centre y
        values.push_back(static_cast<float>(random.uniform() * 300.0));   // width
        values.push_back(static_cast<float>(random.uniform() * 400.0));   // height
        values.push_back(static_cast<float>(random.uniform() * random.uniform()));   // score
        values.push_back(static_cast<float>(random.uniform() * random.uniform() * 0.3));
    }
    return values;
}

}  // namespace

TEST(the_same_output_agrees_with_itself) {
    const std::vector<float> cpu = detectorOutput(1344, 3);
    float error = 1.0f;
    CHECK(outputsAgree(cpu.data(), cpu.data(), static_cast<int>(cpu.size()), &error));
    CHECK_NEAR(error, 0.0, 1e-9);
}

TEST(half_precision_noise_does_not_make_two_outputs_disagree) {
    const std::vector<float> cpu = detectorOutput(1344, 4);
    std::vector<float> gpu = cpu;
    testing::Random noise(9);
    for (float& value : gpu) {
        value += static_cast<float>((noise.uniform() - 0.5) * 2.0 * 1e-3 * (1.0 + std::fabs(value)));   // about one part in a thousand
    }
    CHECK(outputsAgree(cpu.data(), gpu.data(), static_cast<int>(cpu.size())));
}

TEST(rows_that_swapped_places_are_not_a_difference) {
    const std::vector<float> cpu = detectorOutput(1344, 5);
    std::vector<float> gpu = cpu;
    for (size_t row = 0; row + 1 < 1344; row += 2) {   // every pair of rows the other way round
        for (size_t column = 0; column < 6; ++column) {
            std::swap(gpu[row * 6 + column], gpu[(row + 1) * 6 + column]);
        }
    }
    CHECK(outputsAgree(cpu.data(), gpu.data(), static_cast<int>(cpu.size())));
}

TEST(a_gpu_that_returns_zeros_disagrees) {
    const std::vector<float> cpu = detectorOutput(1344, 6);
    const std::vector<float> gpu(cpu.size(), 0.0f);
    float error = 0.0f;
    CHECK(!outputsAgree(cpu.data(), gpu.data(), static_cast<int>(cpu.size()), &error));
    CHECK(error > 0.3f);
}

TEST(a_gpu_that_returns_one_constant_disagrees) {
    const std::vector<float> cpu = detectorOutput(1344, 7);
    const std::vector<float> gpu(cpu.size(), 123.0f);
    CHECK(!outputsAgree(cpu.data(), gpu.data(), static_cast<int>(cpu.size())));
}

TEST(a_value_that_is_not_a_number_is_a_disagreement) {
    const std::vector<float> cpu = detectorOutput(1344, 8);
    std::vector<float> gpu = cpu;
    gpu[100] = std::nanf("");
    CHECK(!outputsAgree(cpu.data(), gpu.data(), static_cast<int>(cpu.size())));
    gpu[100] = cpu[100];
    gpu[200] = HUGE_VALF;   // half precision overflow
    CHECK(!outputsAgree(cpu.data(), gpu.data(), static_cast<int>(cpu.size())));
}

TEST(a_shifted_distribution_is_a_disagreement) {
    const std::vector<float> cpu = detectorOutput(1344, 10);
    std::vector<float> gpu = cpu;
    for (float& value : gpu) {
        value += 60.0f;   // a tenth of the range of coordinates
    }
    CHECK(!outputsAgree(cpu.data(), gpu.data(), static_cast<int>(cpu.size())));
}

TEST(nothing_to_compare_is_a_disagreement) {
    const float one = 1.0f;
    CHECK(!outputsAgree(&one, &one, 0));
    CHECK(!outputsAgree(nullptr, &one, 1));
    CHECK(!outputsAgree(&one, nullptr, 1));
}

TEST(the_picture_both_processors_are_given_is_the_same_every_time_and_in_range) {
    int different = 0;
    for (int y = 0; y < 64; y += 16) {
        for (int x = 0; x < 64; x += 16) {
            const float value = agreementPictureValue(x, y, 1);
            CHECK(value >= 0.0f && value <= 1.0f);
            CHECK(value == agreementPictureValue(x + 7, y + 9, 1));   // the same block, the same colour
            different += value != agreementPictureValue(x + 16, y, 1) ? 1 : 0;
        }
    }
    CHECK(different >= 14);   // neighbouring blocks have colours of their own
}
