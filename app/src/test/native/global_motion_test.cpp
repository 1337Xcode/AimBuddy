#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/global_motion.h"
#include "test_harness.h"

using namespace aimbuddy;

namespace {

constexpr int kSide = 256;

/// A smooth random picture defined everywhere, so a shift of any size, whole pixels or not, is exact.
class Texture {
public:
    explicit Texture(uint32_t seed, int blobs = 120) {
        testing::Random random(seed);
        for (int i = 0; i < blobs; ++i) {
            blobs_.push_back({static_cast<float>(random.uniform() * 700.0 - 200.0), static_cast<float>(random.uniform() * 700.0 - 200.0),
                              static_cast<float>(5.0 + random.uniform() * 22.0), static_cast<float>((random.uniform() - 0.5) * 120.0)});
        }
    }

    float at(float x, float y) const {
        float value = 128.0f;
        for (const Blob& blob : blobs_) {
            const float dx = x - blob.x;
            const float dy = y - blob.y;
            const float distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < 9.0f * blob.sigma * blob.sigma) {
                value += blob.amplitude * std::exp(-distanceSquared / (2.0f * blob.sigma * blob.sigma));
            }
        }
        return value;
    }

private:
    struct Blob {
        float x, y, sigma, amplitude;
    };
    std::vector<Blob> blobs_;
};

/// A frame of the scene seen with the camera turned so that its content moved by (shiftX, shiftY).
struct Frame {
    std::vector<uint8_t> rgba = std::vector<uint8_t>(static_cast<size_t>(kSide) * kSide * 4, 255);

    void set(int x, int y, float grey) {
        if (x < 0 || y < 0 || x >= kSide || y >= kSide) {
            return;
        }
        const uint8_t value = static_cast<uint8_t>(std::clamp(grey, 0.0f, 255.0f));
        uint8_t* pixel = &rgba[(static_cast<size_t>(y) * kSide + x) * 4];
        pixel[0] = pixel[1] = pixel[2] = value;
        pixel[3] = 255;
    }
};

Frame renderScene(const Texture& texture, float shiftX, float shiftY, float noise = 0.0f, uint32_t noiseSeed = 1) {
    Frame frame;
    testing::Random random(noiseSeed);
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const float grey = texture.at(static_cast<float>(x) - shiftX, static_cast<float>(y) - shiftY) +
                               (noise > 0.0f ? static_cast<float>(random.gaussian(noise)) : 0.0f);
            frame.set(x, y, grey);
        }
    }
    return frame;
}

/// Things drawn on the screen that do not move with the camera: the game's crosshair, this app's
/// zone outline and a weapon at the bottom corner.
void drawStaticOverlay(Frame& frame) {
    for (int i = -14; i <= 14; ++i) {
        for (int t = -1; t <= 1; ++t) {
            frame.set(kSide / 2 + i, kSide / 2 + t, 250.0f);
            frame.set(kSide / 2 + t, kSide / 2 + i, 250.0f);
        }
    }
    for (int i = 0; i < kSide; ++i) {
        for (int t = 0; t < 2; ++t) {
            frame.set(i, t, 40.0f);
            frame.set(i, kSide - 1 - t, 40.0f);
            frame.set(t, i, 40.0f);
            frame.set(kSide - 1 - t, i, 40.0f);
        }
    }
    for (int y = kSide - 70; y < kSide; ++y) {
        for (int x = kSide - 90; x < kSide - 20; ++x) {
            frame.set(x, y, 30.0f + 0.3f * static_cast<float>(x - (kSide - 90)));
        }
    }
}

/// A bright block that moves by itself, like an enemy running across the view.
void drawMover(Frame& frame, int left, int top) {
    for (int y = top; y < top + 80; ++y) {
        for (int x = left; x < left + 40; ++x) {
            frame.set(x, y, 235.0f - 0.8f * static_cast<float>(y - top));
        }
    }
}

GlobalShift measureShift(const Frame& first, const Frame& second) {
    GlobalMotionEstimator estimator;
    estimator.push(first.rgba.data(), kSide, kSide * 4);
    return estimator.push(second.rgba.data(), kSide, kSide * 4);
}

}  // namespace

