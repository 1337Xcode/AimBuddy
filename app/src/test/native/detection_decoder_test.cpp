#include <cmath>
#include <vector>

#include "core/detection_decoder.h"
#include "test_harness.h"

using namespace aimbuddy;

namespace {

/// Builds a raw channels-first tensor: (4 + classes) rows by anchors columns, filled with zeros.
struct RawTensor {
    int classes;
    int anchors;
    std::vector<float> data;

    RawTensor(int classCount, int anchorCount)
        : classes(classCount), anchors(anchorCount), data((4 + classCount) * anchorCount, 0.0f) {}

    int rows() const { return 4 + classes; }

    void setAnchor(int i, float cx, float cy, float w, float h, int classId, float score) {
        data[0 * anchors + i] = cx;
        data[1 * anchors + i] = cy;
        data[2 * anchors + i] = w;
        data[3 * anchors + i] = h;
        data[(4 + classId) * anchors + i] = score;
    }

    /// The same values stored anchors-first instead.
    std::vector<float> transposed() const {
        std::vector<float> out(data.size());
        for (int r = 0; r < rows(); ++r) {
            for (int a = 0; a < anchors; ++a) {
                out[a * rows() + r] = data[r * anchors + a];
            }
        }
        return out;
    }
};

DecodeSpec specFor(const RawTensor& tensor) {
    DecodeSpec spec;
    CHECK(inferDecodeSpec(tensor.data.data(), tensor.rows(), tensor.anchors, false, &spec));
    return spec;
}

}  // namespace

TEST(decoder_reads_a_raw_export_and_merges_duplicates) {
    RawTensor tensor(1, 50);
    tensor.setAnchor(3, 100, 120, 40, 90, 0, 0.9f);
    tensor.setAnchor(4, 102, 121, 42, 92, 0, 0.7f);   // duplicate of the first, weaker
    tensor.setAnchor(20, 200, 60, 30, 60, 0, 0.5f);   // a different target
    tensor.setAnchor(30, 50, 50, 30, 60, 0, 0.05f);   // too weak to keep

    const DecodeSpec spec = specFor(tensor);
    CHECK(spec.layout == OutputLayout::kChannelsFirst);
    CHECK(spec.classCount == 1);
    CHECK(!spec.normalized);

    ModelBox out[8];
    const int n = decodeDetections(tensor.data.data(), tensor.rows(), tensor.anchors, spec, DecodeParams(),
                                   ModelProfile::defaultsFor(1), out, 8);
    CHECK(n == 2);
    CHECK_NEAR(out[0].score, 0.9, 1e-4);              // strongest first
    CHECK_NEAR(out[0].left, 80.0, 1e-3);              // centre 100, width 40
    CHECK_NEAR(out[0].top, 75.0, 1e-3);               // centre 120, height 90
    CHECK_NEAR(out[0].right, 120.0, 1e-3);
    CHECK_NEAR(out[0].bottom, 165.0, 1e-3);
    CHECK_NEAR(out[1].score, 0.5, 1e-4);
}

TEST(decoder_reads_the_transposed_layout_identically) {
    RawTensor tensor(2, 40);
    tensor.setAnchor(5, 100, 100, 40, 90, 0, 0.8f);
    tensor.setAnchor(9, 100, 70, 14, 16, 1, 0.6f);
    const std::vector<float> flipped = tensor.transposed();

    DecodeSpec first = specFor(tensor);
    DecodeSpec second;
    CHECK(inferDecodeSpec(flipped.data(), tensor.anchors, tensor.rows(), false, &second));
    CHECK(second.layout == OutputLayout::kRowsFirst);
    CHECK(second.classCount == first.classCount);

    ModelBox a[8], b[8];
    const ModelProfile profile = ModelProfile::defaultsFor(2);
    const int na = decodeDetections(tensor.data.data(), tensor.rows(), tensor.anchors, first, DecodeParams(), profile, a, 8);
    const int nb = decodeDetections(flipped.data(), tensor.anchors, tensor.rows(), second, DecodeParams(), profile, b, 8);
    CHECK(na == 2 && nb == 2);
    for (int i = 0; i < na && i < nb; ++i) {
        CHECK_NEAR(a[i].left, b[i].left, 1e-4);
        CHECK_NEAR(a[i].bottom, b[i].bottom, 1e-4);
        CHECK(a[i].classId == b[i].classId);
    }
}

