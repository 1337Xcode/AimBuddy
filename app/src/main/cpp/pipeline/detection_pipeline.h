#ifndef AIMBUDDY_PIPELINE_DETECTION_PIPELINE_H
#define AIMBUDDY_PIPELINE_DETECTION_PIPELINE_H

#include <android/asset_manager.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "../capture/roi_capture.h"
#include "../core/camera_motion.h"
#include "../core/detection_batch.h"
#include "../core/global_motion.h"
#include "../core/model_profile.h"
#include "../core/tracker.h"
#include "../detector/yolo_detector.h"

namespace aimbuddy {

/**
 * Turns captured frames into tracked targets.
 *
 * It owns the chain  capture mailbox -> detector -> tracker  and the thread that runs it. Before
 * each frame is detected, how far the whole picture slid since the frame before is measured and
 * recorded, so the tracker knows how much of a target's apparent motion was the camera's. The
 * thread always takes the newest frame, so when the phone cannot keep up the pipeline drops
 * frames instead of falling behind, and what it reports is never older than one detection.
 *
 * Other parts of the app read the result through tracker() and are told about every new batch
 * through the observer, so the pipeline does not depend on whoever uses the targets.
 */
class DetectionPipeline {
public:
    /// Called on the pipeline thread after every detection; must return quickly.
    using BatchObserver = std::function<void(const DetectionBatch&)>;

    /// Measurements of the running pipeline, smoothed over the last second or so.
    struct Stats {
        float inferenceMs = 0.0f;           ///< Time to preprocess, run the network and decode.
        float frameAgeMs = 0.0f;            ///< From the frame being on screen to its result being ready.
        float detectionsPerSecond = 0.0f;
        uint64_t replacedFrames = 0;        ///< Frames skipped because a newer one arrived first.
        const char* backend = "";
    };

    DetectionPipeline() = default;
    ~DetectionPipeline() { stop(); }
    DetectionPipeline(const DetectionPipeline&) = delete;
    DetectionPipeline& operator=(const DetectionPipeline&) = delete;

    /// Loads the model. Must not be called while running. Returns false if the model is unusable.
    bool loadModel(AAssetManager* assets, const std::string& paramPath, const std::string& binPath,
                   const ModelProfile& profile);
    bool isModelLoaded() const { return detector_.isLoaded(); }

    /// Starts the pipeline thread. Does nothing if it is running or no model is loaded.
    void start();

    /// Stops the pipeline thread and waits for it. Safe to call when not running.
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_acquire); }

    /// Sets the observer. Must be called while stopped.
    void setObserver(BatchObserver observer) { observer_ = std::move(observer); }

    /// Sets where the camera's movement between frames is recorded. Must be called while stopped.
    void setCameraMotion(CameraMotion* motion) { cameraMotion_ = motion; }

    /// The player's "minimum confidence" setting; retunes the detector and the tracker. Thread safe.
    void setConfidenceThreshold(float threshold) { confidence_.store(threshold, std::memory_order_relaxed); }

    /// What each class of the loaded model means.
    const ModelProfile& profile() const { return detector_.profile(); }

#ifdef AIMBUDDY_DEBUG_HOOKS
    /// Testing only: with this on, targets come from a test scene (see feedSynthetic) instead of the model.
    void setSyntheticMode(bool on) { syntheticMode_.store(on, std::memory_order_release); }

    /// Testing only: the scene's true targets, in screen pixels, as they are at `sceneNs` (steady clock).
    void feedSynthetic(const Detection* detections, int count, int64_t sceneNs);
#endif

    RoiCapture& capture() { return capture_; }
    Tracker& tracker() { return tracker_; }
    Stats stats() const;

private:
    void run();
    void applyConfidenceIfChanged(float& applied);
    void measureCameraMotion(const RoiFrame& frame);
#ifdef AIMBUDDY_DEBUG_HOOKS
    bool detectSynthetic(const RoiFrame& frame, DetectionBatch& batch);
#endif
    void recordStats(const DetectionBatch& batch, int64_t finishedNs);

    RoiCapture capture_;
    YoloDetector detector_;
    Tracker tracker_;
    BatchObserver observer_;
    CameraMotion* cameraMotion_ = nullptr;
    GlobalMotionEstimator sceneMotion_;   ///< Reads how the picture slid between frames. Pipeline thread only.
    int64_t lastFrameNs_ = 0;
    float slideSum_ = 0.0f;               ///< Over the frames since the last log line: summed slide (screen pixels)...
    float qualitySum_ = 0.0f;             ///< ...summed quality...
    int slideFrames_ = 0;                 ///< ...and how many frames that is.
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<float> confidence_{0.5f};
#ifdef AIMBUDDY_DEBUG_HOOKS
    struct SyntheticScene {
        int64_t sceneNs = 0;
        int count = 0;
        Detection detections[kMaxDetections];
    };
    std::mutex syntheticMutex_;
    std::deque<SyntheticScene> synthetic_;
    std::atomic<bool> syntheticMode_{false};
#endif

    std::atomic<float> inferenceMs_{0.0f};
    std::atomic<float> frameAgeMs_{0.0f};
    std::atomic<float> detectionsPerSecond_{0.0f};
    std::atomic<const char*> backend_{""};
    int64_t lastBatchNs_ = 0;
    int64_t lastLogNs_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_PIPELINE_DETECTION_PIPELINE_H
