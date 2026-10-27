#ifndef AIMBUDDY_CORE_CAMERA_MOTION_H
#define AIMBUDDY_CORE_CAMERA_MOTION_H

#include <cstdint>
#include <mutex>

#include "camera_response.h"
#include "ego_motion.h"
#include "finger_log.h"
#include "measured_motion.h"
#include "response_estimator.h"

namespace aimbuddy {

/**
 * Everything the app knows about how the camera is moving, in one place.
 *
 * It answers one question for the tracker and the aim controller: how far did the whole scene
 * slide across the screen between two instants? Time that was captured is answered from what was
 * measured on the captured frames. Time after the newest captured frame is answered from the
 * assist's own recent finger moves and the camera's learned response to them, which is how the
 * aim can account for moves that are on their way and not yet visible.
 *
 * It also learns the response (see ResponseEstimator) and hands out the best current figures.
 *
 * Safe to use from several threads.
 */
class CameraMotion final : public EgoMotion {
public:
    CameraMotion() = default;

    /**
     * Sets the figures to use before the camera has been measured, and whether to measure at all.
     * With learning off the prior is used as is, for a player who set the numbers by hand.
     */
    void setPrior(const CameraResponse& prior, bool learn);

    /// Starts from figures learned in an earlier run, trusted as much as `confidence` says (0 to 1).
    void seed(const CameraResponse& earlier, float confidence);

    /// The response to plan with: the measured one when it can be trusted, the prior otherwise.
    CameraResponse response() const;

    /// 0 to 1: how far response() can be trusted.
    float confidence() const;

    /// True once the camera has been measured in this run or seeded from an earlier one.
    bool hasLearned() const;

    /// Notes that the assist moved its finger by (dx, dy) pixels, evenly between sentNs and sentNs + spreadNs.
    void recordFinger(int64_t sentNs, int64_t spreadNs, float dx, float dy);

    /**
     * Adds the slide of the scene measured between two captured frames, in screen pixels.
     * `quality` (0 to 1) says how far to trust it; a measurement that is too poor is replaced by
     * what the response predicts, so the record never has gaps.
     */
    void addMeasurement(int64_t fromNs, int64_t toNs, float dx, float dy, float quality);

    /// Finger travel in pixels that has been moved but whose effect no captured frame shows yet.
    void unseenFinger(float* dx, float* dy) const;

    void shiftBetween(int64_t fromNs, int64_t toNs, float* dx, float* dy) const override;

    /// Forgets the recorded moves and measurements; what was learned about the response stays.
    void clear();

private:
    void modelShift(int64_t fromNs, int64_t toNs, float* dx, float* dy) const;
    void adopt(const ResponseEstimator::Estimate& estimate);

    mutable std::mutex mutex_;
    FingerLog fingers_;
    MeasuredMotion measured_;
    ResponseEstimator estimator_;

    CameraResponse prior_;
    CameraResponse current_;
    bool learn_ = true;
    bool hasLearned_ = false;
    float confidence_ = 0.0f;
    int64_t lastEstimateNs_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_CAMERA_MOTION_H