TEST(decoder_keeps_a_head_inside_its_body) {
    // The head box lies entirely inside the body box. Merging must never confuse different classes.
    RawTensor tensor(2, 30);
    tensor.setAnchor(2, 100, 120, 40, 120, 0, 0.9f);   // body
    tensor.setAnchor(7, 100, 75, 14, 16, 1, 0.8f);     // head
    ModelBox out[8];
    const int n = decodeDetections(tensor.data.data(), tensor.rows(), tensor.anchors, specFor(tensor),
                                   DecodeParams(), ModelProfile::defaultsFor(2), out, 8);
    CHECK(n == 2);
}

TEST(decoder_drops_classes_the_profile_ignores) {
    RawTensor tensor(3, 30);
    tensor.setAnchor(2, 100, 120, 40, 120, 0, 0.9f);   // enemy body
    tensor.setAnchor(6, 200, 120, 40, 120, 2, 0.95f);  // a teammate, marked ignore
    const ModelProfile profile = ModelProfile::parseRoles("body,head,ignore", 3);
    ModelBox out[8];
    const int n = decodeDetections(tensor.data.data(), tensor.rows(), tensor.anchors, specFor(tensor),
                                   DecodeParams(), profile, out, 8);
    CHECK(n == 1);
    CHECK(n == 1 && out[0].classId == 0);
}

TEST(decoder_drops_a_body_box_that_a_surer_ignored_box_covers) {
    // Neighbouring anchors on the player's own character disagree: most call it the own character, one an enemy.
    // The weaker enemy reading must not become a target; an enemy beside it and one the model is surer of stay.
    RawTensor tensor(3, 40);
    tensor.setAnchor(2, 100, 150, 60, 180, 2, 0.85f);  // the own character
    tensor.setAnchor(3, 102, 152, 58, 176, 0, 0.55f);  // the same figure read as a body, less sure
    tensor.setAnchor(9, 200, 100, 30, 80, 0, 0.6f);    // an enemy beside it
    tensor.setAnchor(20, 40, 150, 50, 160, 2, 0.5f);   // a teammate marked ignore...
    tensor.setAnchor(21, 41, 151, 50, 160, 0, 0.7f);   // ...that the model is surer is an enemy
    const ModelProfile profile = ModelProfile::parseRoles("body,head,ignore", 3);
    ModelBox out[8];
    const int n = decodeDetections(tensor.data.data(), tensor.rows(), tensor.anchors, specFor(tensor),
                                   DecodeParams(), profile, out, 8);
    CHECK(n == 2);
    CHECK(n == 2 && out[0].classId == 0 && out[1].classId == 0);
    CHECK(n == 2 && std::fabs(out[0].score - 0.7f) < 1e-4f && std::fabs(out[1].score - 0.6f) < 1e-4f);
}

TEST(decoder_scales_normalised_coordinates_to_pixels) {
    RawTensor tensor(1, 30);
    tensor.setAnchor(1, 0.5f, 0.5f, 0.25f, 0.5f, 0, 0.9f);
    const DecodeSpec spec = specFor(tensor);
    CHECK(spec.normalized);
    ModelBox out[4];
    CHECK(decodeDetections(tensor.data.data(), tensor.rows(), tensor.anchors, spec, DecodeParams(),
                           ModelProfile::defaultsFor(1), out, 4) == 1);
    CHECK_NEAR(out[0].left, 96.0, 1e-3);    // (0.5 - 0.125) * 256
    CHECK_NEAR(out[0].right, 160.0, 1e-3);
    CHECK_NEAR(out[0].top, 64.0, 1e-3);
    CHECK_NEAR(out[0].bottom, 192.0, 1e-3);
}

