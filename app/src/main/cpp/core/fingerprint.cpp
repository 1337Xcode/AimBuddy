#include "core/fingerprint.h"

#include <cstring>

namespace aimbuddy {

namespace {

/// The prime of the 64 bit FNV hash, and how far the high bits are folded down after each step.
constexpr uint64_t kPrime = 0x100000001b3ULL;
constexpr int kFold = 29;

uint64_t step(uint64_t fingerprint, uint64_t word) {
    fingerprint = (fingerprint ^ word) * kPrime;
    return fingerprint ^ (fingerprint >> kFold);
}

}  // namespace

uint64_t mixIntoFingerprint(uint64_t fingerprint, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t at = 0;
    for (; at + sizeof(uint64_t) <= size; at += sizeof(uint64_t)) {
        uint64_t word;
        std::memcpy(&word, bytes + at, sizeof(word));
        fingerprint = step(fingerprint, word);
    }
    // The last few bytes, and the length, so that a block and the same block with zeros after it differ.
    uint64_t tail = 0;
    if (size > at) {
        std::memcpy(&tail, bytes + at, size - at);
    }
    return step(step(fingerprint, tail), static_cast<uint64_t>(size));
}

}  // namespace aimbuddy