TEST(global_motion_finds_known_shifts) {
    const Texture texture(1);
    const float shifts[][2] = {{0.0f, 0.0f}, {3.5f, -2.25f}, {-17.0f, 9.0f}, {30.2f, -28.6f}, {-40.0f, 40.0f}, {0.0f, 55.0f}};
    const Frame first = renderScene(texture, 0.0f, 0.0f);
    for (const auto& shift : shifts) {
        const Frame second = renderScene(texture, shift[0], shift[1]);
        const GlobalShift result = measureShift(first, second);
        CHECK_NEAR(result.dx, shift[0], 0.5);
        CHECK_NEAR(result.dy, shift[1], 0.5);
        if (shift[0] != 0.0f || shift[1] != 0.0f) {
            CHECK(result.quality > 0.5f);
        }
    }
}

TEST(global_motion_copes_with_noise) {
    const Texture texture(2);
    const Frame first = renderScene(texture, 0.0f, 0.0f, 8.0f, 11);
    const Frame second = renderScene(texture, 13.7f, -6.3f, 8.0f, 12);
    const GlobalShift result = measureShift(first, second);
    CHECK_NEAR(result.dx, 13.7, 1.0);
    CHECK_NEAR(result.dy, -6.3, 1.0);
    CHECK(result.quality > 0.4f);
}

TEST(global_motion_learns_to_ignore_what_does_not_move_with_the_camera) {
    // The game's crosshair, this app's outline and a weapon stay where they are while the scene
    // slides by (12, -7) a frame. The first comparisons are fooled by them; once they have been seen
    // not to move with the scene, they are left out and the scene's own motion is read.
    const Texture texture(3);
    GlobalMotionEstimator estimator;
    GlobalShift result;
    for (int frame = 0; frame < 10; ++frame) {
        Frame image = renderScene(texture, 12.0f * static_cast<float>(frame), -7.0f * static_cast<float>(frame));
        drawStaticOverlay(image);
        result = estimator.push(image.rgba.data(), kSide, kSide * 4);
    }
    CHECK_NEAR(result.dx, 12.0, 1.0);
    CHECK_NEAR(result.dy, -7.0, 1.0);
    CHECK(result.quality > 0.4f);
}

TEST(global_motion_ignores_something_moving_on_its_own) {
    const Texture texture(4);
    Frame first = renderScene(texture, 0.0f, 0.0f);
    Frame second = renderScene(texture, 12.0f, -7.0f);
    drawMover(first, 150, 90);
    drawMover(second, 125, 100);   // it moved by (-25, 10) on screen, not with the scene
    const GlobalShift result = measureShift(first, second);
    CHECK_NEAR(result.dx, 12.0, 1.2);
    CHECK_NEAR(result.dy, -7.0, 1.2);
}

TEST(global_motion_does_not_trust_unrelated_frames) {
    const Texture a(5);
    const Texture b(6);
    const GlobalShift result = measureShift(renderScene(a, 0.0f, 0.0f), renderScene(b, 0.0f, 0.0f));
    CHECK(result.quality < 0.3f);
}

TEST(global_motion_sees_identical_frames_as_unchanged) {
    const Texture texture(7);
    const Frame frame = renderScene(texture, 0.0f, 0.0f);
    const GlobalShift result = measureShift(frame, frame);
    CHECK(result.unchanged);
    CHECK_NEAR(result.dx, 0.0, 1e-6);
    CHECK_NEAR(result.dy, 0.0, 1e-6);
    CHECK(result.quality > 0.9f);
}

TEST(global_motion_cannot_vouch_for_a_flat_picture_that_did_not_change) {
    // A sky or a plain wall looks the same however far the camera turned, so two equal frames of it
    // say nothing about the camera. Called "unchanged with full trust" they would teach the controller
    // that the game ignores the finger.
    Frame flat;
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            flat.set(x, y, 128.0f);
        }
    }
    const GlobalShift result = measureShift(flat, flat);
    CHECK(result.unchanged);
    CHECK_NEAR(result.quality, 0.0, 1e-6);
}