TEST(decoder_reads_an_end_to_end_export) {
    // Rows of [x1, y1, x2, y2, score, class], already free of duplicates.
    const float rows[] = {80, 60, 120, 160, 0.92f, 0,
                          150, 70, 170, 100, 0.55f, 1,
                          10, 10, 30, 40, 0.80f, 2,      // class 2 is ignored
                          0, 0, 0, 0, 0.0f, 0};
    DecodeSpec spec;
    CHECK(inferDecodeSpec(rows, 4, 6, true, &spec));
    CHECK(spec.layout == OutputLayout::kEndToEnd);
    ModelBox out[8];
    const int n = decodeDetections(rows, 4, 6, spec, DecodeParams(), ModelProfile::parseRoles("body,head,ignore", 3), out, 8);
    CHECK(n == 2);
    CHECK_NEAR(out[0].left, 80.0, 1e-3);
    CHECK_NEAR(out[0].bottom, 160.0, 1e-3);
    CHECK(out[1].classId == 1);
}

TEST(decoder_flags_boxes_cut_off_by_the_input_border) {
    RawTensor tensor(1, 20);
    tensor.setAnchor(0, 250, 120, 40, 90, 0, 0.9f);    // sticks out past the right edge (x 230..270)
    ModelBox out[4];
    const int n = decodeDetections(tensor.data.data(), tensor.rows(), tensor.anchors, specFor(tensor),
                                   DecodeParams(), ModelProfile::defaultsFor(1), out, 4);
    CHECK(n == 1);
    CHECK((out[0].clipFlags & kClipRight) != 0);
    CHECK((out[0].clipFlags & kClipLeft) == 0);
    CHECK_NEAR(out[0].right, 256.0, 1e-4);             // clamped to the input
}

TEST(decoder_survives_garbage_values) {
    RawTensor tensor(1, 20);
    tensor.setAnchor(0, NAN, 100, 40, 90, 0, 0.9f);
    tensor.setAnchor(1, 100, 100, INFINITY, 90, 0, 0.9f);
    tensor.setAnchor(2, 100, 100, 0.5f, 0.5f, 0, 0.9f);  // smaller than the minimum box
    ModelBox out[4];
    DecodeSpec spec;
    spec.layout = OutputLayout::kChannelsFirst;
    spec.classCount = 1;
    CHECK(decodeDetections(tensor.data.data(), tensor.rows(), tensor.anchors, spec, DecodeParams(),
                           ModelProfile::defaultsFor(1), out, 4) == 0);
    CHECK(decodeDetections(nullptr, 5, 20, spec, DecodeParams(), ModelProfile::defaultsFor(1), out, 4) == 0);
}

TEST(decoder_rejects_shapes_that_cannot_be_detections) {
    DecodeSpec spec;
    const float one = 0.0f;
    CHECK(!inferDecodeSpec(&one, 1, 1, false, &spec));
    CHECK(!inferDecodeSpec(&one, 3, 400, false, &spec));    // fewer than 4 box values and a class
    CHECK(!inferDecodeSpec(nullptr, 5, 400, false, &spec));
}

TEST(mapping_takes_model_pixels_to_screen_pixels) {
    CaptureGeometry geometry;
    geometry.screenWidth = 2400;
    geometry.screenHeight = 1080;
    geometry.captureWidth = 1600;
    geometry.captureHeight = 720;
    RoiRect roi;
    roi.x = 640;
    roi.y = 200;
    roi.side = 320;

    ModelBox box;
    box.left = 128;
    box.top = 128;
    box.right = 192;
    box.bottom = 192;
    box.score = 0.7f;
    box.classId = 1;
    const Detection d = mapToScreen(box, roi, geometry, 256);
    // 320 px of capture shown in 256 model px, and 1.5 screen px per capture px: 1.875 per model px.
    CHECK_NEAR(d.x, 128 * 1.875 + 640 * 1.5, 1e-3);
    CHECK_NEAR(d.y, 128 * 1.875 + 200 * 1.5, 1e-3);
    CHECK_NEAR(d.width, 64 * 1.875, 1e-3);
    CHECK_NEAR(d.height, 64 * 1.875, 1e-3);
    CHECK(d.classId == 1);
    CHECK_NEAR(d.score, 0.7, 1e-6);
}

