#ifndef AIMBUDDY_DETECTOR_YOLO_DETECTOR_H
#define AIMBUDDY_DETECTOR_YOLO_DETECTOR_H

#include <android/asset_manager.h>
#include <ncnn/allocator.h>
#include <ncnn/net.h>

#include <atomic>
#include <string>

#include "../capture/roi_capture.h"
#include "../core/detection_batch.h"
#include "../core/detection_decoder.h"
#include "../core/model_profile.h"

namespace aimbuddy {

/**
 * Runs the user's detection model on the centre region of each captured frame.
 *
 * The model is an NCNN export of a YOLO-style detector. Because users train their own models,
 * nothing about the model is assumed beyond what it tells us: the input size, the output layout
 * and the coordinate units are worked out when it loads, and what each class means comes from
 * the ModelProfile.
 *
 * Phones differ a lot, so the detector loads the model for both the CPU and (if present) the
 * GPU and, on the inference thread, times both and keeps the faster. The GPU is shared with the
 * game, so it must be clearly faster to be chosen.
 *
 * All methods except setMinScore() must be called from a single thread at a time.
 */
class YoloDetector {
public:
    enum class Backend { kCpu, kVulkan };

    YoloDetector() = default;
    ~YoloDetector() { unload(); }
    YoloDetector(const YoloDetector&) = delete;
    YoloDetector& operator=(const YoloDetector&) = delete;

    /**
     * Loads the model, from the given files if both are set and readable, otherwise from the app's
     * bundled assets. Works out the network input size and the output layout with a trial run.
     * Returns false, leaving the detector unloaded, if the model cannot be used.
     */
    bool load(AAssetManager* assets, const char* paramPath, const char* binPath, const ModelProfile& profile);

    /// Releases the model and every buffer it holds.
    void unload();

    bool isLoaded() const { return loaded_; }

    /**
     * Times the model on the CPU and on the GPU and keeps the faster. Call it on the thread that
     * will run detect(): it also pins that thread, and the CPU worker threads, to the fast cores.
     */
    void chooseBackend();

    /**
     * Detects targets in one captured region.
     *
     * Fills `batch` with boxes in screen pixels, strongest first. Returns false if the model is
     * not loaded or the network failed.
     */
    bool detect(const RoiFrame& frame, DetectionBatch& batch);

    /// Lowest score worth reporting; weaker candidates are dropped while decoding. Thread safe.
    void setMinScore(float score) { minScore_.store(score, std::memory_order_relaxed); }

    /// Width and height of the network input, in pixels.
    int inputSize() const { return inputSize_; }

    /// What each class means, with defaults filled in for a model that came without a description.
    const ModelProfile& profile() const { return profile_; }

    /**
     * Switches to an already loaded backend, for example back to the CPU when the phone gets hot.
     * Returns false if that backend is not available.
     */
    bool useBackend(Backend backend);

    Backend backend() const { return backend_; }
    const char* backendName() const { return backend_ == Backend::kVulkan ? "GPU (Vulkan)" : "CPU"; }

private:
    bool loadNet(ncnn::Net& net, const char* paramPath, const char* binPath) const;
    bool runOnce(ncnn::Net& net, const ncnn::Mat& input, ncnn::Mat& output) const;
    bool probeOutput(ncnn::Net& net);
    float medianLatencyMs(ncnn::Net& net);

    ncnn::Net cpuNet_;
    ncnn::Net gpuNet_;
    ncnn::Net* active_ = nullptr;
    ncnn::UnlockedPoolAllocator inputPool_;  ///< Recycles the per-frame input image instead of reallocating it.

    bool loaded_ = false;
    bool gpuLoaded_ = false;
    Backend backend_ = Backend::kCpu;
    int inputIndex_ = 0;
    int outputIndex_ = 0;
    int inputSize_ = 0;
    AAssetManager* assets_ = nullptr;  ///< Owned by the Java activity; only used while loading.
    std::string paramPath_;
    std::string binPath_;
    ModelProfile profile_;
    DecodeSpec spec_;
    std::atomic<float> minScore_{0.12f};
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_DETECTOR_YOLO_DETECTOR_H
