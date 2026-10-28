#ifndef AIMBUDDY_PIPELINE_DETECTION_PIPELINE_H
#define AIMBUDDY_PIPELINE_DETECTION_PIPELINE_H

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../capture/roi_capture.h"
#include "../core/camera_motion.h"
#include "../core/colour_blobs.h"
#include "../core/detection_batch.h"
#include "../core/frame_thinner.h"
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

    DetectionPipeline();
    ~DetectionPipeline() { stop(); }
    DetectionPipeline(const DetectionPipeline&) = delete;
    DetectionPipeline& operator=(const DetectionPipeline&) = delete;

    /// What the colour detector looks for, as the player has set it.
    struct ColourTarget {
        bool learned = false;          ///< A colour has been learned; without one nothing is found.
        ColourRange range;
        float minHeightScreenPx = 40.0f;   ///< Patches shorter than this are not targets.
    };

    /// What came of the latest request to learn the colour under the crosshair.
    enum class ColourLearning { kNone, kLearned, kNotFound };

    /// Told, on the pipeline thread, what the colour under the crosshair is (or that there was none).
    using ColourSink = std::function<void(bool found, const ColourRange& range)>;

    /// Loads the model. Must not be called while running. Returns false if the model is unusable.
    bool loadModel(const std::string& paramPath, const std::string& binPath, const ModelProfile& profile);
    bool isModelLoaded() const { return detector_.isLoaded(); }

    /**
     * Chooses what finds the targets: the loaded model (the default) or a colour, which needs no model.
     * Must not be called while running; the choice is taken when the pipeline starts.
     */
    void setColourDetection(bool on);
    bool detectsByColour() const { return colourMode_.load(std::memory_order_acquire); }

    /// True when there is something to detect with: the model is loaded, or the colour detector is chosen.
    bool canStart() const { return detectsByColour() || detector_.isLoaded(); }

    /// What the colour detector looks for. Thread safe.
    void setColourTarget(const ColourTarget& target);

    /// Asks for the colour at the crosshair to be learned from the next frame. Thread safe.
    void requestColourLearning() {
        learningResult_.store(static_cast<int>(ColourLearning::kNone), std::memory_order_release);
        learnRequested_.store(true, std::memory_order_release);
    }
    ColourLearning colourLearning() const { return static_cast<ColourLearning>(learningResult_.load(std::memory_order_acquire)); }

    /// Sets where a learned colour is reported. Must be called while stopped.
    void setColourSink(ColourSink sink) { colourSink_ = std::move(sink); }

    /// Starts the pipeline thread. Does nothing if it is running or there is nothing to detect with.
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

    /// The most detections a second to run, for when the phone is getting hot. Zero or less lifts the limit. Thread safe.
    /// The camera is looked at no more often than that either.
    void setMaxRate(float perSecond) {
        thinner_.setMaxRate(perSecond);
        maxRate_.store(perSecond, std::memory_order_relaxed);
    }

    /// What each class of the loaded model means; one body class when a colour is looked for.
    const ModelProfile& profile() const { return profile_; }

#ifdef AIMBUDDY_DEBUG_HOOKS
    /// Testing only: with this on, targets come from a test scene (see feedSynthetic) instead of the model.
    void setSyntheticMode(bool on) { syntheticMode_.store(on, std::memory_order_release); }

    /// Testing only: the scene's true targets, in screen pixels, as they are at `sceneNs` (steady clock).
    void feedSynthetic(const Detection* detections, int count, int64_t sceneNs);

    /// Testing only: starts keeping every measurement of how the picture slid between frames.
    void startMotionTrace();

    /// Testing only: writes the measurements kept since startMotionTrace to `path` as CSV and stops keeping them.
    /// The reduced pictures the measurements were made from go to `path` + ".grey" as raw floats, for replaying them.
    bool writeMotionTrace(const std::string& path);

    /// Testing only: the next frame the detector's region is copied from is written to `path` as a PPM picture,
    /// exactly as the detector will see it (the app's own overlay included).
    void requestFrameDump(const std::string& path);
#endif

    RoiCapture& capture() { return capture_; }
    Tracker& tracker() { return tracker_; }
    Stats stats() const;

private:
    void run();
    void applyConfidenceIfChanged(float& applied);
    bool detectByColour(const RoiFrame& frame, DetectionBatch& batch);
    void learnColourFrom(const RoiFrame& frame);
    void measureCameraMotion(const RoiFrame& frame);
#ifdef AIMBUDDY_DEBUG_HOOKS
    bool detectSynthetic(const RoiFrame& frame, DetectionBatch& batch);
    void dumpFrameIfAsked(const RoiFrame& frame);
#endif
    void recordStats(const DetectionBatch& batch, int64_t finishedNs);

    RoiCapture capture_;
    FrameThinner thinner_;
    YoloDetector detector_;
    ModelProfile profile_;                ///< What the detections mean; the model's, or one body class for a colour. Fixed while running.
    ColourBlobFinder colourFinder_;       ///< Pipeline thread only.
    std::atomic<bool> colourMode_{false};
    std::mutex colourMutex_;              ///< Guards colourTarget_, which the capture thread sets and the pipeline thread reads.
    ColourTarget colourTarget_;
    std::atomic<bool> learnRequested_{false};
    std::atomic<int> learningResult_{static_cast<int>(ColourLearning::kNone)};
    ColourSink colourSink_;
    Tracker tracker_;
    BatchObserver observer_;
    CameraMotion* cameraMotion_ = nullptr;
    std::mutex motionMutex_;              ///< Guards everything about reading the picture's slide: it runs on the capture thread, the log line on the pipeline's.
    GlobalMotionEstimator sceneMotion_;   ///< Reads how the picture slid between frames.
    int64_t lastFrameNs_ = 0;             ///< The last frame it was read on.
    float slideSum_ = 0.0f;               ///< Over the frames since the last log line: summed slide (screen pixels)...
    float qualitySum_ = 0.0f;             ///< ...summed quality...
    int slideFrames_ = 0;                 ///< ...and how many frames that is.
    std::atomic<float> maxRate_{0.0f};
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
    std::mutex dumpMutex_;
    std::string dumpPath_;   ///< Where the next frame is to be written; empty when none is wanted.

    /// Boxes kept with each recorded measurement.
    static constexpr int kTracedMovers = 8;

    /// One measurement of the picture's slide between two frames, in screen pixels.
    struct MotionRow {
        int64_t fromNs = 0;
        int64_t toNs = 0;
        float dx = 0.0f;
        float dy = 0.0f;
        float quality = 0.0f;
        int movers = 0;   ///< How many detected enemies were left out of the measurement.
        std::array<MovingBox, kTracedMovers> boxes{};   ///< The first few of them, in pixels of the reduced picture.
        std::array<float, GlobalMotionEstimator::kGrid * GlobalMotionEstimator::kGrid> grey{};   ///< The frame the slide ends at, reduced.
    };
    std::mutex motionTraceMutex_;
    std::vector<MotionRow> motionTrace_;
    std::atomic<bool> tracingMotion_{false};
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
