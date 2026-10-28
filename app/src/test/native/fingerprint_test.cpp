#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include "core/fingerprint.h"
#include "test_harness.h"

using aimbuddy::kFingerprintStart;
using aimbuddy::mixIntoFingerprint;

namespace {

std::vector<uint8_t> someBytes(size_t count, uint32_t seed) {
    testing::Random random(seed);
    std::vector<uint8_t> bytes(count);
    for (uint8_t& byte : bytes) {
        byte = static_cast<uint8_t>(random.uniform() * 256.0);
    }
    return bytes;
}

uint64_t of(const std::vector<uint8_t>& bytes) {
    return mixIntoFingerprint(kFingerprintStart, bytes.data(), bytes.size());
}

}  // namespace

TEST(the_same_bytes_give_the_same_fingerprint) {
    const std::vector<uint8_t> bytes = someBytes(4099, 1);
    CHECK(of(bytes) == of(bytes));
    CHECK(of(bytes) == of(std::vector<uint8_t>(bytes)));
}

TEST(one_changed_bit_anywhere_changes_the_fingerprint) {
    const std::vector<uint8_t> bytes = someBytes(1001, 2);   // not a multiple of eight, so the tail is covered too
    const uint64_t original = of(bytes);
    for (size_t at = 0; at < bytes.size(); at += 37) {
        std::vector<uint8_t> changed = bytes;
        changed[at] ^= 0x10;
        CHECK(of(changed) != original);
    }
    std::vector<uint8_t> lastByte = bytes;
    lastByte.back() ^= 0x01;
    CHECK(of(lastByte) != original);
}

TEST(a_block_and_the_same_block_with_zeros_after_it_differ) {
    std::vector<uint8_t> bytes = someBytes(13, 3);
    const uint64_t shortBlock = of(bytes);
    bytes.push_back(0);
    CHECK(of(bytes) != shortBlock);
    bytes.resize(21, 0);
    CHECK(of(bytes) != shortBlock);
}

TEST(the_same_bytes_in_another_order_give_another_fingerprint) {
    std::vector<uint8_t> bytes = someBytes(64, 4);
    std::vector<uint8_t> swapped = bytes;
    std::swap(swapped[0], swapped[9]);
    CHECK(bytes[0] != bytes[9]);
    CHECK(of(swapped) != of(bytes));
}

TEST(mixing_in_two_blocks_depends_on_both) {
    const std::vector<uint8_t> a = someBytes(40, 5);
    const std::vector<uint8_t> b = someBytes(24, 6);
    const uint64_t both = mixIntoFingerprint(mixIntoFingerprint(kFingerprintStart, a.data(), a.size()), b.data(), b.size());
    const uint64_t reversed = mixIntoFingerprint(mixIntoFingerprint(kFingerprintStart, b.data(), b.size()), a.data(), a.size());
    CHECK(both != reversed);
    CHECK(both != of(a));
}

TEST(many_different_small_blocks_do_not_collide) {
    std::set<uint64_t> seen;
    for (uint32_t i = 0; i < 20000; ++i) {
        const std::vector<uint8_t> bytes = someBytes(24 + i % 40, i + 100);
        seen.insert(of(bytes));
    }
    CHECK(seen.size() == 20000);
}
