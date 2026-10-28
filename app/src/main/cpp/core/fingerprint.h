#ifndef AIMBUDDY_CORE_FINGERPRINT_H
#define AIMBUDDY_CORE_FINGERPRINT_H

#include <cstddef>
#include <cstdint>

namespace aimbuddy {

/// Where a fingerprint starts: pass it as the first `fingerprint` of mixIntoFingerprint().
constexpr uint64_t kFingerprintStart = 0xcbf29ce484222325ULL;

/**
 * Folds `size` bytes at `data` into `fingerprint` and returns the new one, to tell whether what is about to be
 * drawn is what was drawn last: the same bytes in the same order always give the same number, and any other bytes
 * give another, except by a chance of about one in 2^64. The number depends on where the blocks end, so mix a
 * picture's parts in the same pieces every time. It is for noticing a change, not for keeping anyone out.
 *
 * It reads eight bytes a step, so a picture of 60 kB costs a few microseconds.
 */
uint64_t mixIntoFingerprint(uint64_t fingerprint, const void* data, size_t size);

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_FINGERPRINT_H