TEST(global_motion_cannot_vouch_for_a_picture_that_is_all_one_enemy) {
    // A box that covers the whole picture is left out, and what is left has nothing to compare.
    const Texture scene(16);
    const Frame frame = renderScene(scene, 0.0f, 0.0f);
    const MovingBox everything[] = {{0.0f, 0.0f, static_cast<float>(kSide), static_cast<float>(kSide)}};
    GlobalMotionEstimator estimator;
    estimator.push(frame.rgba.data(), kSide, kSide * 4, everything, 1);
    const GlobalShift result = estimator.push(frame.rgba.data(), kSide, kSide * 4, everything, 1);
    CHECK(result.unchanged);
    CHECK_NEAR(result.quality, 0.0, 1e-6);
}

TEST(global_motion_needs_two_frames_and_a_steady_size) {
    const Texture texture(8);
    const Frame frame = renderScene(texture, 0.0f, 0.0f);
    GlobalMotionEstimator estimator;
    CHECK(estimator.push(frame.rgba.data(), kSide, kSide * 4).quality == 0.0f);   // nothing to compare with
    CHECK(estimator.push(frame.rgba.data(), kSide, kSide * 4).unchanged);
    estimator.reset();
    CHECK(estimator.push(frame.rgba.data(), kSide, kSide * 4).quality == 0.0f);
    CHECK(estimator.push(nullptr, kSide, kSide * 4).quality == 0.0f);
}

TEST(global_motion_learns_to_ignore_large_panels_that_stay_put) {
    // Two large panels (the game's buttons) stay where they are while the scene slides by (15, 0)
    // a frame. Their strong edges say "no motion" at first; after a few frames they have been seen
    // not to move with the scene and the scene's motion is read.
    const Texture texture(9);
    GlobalMotionEstimator estimator;
    GlobalShift result;
    for (int frame = 0; frame < 10; ++frame) {
        Frame image = renderScene(texture, 15.0f * static_cast<float>(frame), 0.0f);
        for (int y = 20; y < 110; ++y) {
            for (int x = 20; x < 110; ++x) {
                const bool border = x < 24 || y < 24 || x >= 106 || y >= 106;
                image.set(x, y, border ? 240.0f : 40.0f);
            }
        }
        for (int y = 150; y < 240; ++y) {
            for (int x = 150; x < 240; ++x) {
                const bool border = x < 154 || y < 154 || x >= 236 || y >= 236;
                image.set(x, y, border ? 15.0f : 200.0f);
            }
        }
        result = estimator.push(image.rgba.data(), kSide, kSide * 4);
    }
    CHECK_NEAR(result.dx, 15.0, 1.2);
    CHECK_NEAR(result.dy, 0.0, 1.2);
}

namespace {

/// A picture of the kind the sky and open ground make: a gradient sky over flat ground, and a few
/// plain shapes with soft edges. There is little to follow, and a shift of any size is still exact.
class SparseScene {
public:
    explicit SparseScene(uint32_t seed, int shapes) {
        testing::Random random(seed);
        for (int i = 0; i < shapes; ++i) {
            shapes_.push_back({static_cast<float>(20.0 + random.uniform() * 220.0), static_cast<float>(40.0 + random.uniform() * 150.0),
                               static_cast<float>(8.0 + random.uniform() * 12.0), static_cast<float>(8.0 + random.uniform() * 12.0),
                               static_cast<float>(110.0 + random.uniform() * 60.0)});
        }
    }

    float at(float x, float y) const {
        constexpr float kHorizon = 128.0f;
        float value = y < kHorizon ? 200.0f - 0.25f * y : 70.0f;
        for (const Shape& shape : shapes_) {
            const float inside = edge(shape.halfWidth - std::fabs(x - shape.x)) * edge(shape.halfHeight - std::fabs(y - shape.y));
            value += inside * (shape.tone - value);
        }
        return value;
    }

private:
    struct Shape {
        float x, y, halfWidth, halfHeight, tone;
    };

    /// 0 outside, 1 inside, with a soft edge about two pixels wide.
    static float edge(float distanceInside) { return std::clamp(0.5f + distanceInside / 2.0f, 0.0f, 1.0f); }

    std::vector<Shape> shapes_;
};

Frame renderSparse(const SparseScene& scene, float shiftX, float shiftY) {
    Frame frame;
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            frame.set(x, y, scene.at(static_cast<float>(x) - shiftX, static_cast<float>(y) - shiftY));
        }
    }
    return frame;
}

}  // namespace

