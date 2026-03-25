#ifndef ESP_YOLO_DETECTOR_H
#define ESP_YOLO_DETECTOR_H

#include <atomic>
#include <memory>
#include <string>
#include <android/asset_manager.h>
#include <android/hardware_buffer.h>
#include <ncnn/net.h>
#include <ncnn/gpu.h>
#include "bounding_box.h"
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
     * @param screenWidth Full screen width for coordinate mapping
     * @param screenHeight Full screen height for coordinate mapping
     * @return true if initialization successful
     */
    bool initialize(AAssetManager* assetManager, int screenWidth, int screenHeight);
    
    /**
     * @brief Shutdown detector and release resources
     */
    void shutdown();
    
    /**
     * @brief Run detection on hardware buffer
     * @param buffer AHardwareBuffer from screen capture
     * @param result Output detection result (pre-allocated)
     * @return true if detection successful
     */
    bool detect(AHardwareBuffer* buffer, DetectionResult& result);
    bool detect(AHardwareBuffer* buffer, DetectionResult& result, int dynamicCropSize);

    /**
     * @brief Update screen size for coordinate mapping
     * @param screenWidth Current screen width in pixels
     * @param screenHeight Current screen height in pixels
     */
    void setScreenSize(int screenWidth, int screenHeight) {
        screenWidth_.store(screenWidth, std::memory_order_relaxed);
        screenHeight_.store(screenHeight, std::memory_order_relaxed);
    }
    
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
     * @brief Preprocess image: center crop and resize
     * @param buffer Input hardware buffer
     * @param inputMat Output NCNN mat ready for inference
     * @return true if preprocessing successful
     */
    bool preprocess(AHardwareBuffer* buffer, ncnn::Mat& inputMat, int cropSize = Config::CROP_SIZE);
    
    /**
     * @brief Run NCNN inference
     * @param input Preprocessed input mat
     * @param output Raw output from network
     * @return true if inference successful
     */
    bool runInference(const ncnn::Mat& input, ncnn::Mat& output);
    
    /**
     * @brief Post-process: decode boxes, apply NMS, map coordinates
     * @param output Raw network output
     * @param result Output detection result
     */
    void postprocess(const ncnn::Mat& output, DetectionResult& result, int cropSize = Config::CROP_SIZE);
    
    /**
     * @brief Apply Non-Maximum Suppression
     * @param boxes Input boxes
     * @param result Output filtered boxes
     */
    void applyNMS(DetectionArray& boxes);
    
    ncnn::Net net_;
    ncnn::VulkanDevice* vulkanDevice_;
    bool initialized_;
    
    // Screen dimensions for coordinate mapping
    std::atomic<int> screenWidth_;
    std::atomic<int> screenHeight_;
    
    // Configuration
    std::atomic<float> confidenceThreshold_;
    
    // Runtime state
    int currentCropX_ = 0;
    int currentCropY_ = 0;
    int currentCaptureWidth_ = 0;
    int currentCaptureHeight_ = 0;

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