TEST(roi_is_centred_and_clamped) {
    CaptureGeometry geometry;
    geometry.screenWidth = 2400;
    geometry.screenHeight = 1080;
    geometry.captureWidth = 1600;
    geometry.captureHeight = 720;

    const RoiRect small = computeRoi(geometry, 50.0f, 256);      // asks for less than the model input
    CHECK(small.side == 256);
    const RoiRect big = computeRoi(geometry, 600.0f, 256);       // asks for more than 1.75 x the model input
    CHECK(big.side == 448);
    CHECK(big.x + big.side / 2 == geometry.captureWidth / 2);    // centred horizontally
    CHECK(big.y + big.side / 2 == geometry.captureHeight / 2);   // and vertically
    const RoiRect middle = computeRoi(geometry, 200.0f, 256);    // 400 screen px = 266 capture px
    CHECK(middle.side == 266);
    CHECK(computeRoi(CaptureGeometry(), 200.0f, 256).side == 0); // unconfigured geometry gives nothing
    CHECK(computeRoi(geometry, 200.0f, 0).side == 0);

    // A larger model sees a larger region, but never more than the capture's short edge.
    CHECK(computeRoi(geometry, 100.0f, 416).side == 416);
    CHECK(computeRoi(geometry, 600.0f, 640).side == 720);
}

TEST(roi_is_empty_while_the_capture_has_another_shape_than_the_screen) {
    // The display turned from portrait to landscape: frames of the new landscape size came while the screen was
    // still known as portrait, and each of their boxes was scaled by 0.675 where it should have been 1.5.
    CaptureGeometry geometry;
    geometry.screenWidth = 1080;
    geometry.screenHeight = 2400;
    geometry.captureWidth = 1600;
    geometry.captureHeight = 720;
    CHECK(!geometry.hasScreenShape());
    CHECK(computeRoi(geometry, 200.0f, 256).side == 0);

    geometry.captureWidth = 720;    // the capture of that portrait screen, rounded to even sides
    geometry.captureHeight = 1600;
    CHECK(geometry.hasScreenShape());
    CHECK(computeRoi(geometry, 200.0f, 256).side > 0);
    geometry.captureWidth = 718;
    CHECK(geometry.hasScreenShape());
}

TEST(profile_parses_role_lists) {
    const ModelProfile one = ModelProfile::defaultsFor(1);
    CHECK(one.roleOf(0) == ClassRole::kBody);
    CHECK(one.roleOf(1) == ClassRole::kIgnore);
    CHECK(!one.hasHeadClass());

    const ModelProfile two = ModelProfile::defaultsFor(2);
    CHECK(two.roleOf(1) == ClassRole::kHead);
    CHECK(two.hasHeadClass());

    const ModelProfile custom = ModelProfile::parseRoles(" head , body,teammate ,ignore", 4);
    CHECK(custom.roleOf(0) == ClassRole::kHead);
    CHECK(custom.roleOf(1) == ClassRole::kBody);
    CHECK(custom.roleOf(2) == ClassRole::kIgnore);
    CHECK(custom.declaredClassCount == 4);
    CHECK(custom.roleOf(-1) == ClassRole::kIgnore);
    CHECK(custom.roleOf(99) == ClassRole::kIgnore);

    CHECK(ModelProfile::parseRoles("", 2).roleOf(1) == ClassRole::kHead);   // empty falls back to defaults
}

TEST(input_size_follows_from_the_anchor_count) {
    CHECK(inferInputSize(1344) == 256);
    CHECK(inferInputSize(2100) == 320);
    CHECK(inferInputSize(3549) == 416);
    CHECK(inferInputSize(8400) == 640);
    CHECK(inferInputSize(100) == 0);
    CHECK(inferInputSize(0) == 0);
}
