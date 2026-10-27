#ifndef AIMBUDDY_CORE_DETECTION_DECODER_H
#define AIMBUDDY_CORE_DETECTION_DECODER_H

#include <cstdint>

#include "geometry.h"
#include "model_profile.h"
#include "tracker.h"

namespace aimbuddy {

/// A detection in model input pixels (the coordinate system of the 256 x 256 network input).
struct ModelBox {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float score = 0.0f;
    int classId = 0;
    uint8_t clipFlags = kClipNone;  ///< Edges where the box touches the border of the model input.
};

/// Facts about a model's output tensor, worked out once when the model is loaded.
struct DecodeSpec {
    OutputLayout layout = OutputLayout::kChannelsFirst;
    int classCount = 0;
    bool normalized = false;  ///< Box coordinates are fractions of the input size instead of pixels.
};

/// Limits applied while decoding.
struct DecodeParams {
    float minScore = 0.12f;  ///< Candidates below this score are dropped before anything else.
    float nmsIou = 0.50f;    ///< Boxes of one class overlapping more than this are merged into the best.
    int modelSize = 256;     ///< Width and height of the network input.
};

/**
 * Works out how a model's output tensor is arranged from its shape and values.
 *
 * `rows` and `cols` are the tensor's two dimensions and `data` its contents, row by row. Pass
 * `endToEnd` when the model description says the model already removes duplicates (6 columns of
 * [x1, y1, x2, y2, score, class]); the shape alone cannot tell that apart from a two class model.
 * Returns false when the shape cannot be a detector output.
 */
bool inferDecodeSpec(const float* data, int rows, int cols, bool endToEnd, DecodeSpec* spec);

/**
 * Network input side implied by the number of anchors of a model with strides 8, 16 and 32
 * (an input of S pixels gives (S/8)^2 + (S/16)^2 + (S/32)^2 anchors). Returns 0 when no
 * multiple of 32 matches, for example a model with extra detection scales.
 */
int inferInputSize(int anchors);

/**
 * Turns the output tensor into a short list of boxes, strongest first.
 *
 * Steps: read each candidate's best class, drop weak and ignored ones, convert to corner form,
 * mark the edges where the box is cut off by the model input, then merge duplicates of the same
 * class. Returns how many boxes were written (at most `capacity`).
 */
int decodeDetections(const float* data, int rows, int cols, const DecodeSpec& spec,
                     const DecodeParams& params, const ModelProfile& profile,
                     ModelBox* out, int capacity);

/**
 * Converts a box from model input pixels to screen pixels.
 *
 * The model saw the square region `roi` of the capture, shrunk to the model size; the capture
 * is in turn a scaled copy of the screen. Both steps are plain scales plus an offset.
 */
Detection mapToScreen(const ModelBox& box, const RoiRect& roi, const CaptureGeometry& geometry,
                      int modelSize);

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_DETECTION_DECODER_H