TEST(global_motion_follows_a_mostly_flat_picture) {
    // Two shapes under a horizon: the horizon says nothing about a sideways slide, so the whole
    // answer rests on a few edges. Most slides must still be found, and none may be wrong with confidence.
    for (const uint32_t seed : {2u, 3u, 4u}) {
        const SparseScene scene(seed, 2);
        const Frame first = renderSparse(scene, 0.0f, 0.0f);
        int found = 0;
        for (const float shift : {-45.0f, -30.0f, -12.0f, 6.0f, 20.0f, 36.0f}) {
            const GlobalShift result = measureShift(first, renderSparse(scene, shift, 0.0f));
            const bool right = std::fabs(result.dx - shift) <= 1.5f && std::fabs(result.dy) <= 1.5f;
            found += (right && result.quality > 0.45f) ? 1 : 0;
            CHECK(right || result.quality <= 0.45f);
        }
        CHECK(found >= 5);
    }
}

TEST(global_motion_does_not_call_a_sliding_flat_picture_unchanged) {
    const SparseScene scene(2, 2);
    const GlobalShift result = measureShift(renderSparse(scene, 0.0f, 0.0f), renderSparse(scene, 6.0f, 0.0f));
    CHECK(!result.unchanged);
    CHECK_NEAR(result.dx, 6.0, 1.0);
}

namespace {

/// A block with a picture of its own, such as an enemy standing close to the camera.
void drawBigMover(Frame& frame, const Texture& look, int left, int top) {
    for (int y = top; y < top + 150; ++y) {
        for (int x = left; x < left + 90; ++x) {
            frame.set(x, y, look.at(static_cast<float>(x - left) + 40.0f, static_cast<float>(y - top) + 40.0f) * 1.1f);
        }
    }
}

}  // namespace

TEST(global_motion_leaves_out_the_boxes_it_is_told_move_by_themselves) {
    // The scene slides by (6, -3) while a large, strongly textured figure runs across the view by
    // (-30, 8). Told where the figure is in each frame, the estimator reads the scene's slide.
    const Texture scene(12);
    const Texture figure(13, 260);
    int told = 0;
    for (int trial = 0; trial < 4; ++trial) {
        Frame first = renderScene(scene, 0.0f, 0.0f);
        Frame second = renderScene(scene, 6.0f, -3.0f);
        const int left = 30 + 28 * trial;
        drawBigMover(first, figure, left + 30, 50);
        drawBigMover(second, figure, left, 58);
        const MovingBox before[] = {{static_cast<float>(left + 30), 50.0f, static_cast<float>(left + 120), 200.0f}};
        const MovingBox after[] = {{static_cast<float>(left), 58.0f, static_cast<float>(left + 90), 208.0f}};

        GlobalMotionEstimator estimator;
        estimator.push(first.rgba.data(), kSide, kSide * 4, before, 1);
        const GlobalShift result = estimator.push(second.rgba.data(), kSide, kSide * 4, after, 1);
        if (std::fabs(result.dx - 6.0f) < 1.2f && std::fabs(result.dy + 3.0f) < 1.2f && result.quality > 0.4f) {
            ++told;
        }
    }
    CHECK(told == 4);
}

TEST(global_motion_does_not_call_a_frame_unchanged_because_only_a_mover_moved) {
    // Nothing but the figure moves: with its box left out, the picture did not change.
    const Texture scene(14);
    const Texture figure(15, 260);
    Frame first = renderScene(scene, 0.0f, 0.0f);
    Frame second = renderScene(scene, 0.0f, 0.0f);
    drawBigMover(first, figure, 100, 50);
    drawBigMover(second, figure, 80, 56);
    const MovingBox before[] = {{100.0f, 50.0f, 190.0f, 200.0f}};
    const MovingBox after[] = {{80.0f, 56.0f, 170.0f, 206.0f}};
    GlobalMotionEstimator estimator;
    estimator.push(first.rgba.data(), kSide, kSide * 4, before, 1);
    const GlobalShift result = estimator.push(second.rgba.data(), kSide, kSide * 4, after, 1);
    CHECK(result.unchanged);
}
