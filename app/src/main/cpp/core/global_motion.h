#ifndef AIMBUDDY_CORE_GLOBAL_MOTION_H
#define AIMBUDDY_CORE_GLOBAL_MOTION_H

#include <array>
#include <cstdint>

namespace aimbuddy {

/// How far the picture in a frame slid since the frame before it, in pixels of the frame given.
struct GlobalShift {
    float dx = 0.0f;        ///< Positive: the scene moved right.
    float dy = 0.0f;        ///< Positive: the scene moved down.
    float quality = 0.0f;   ///< 0 to 1: how far the measurement can be trusted.
    bool unchanged = false; ///< The two frames are practically identical, so nothing moved.
};

/// A part of the frame in which something moves by itself (a detected enemy), in pixels of the frame given to push().
struct MovingBox {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

/**
 * Measures how the whole picture slides from one frame to the next.
 *
 * When the camera turns, almost everything on the screen slides by the same amount, so one
 * shift describes it, and it is known without knowing anything about the game or about what is
 * in the picture. The method is phase correlation: the two frames are reduced to 64 by 64 grey
 * values, and the shift is read from where the cross-power spectrum, brought back to the image
 * domain, has its peak. It copes with shifts up to half the frame, needs no features, and ignores
 * the overall brightness.
 *
 * Parts of the picture that do not move with the camera (the game's crosshair and buttons, the
 * weapon, this app's own boxes, the target the camera is following) would pull the answer towards
 * zero. Whenever the picture as a whole clearly moves, the pixels that stayed the same are noted,
 * and the ones noted often enough are left out of later measurements. The strongest few peaks
 * are tried as the scene's motion, each polished by a few steps of least-squares fitting, and
 * the one that explains the largest share of the picture wins; if even that explains too little,
 * what it left unexplained is looked at again.
 *
 * Anything the caller knows moves by itself, such as the enemies the detector found, can be
 * handed over as boxes. Those parts of the picture, in the frame and in the one before it, are
 * left out: a large enemy crossing the middle of the view would otherwise be taken for the camera.
 *
 * Not thread safe. One instance follows one stream of frames.
 */
class GlobalMotionEstimator {
public:
    /// Width and height of the grey picture the frames are reduced to.
    static constexpr int kGrid = 64;

    /// Most peaks of the correlation that are tried as the scene's motion.
    static constexpr int kMaxCandidates = 3;

    GlobalMotionEstimator();

    /**
     * Takes the next frame (RGBA, `side` by `side` pixels, rows `strideBytes` apart) and returns how
     * far its picture slid compared with the previous frame. The first frame, or the first after a
     * change of size or a reset, returns quality 0. `movers` (if any) are the parts of this frame
     * that move by themselves.
     */
    GlobalShift push(const uint8_t* rgba, int side, int strideBytes, const MovingBox* movers = nullptr, int moverCount = 0);

    /**
     * Like push(), for a frame that is already reduced to the grey picture (kGrid by kGrid values,
     * as lastGrey() returns them). It lets a recorded run be replayed through the estimator.
     */
    GlobalShift pushGrey(const float* grey, const MovingBox* movers = nullptr, int moverCount = 0);

    /// The reduced picture of the frame pushed last.
    const std::array<float, kGrid * kGrid>& lastGrey() const { return previousGrey_; }

    /// Forgets the previous frame.
    void reset();

private:
    struct Complex {
        float re = 0.0f;
        float im = 0.0f;
    };
    using Grid = std::array<Complex, kGrid * kGrid>;

    /// One possible motion of the scene: a peak of the correlation, in grid pixels.
    struct Candidate {
        float x = 0.0f;
        float y = 0.0f;
    };

    void reduceToGrey(const uint8_t* rgba, int side, int strideBytes);
    void markMovers(const MovingBox* movers, int moverCount, int side);
    GlobalShift compareWithPrevious(int side);
    void transform(Grid& grid, bool inverse) const;
    void fft1d(Complex* data, int stride, bool inverse) const;
    void learnStaticPixels();
    int findCandidates(Candidate* candidates);
    float refineShift(float* shiftX, float* shiftY, int steps, float frameTextured) const;
    float measureFrameTexture() const;
    void keepUnexplainedPixels(float shiftX, float shiftY);

    std::array<float, kGrid * kGrid> grey_{};         ///< The current frame, reduced.
    std::array<float, kGrid * kGrid> previousGrey_{};
    std::array<uint8_t, kGrid * kGrid> staticScore_{}; ///< How sure each pixel is not to move with the scene.
    std::array<float, kGrid * kGrid> moverMask_{};     ///< 1 where something in this frame moves by itself, 0 elsewhere.
    std::array<float, kGrid * kGrid> previousMoverMask_{};
    std::array<float, kGrid * kGrid> leftOut_{};       ///< The two masks together: what is left out of this comparison.
    Grid current_{};
    Grid previous_{};
    std::array<float, kGrid * kGrid> window_{};       ///< Fades the border out; fixed.
    std::array<float, kGrid * kGrid> weight_{};       ///< How much each pixel counts in the current comparison.
    std::array<float, kGrid * kGrid> layerWeight_{};  ///< The same, with the pixels already explained turned down.
    std::array<float, kGrid * kGrid> lowPass_{};      ///< Weight of each spatial frequency, fixed.
    std::array<float, kGrid / 2> cosine_{};
    std::array<float, kGrid / 2> sine_{};
    std::array<int, kGrid> bitReversed_{};
    int previousSide_ = 0;
    bool hasPrevious_ = false;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_GLOBAL_MOTION_H
