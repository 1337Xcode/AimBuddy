#ifndef AIMBUDDY_CORE_OUTPUT_AGREEMENT_H
#define AIMBUDDY_CORE_OUTPUT_AGREEMENT_H

namespace aimbuddy {

/// The share of the CPU's range of values a rank may differ by before the two outputs are said to disagree.
constexpr float kOutputAgreementTolerance = 0.05f;

/**
 * Whether the GPU's answer to a picture is the CPU's answer, give or take what half precision arithmetic
 * changes: the two output tensors of one network, run on one picture on each processor.
 *
 * Some graphics drivers are fast and wrong: they return zeros, a constant, or numbers that overflowed half
 * precision. A detector that took the GPU for being faster would then see nobody, with nothing to say why.
 * The values are compared sorted, so that two rows that swapped places do not count as a difference, and
 * only at 64 evenly spaced ranks, which is enough to tell a damaged distribution from a faithful one.
 *
 * `error`, when given, receives the largest difference found as a share of the CPU's range of values.
 * False for an empty tensor, for a value that is not finite, and for a difference above
 * kOutputAgreementTolerance.
 */
bool outputsAgree(const float* cpu, const float* gpu, int count, float* error = nullptr);

/**
 * One value, 0 to 1, of the picture the two processors are given: blocks of 16 by 16 pixels, each a colour
 * of its own, so that every layer of the network has edges and varied values to work on, which a flat grey
 * picture does not give. The same picture every time, so that a disagreement can be told again.
 */
float agreementPictureValue(int x, int y, int channel);

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_OUTPUT_AGREEMENT_H
