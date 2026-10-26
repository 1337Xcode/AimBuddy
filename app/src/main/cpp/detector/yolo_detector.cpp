#include "yolo_detector.h"
#include <chrono>
#include <algorithm>
#include <array>

namespace ESP {

YoloDetector::YoloDetector()
    : vulkanDevice_(nullptr)
    , initialized_(false)
    , confidenceThreshold_(Config::DEFAULT_CONFIDENCE_THRESHOLD) {
    LOGD("YoloDetector created");
}

YoloDetector::~YoloDetector() {
    shutdown();
}

bool YoloDetector::initialize(AAssetManager* assetManager,
                              const char* modelParamPath,
                              const char* modelBinPath) {
    if (initialized_) {
        LOGW("YoloDetector already initialized");
        return true;
    }
    
    if (!assetManager) {
        LOGE("AssetManager is null");
        return false;
    }
    
    LOGI("Initializing YoloDetector");

    // Initialize Vulkan if available
    int gpuCount = ncnn::get_gpu_count();
    LOGI("NCNN GPU count: %d", gpuCount);
    
    if (gpuCount > 0 && Config::NCNN_USE_VULKAN_COMPUTE) {
        vulkanDevice_ = ncnn::get_gpu_device(0);
        if (vulkanDevice_) {
            net_.set_vulkan_device(vulkanDevice_);
            LOGI("Vulkan device set: %s", vulkanDevice_->info.device_name());
        } else {
            LOGW("Failed to get Vulkan device, falling back to CPU");
        }
    } else {
        LOGI("Vulkan not available or disabled, using CPU");
    }
    
    // Configure NCNN options for Adreno 660
    net_.opt.use_vulkan_compute = (vulkanDevice_ != nullptr);
    net_.opt.use_fp16_packed = Config::NCNN_USE_FP16_PACKED;
    net_.opt.use_fp16_storage = Config::NCNN_USE_FP16_STORAGE;
    net_.opt.use_fp16_arithmetic = Config::NCNN_USE_FP16_ARITHMETIC;
    net_.opt.use_packing_layout = Config::NCNN_USE_PACKING_LAYOUT;
    net_.opt.use_sgemm_convolution = true;  // Optimized matrix multiplication
    net_.opt.use_winograd_convolution = true;  // Fast convolution algorithm
    net_.opt.lightmode = Config::NCNN_LIGHT_MODE;
    net_.opt.num_threads = Config::NCNN_NUM_THREADS;
    
    LOGI("NCNN options: vulkan=%d, fp16_storage=%d, fp16_arith=%d, threads=%d",
         net_.opt.use_vulkan_compute, net_.opt.use_fp16_storage,
         net_.opt.use_fp16_arithmetic, net_.opt.num_threads);
    
    int ret = -1;
    if (modelParamPath && modelBinPath && modelParamPath[0] != '\0' && modelBinPath[0] != '\0') {
        LOGI("Trying local model files: %s / %s", modelParamPath, modelBinPath);
        ret = net_.load_param(modelParamPath);
        if (ret == 0) {
            ret = net_.load_model(modelBinPath);
        }

        if (ret == 0) {
            LOGI("Loaded model from local storage");
        } else {
            LOGW("Local model load failed (error %d), falling back to assets", ret);
        }
    }

    if (ret != 0) {
        ret = net_.load_param(assetManager, Config::MODEL_PARAM_FILE);
        if (ret != 0) {
            LOGE("Failed to load model param: %s (error %d)", Config::MODEL_PARAM_FILE, ret);
            return false;
        }
        LOGI("Loaded model param: %s", Config::MODEL_PARAM_FILE);

        ret = net_.load_model(assetManager, Config::MODEL_BIN_FILE);
        if (ret != 0) {
            LOGE("Failed to load model bin: %s (error %d)", Config::MODEL_BIN_FILE, ret);
            return false;
        }
        LOGI("Loaded model bin: %s", Config::MODEL_BIN_FILE);
    }

    // Cache input/output blob names when available to avoid repeated lookup warnings
#if NCNN_STRING
    const auto& inputNames = net_.input_names();
    if (!inputNames.empty() && inputNames[0]) {
        inputBlobName_ = inputNames[0];
    }

    const auto& outputNames = net_.output_names();
    if (!outputNames.empty() && outputNames[0]) {
        outputBlobName_ = outputNames[0];
    }
#endif
    
    // Pre-allocate input mat
    inputMat_.create(Config::MODEL_INPUT_SIZE, Config::MODEL_INPUT_SIZE, 3);
    
    initialized_ = true;
    LOGI("YoloDetector initialized successfully");
    return true;
}

void YoloDetector::shutdown() {
    if (!initialized_) {
        return;
    }
    
    LOGI("Shutting down YoloDetector");
    net_.clear();
    vulkanDevice_ = nullptr;
    initialized_ = false;
}

bool YoloDetector::detect(const aimbuddy::RoiFrame& frame, DetectionResult& result) {
    if (!initialized_) {
        LOGE("Detector not initialized");
        return false;
    }
    if (frame.roi.side <= 0 || frame.rgba.size() < static_cast<size_t>(frame.roi.side) * frame.roi.side * 4) {
        LOGE("Region frame is empty");
        return false;
    }

    result.clear();
    const auto startTime = std::chrono::steady_clock::now();

    preprocess(frame, inputMat_);

    ncnn::Mat output;
    if (!runInference(inputMat_, output)) {
        LOGE("Inference failed");
        return false;
    }

    postprocess(output, frame, result);

    const auto endTime = std::chrono::steady_clock::now();
    result.inferenceTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    {
        std::scoped_lock lock(resultMutex_);
        latestResult_ = result;
    }

    LOGP("Detection: %d boxes in %.2f ms", result.boxes.size(), result.inferenceTimeMs);
    return true;
}

DetectionResult YoloDetector::getResult() const {
    std::scoped_lock lock(resultMutex_);
    return latestResult_;
}

void YoloDetector::preprocess(const aimbuddy::RoiFrame& frame, ncnn::Mat& inputMat) {
    const int side = frame.roi.side;

    // One pass converts RGBA to RGB and resizes the region to the model input size.
    inputMat = ncnn::Mat::from_pixels_resize(
        frame.rgba.data(),
        ncnn::Mat::PIXEL_RGBA2RGB,
        side, side,
        side * 4,  // row stride in bytes: the region is tightly packed
        Config::MODEL_INPUT_SIZE, Config::MODEL_INPUT_SIZE);

    // Scale pixel values from [0, 255] to [0, 1], the range the model was trained on.
    const std::array<float, 3> meanVals = {0.0f, 0.0f, 0.0f};
    const std::array<float, 3> normVals = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
    inputMat.substract_mean_normalize(meanVals.data(), normVals.data());
}

bool YoloDetector::runInference(const ncnn::Mat& input, ncnn::Mat& output) {
    ncnn::Extractor ex = net_.create_extractor();

    // Set input using cached name if available, otherwise fallback to index 0
    int ret = -1;
    if (!useInputIndex_ && !inputBlobName_.empty()) {
        ret = ex.input(inputBlobName_.c_str(), input);
    }

    if (ret != 0) {
        useInputIndex_ = true;
        ret = ex.input(0, input);
    }

    if (ret != 0) {
        LOGE("Failed to set input: %d", ret);
        return false;
    }

    // Get output using cached name if available, otherwise fallback to index 0
    ret = -1;
    if (!useOutputIndex_ && !outputBlobName_.empty()) {
        ret = ex.extract(outputBlobName_.c_str(), output);
    }

    if (ret != 0) {
        useOutputIndex_ = true;
        ret = ex.extract(0, output);
    }

    if (ret != 0) {
        LOGE("Failed to extract output: %d", ret);
        return false;
    }

    return true;
}

void YoloDetector::postprocess(const ncnn::Mat& output, const aimbuddy::RoiFrame& frame, DetectionResult& result) {
    result.boxes.clear(); // Fix ghosting: Clear previous frame detections
    float confThreshold = confidenceThreshold_.load(std::memory_order_relaxed);
    
    int numBoxes = output.h;
    int numValues = output.w;
    bool transposed = false; 

    if (numBoxes < numValues) {
        transposed = true;
        std::swap(numBoxes, numValues);
    }
    
    // Model pixels -> region pixels -> capture pixels -> screen pixels, folded into one scale and offset.
    // The capture and screen grids share one aspect ratio, so a single factor serves both axes.
    const float modelSize = static_cast<float>(Config::MODEL_INPUT_SIZE);
    const float regionPerModel = static_cast<float>(frame.roi.side) / modelSize;
    const float screenPerCapture = frame.geometry.screenPerCapture();
    const float scaleX = regionPerModel * screenPerCapture;
    const float scaleY = scaleX;
    const float offsetX = static_cast<float>(frame.roi.x) * screenPerCapture;
    const float offsetY = static_cast<float>(frame.roi.y) * screenPerCapture;

    int numClasses = numValues - 4;
    int classOffset = 4;
    float objectness = 1.0f;
    
    // Auto-detect YOLO version
    if (numClasses == Config::NUM_CLASSES + 1) {
        classOffset = 5;
        numClasses -= 1; 
    }
    if (numClasses < 1) numClasses = 1;

    // Optimized path: Split loops for Transposed vs Non-Transposed to enable SIMD
    if (transposed) {
        const float* row0 = output.row(0);
        const float* row1 = output.row(1);
        const float* row2 = output.row(2);
        const float* row3 = output.row(3);
        const float* rowObj = (classOffset > 4) ? output.row(4) : nullptr;
        
        for (int i = 0; i < numBoxes; ++i) {
            if (result.boxes.full()) break;

            float maxClassProb = 0.0f;
            int bestClassId = 0;
            
            if (classOffset > 4) objectness = rowObj[i];
            
            // Unrolling class loop slightly helpful, but dynamic count prevents full unroll
            for (int c = 0; c < numClasses; ++c) {
                float prob = output.row(classOffset + c)[i];
                prob *= objectness;
                if (prob > maxClassProb) {
                    maxClassProb = prob;
                    bestClassId = c;
                }
            }
            
            if (maxClassProb < confThreshold) continue;
            if (Config::FILTER_ENEMY_ONLY && bestClassId != Config::ENEMY_CLASS_ID) continue;

            float xCenter = row0[i];
            float yCenter = row1[i];
            float width = row2[i];
            float height = row3[i];

            // Normalize if needed (heuristic based on value range)
            if (xCenter <= 1.5f) {
                xCenter *= modelSize;
                yCenter *= modelSize;
                width *= modelSize;
                height *= modelSize;
            }

            // Optimized coordinate transform (Fused Multiply-Add)
            // box.x = (xCenter - width/2) * scaleX + offsetX
            float halfW = width * 0.5f;
            float halfH = height * 0.5f;
            
            float boxX = (xCenter - halfW) * scaleX + offsetX;
            float boxY = (yCenter - halfH) * scaleY + offsetY;
            float boxW = width * scaleX;
            float boxH = height * scaleY;

            if (boxW <= 0.0f || boxH <= 0.0f) continue;
            
            BoundingBox box;
            box.x = boxX;
            box.y = boxY;
            box.width = boxW;
            box.height = boxH;
            box.confidence = maxClassProb;
            box.classId = bestClassId;
            
            result.boxes.push(box);
        }
    } else {
        // Non-transposed path (standard NCNN)
        for (int i = 0; i < numBoxes; ++i) {
            if (result.boxes.full()) break;

            const float* values = output.row(i);
            
            float maxClassProb = 0.0f;
            int bestClassId = 0;

            if (classOffset > 4) objectness = values[4];
            
            for (int c = 0; c < numClasses; ++c) {
                float prob = values[classOffset + c];
                prob *= objectness;
                if (prob > maxClassProb) {
                    maxClassProb = prob;
                    bestClassId = c;
                }
            }
            
            if (maxClassProb < confThreshold) continue;
            if (Config::FILTER_ENEMY_ONLY && bestClassId != Config::ENEMY_CLASS_ID) continue;

            float xCenter = values[0];
            float yCenter = values[1];
            float width = values[2];
            float height = values[3];

            if (xCenter <= 1.5f) {
                xCenter *= modelSize;
                yCenter *= modelSize;
                width *= modelSize;
                height *= modelSize;
            }

            float halfW = width * 0.5f;
            float halfH = height * 0.5f;
            
            float boxX = (xCenter - halfW) * scaleX + offsetX;
            float boxY = (yCenter - halfH) * scaleY + offsetY;
            float boxW = width * scaleX;
            float boxH = height * scaleY;

            if (boxW <= 0.0f || boxH <= 0.0f) continue;

            BoundingBox box;
            box.x = boxX;
            box.y = boxY;
            box.width = boxW;
            box.height = boxH;
            box.confidence = maxClassProb;
            box.classId = bestClassId;
            
            result.boxes.push(box);
        }
    }
    
    if (result.boxes.size() > 1) {
        applyNMS(result.boxes);
    }
}

void YoloDetector::applyNMS(DetectionArray& boxes) {
    int count = boxes.size();
    if (count <= 1) return;
    
    boxes.sort([](const BoundingBox& a, const BoundingBox& b) {
        return a.confidence > b.confidence;
    });
    
    std::array<bool, Config::MAX_DETECTIONS> suppressed{};
    int finalCount = 0;
    
    for (int i = 0; i < count; ++i) {
        if (suppressed[i]) continue;
        
        if (i != finalCount) {
             boxes[finalCount] = boxes[i];
        }
        finalCount++;
        
        for (int j = i + 1; j < count; ++j) {
            if (!suppressed[j]) {
                float iou = boxes[i].iou(boxes[j]);
                if (iou > Config::NMS_IOU_THRESHOLD) {
                    suppressed[j] = true;
                }
            }
        }
    }
    
    // Compact FixedArray by recreating it (stack copy, zero allocation)
    DetectionArray newBoxes;
    for (int i = 0; i < finalCount; ++i) {
        newBoxes.push(boxes[i]);
    }
    // Assign back (copy data)
    boxes = newBoxes;
}

} // namespace ESP