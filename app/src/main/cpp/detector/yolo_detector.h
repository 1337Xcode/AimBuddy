#ifndef ESP_YOLO_DETECTOR_H
#define ESP_YOLO_DETECTOR_H

#include <atomic>
#include <memory>
#include <string>
#include <android/asset_manager.h>
#include <ncnn/net.h>
#include <ncnn/gpu.h>
#include "bounding_box.h"
#include "../capture/roi_capture.h"
#include "../settings.h"
#include "../utils/logger.h"
#include "../utils/memory_pool.h"

/**
 * @file yolo_detector.h
 * @brief NCNN-based YOLOv26n inference engine with Vulkan acceleration
 * 
 * Handles model loading, preprocessing, inference, and post-processing.
 * Optimized for Adreno 660 GPU with FP16 storage.
 */

namespace ESP {

/**
 * @struct DetectionResult
 * @brief Container for detection results (pre-allocated, no heap allocation)
 */
struct DetectionResult {
    DetectionArray boxes;  // Use FixedArray for zero-allocation, auto-sized list
    float inferenceTimeMs;
    
    DetectionResult() : inferenceTimeMs(0.0f) {}
    
    void clear() {
        boxes.clear();
        inferenceTimeMs = 0.0f;
    }
};

/**
 * @class YoloDetector
 * @brief YOLOv26n detector with NCNN Vulkan backend
 */
class YoloDetector {
public:
    YoloDetector();
    ~YoloDetector();
    
    // Non-copyable
    YoloDetector(const YoloDetector&) = delete;
    YoloDetector& operator=(const YoloDetector&) = delete;
    
    /**
     * @brief Initialize detector with model from assets
     * @param assetManager Android AssetManager for loading model files
     * @return true if initialization successful
     */
    bool initialize(AAssetManager* assetManager,
                    const char* modelParamPath = nullptr,
                    const char* modelBinPath = nullptr);
    
    /**
     * @brief Shutdown detector and release resources
     */
    void shutdown();
    
    /**
     * @brief Run detection on the centre region copied out of one captured frame
     * @param frame Region pixels plus where they came from, so boxes land in screen space
     * @param result Output detection result (pre-allocated)
     * @return true if detection successful
     */
    bool detect(const aimbuddy::RoiFrame& frame, DetectionResult& result);

    /**
     * @brief Set confidence threshold
     * @param threshold Confidence threshold (0.3 - 0.9)
     */
    void setConfidenceThreshold(float threshold) {
        confidenceThreshold_.store(threshold, std::memory_order_relaxed);
    }
    
    /**
     * @brief Get current confidence threshold
     * @return Current threshold
     */
    float getConfidenceThreshold() const {
        return confidenceThreshold_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Check if detector is initialized
     * @return true if ready for inference
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief Get the latest detection result thread-safely
     * @return Copy of the latest result
     */
    DetectionResult getResult() const;

private:
    /**
     * @brief Resize the region to the model input size and normalise it to [0, 1]
     * @param frame Region copied out of the capture
     * @param inputMat Output NCNN mat ready for inference
     */
    void preprocess(const aimbuddy::RoiFrame& frame, ncnn::Mat& inputMat);
    
    /**
     * @brief Run NCNN inference
     * @param input Preprocessed input mat
     * @param output Raw output from network
     * @return true if inference successful
     */
    bool runInference(const ncnn::Mat& input, ncnn::Mat& output);
    
    /**
     * @brief Post-process: decode boxes, apply NMS, map coordinates to screen space
     * @param output Raw network output
     * @param frame Region the output was computed from
     * @param result Output detection result
     */
    void postprocess(const ncnn::Mat& output, const aimbuddy::RoiFrame& frame, DetectionResult& result);
    
    /**
     * @brief Apply Non-Maximum Suppression
     * @param boxes Input boxes
     * @param result Output filtered boxes
     */
    void applyNMS(DetectionArray& boxes);
    
    ncnn::Net net_;
    ncnn::VulkanDevice* vulkanDevice_;
    bool initialized_;
    
    // Configuration
    std::atomic<float> confidenceThreshold_;
    
    // Cached blob names to avoid repeated lookup warnings
    std::string inputBlobName_;
    std::string outputBlobName_;
    bool useInputIndex_ = false;
    bool useOutputIndex_ = false;
    
    // Result storage
    mutable std::mutex resultMutex_;
    DetectionResult latestResult_;
    
    // Pre-allocated buffers (avoid heap allocation in hot path)
    ncnn::Mat inputMat_;
};

} // namespace ESP

#endif // ESP_YOLO_DETECTOR_H
