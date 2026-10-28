#include "yolo_detector.h"

#include <ncnn/cpu.h>
#include <ncnn/gpu.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <vector>

#include "../settings.h"
#include "../utils/logger.h"

namespace aimbuddy {

namespace {

/// Network input sizes tried, in order, when the model description does not state one.
constexpr int kProbeInputSizes[] = {256, 320, 384, 416, 512, 640};

/// Runs thrown away before timing, so caches and GPU pipelines are warm.
constexpr int kBenchmarkWarmupRuns = 3;
constexpr int kBenchmarkTimedRuns = 12;

/// The GPU is shared with the game, so it must beat the CPU by a clear margin to be used.
constexpr float kGpuMustBeFasterBy = 0.80f;

/// CPU worker threads are capped so the game keeps most of the fast cores.
constexpr int kMaxCpuThreads = 3;

/// How long idle OpenMP workers spin before sleeping. The library default (200 ms) would keep
/// every worker busy between 30 to 60 Hz frames, wasting power and heat for no latency gain.
constexpr int kSpinWaitMs = 1;

/// Grey level of the synthetic image used to probe and time the network.
constexpr float kProbeGrey = 0.45f;

float elapsedMs(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - since).count();
}

/// Worker threads for CPU inference: the fast cores, but not all of them.
int cpuThreadCount() {
    const int big = ncnn::get_big_cpu_count();
    if (big > 0) {
        return std::min(big, kMaxCpuThreads);
    }
    return std::max(1, std::min(ncnn::get_cpu_count() / 2, kMaxCpuThreads));
}

/// Options that suit both backends: low memory, half precision where the hardware allows it.
void applyCommonOptions(ncnn::Option& opt) {
    opt.lightmode = true;
    opt.use_packing_layout = true;
    opt.use_fp16_packed = true;
    opt.use_fp16_storage = true;
    opt.use_fp16_arithmetic = true;
    opt.use_sgemm_convolution = true;
    opt.use_winograd_convolution = true;
    opt.openmp_blocktime = kSpinWaitMs;
    opt.flush_denormals = 3;  // tiny float values are flushed to zero, which is faster on ARM
}

/// A flat grey image of the given size, used to probe and time the network.
ncnn::Mat makeProbeImage(int size) {
    ncnn::Mat image(size, size, 3);
    image.fill(kProbeGrey);
    return image;
}

/// Pointer to the first value of a detection tensor, whether ncnn stored it as 2D or as one channel.
const float* tensorData(const ncnn::Mat& tensor) {
    return tensor.dims == 3 ? static_cast<const float*>(tensor.channel(0).data)
                            : static_cast<const float*>(tensor.data);
}

}  // namespace

bool YoloDetector::loadNet(ncnn::Net& net, const char* paramPath, const char* binPath) const {
    if (paramPath == nullptr || binPath == nullptr || paramPath[0] == '\0' || binPath[0] == '\0') {
        LOGE("There are no model files to read");
        return false;
    }
    if (net.load_param(paramPath) != 0 || net.load_model(binPath) != 0) {
        LOGE("Could not read the model files %s and %s", paramPath, binPath);
        net.clear();
        return false;
    }
    return true;
}

bool YoloDetector::runOnce(ncnn::Net& net, const ncnn::Mat& input, ncnn::Mat& output) const {
    ncnn::Extractor extractor = net.create_extractor();
    return extractor.input(inputIndex_, input) == 0 && extractor.extract(outputIndex_, output) == 0;
}

bool YoloDetector::probeOutput(ncnn::Net& net) {
    std::vector<int> sizes;
    if (profile_.inputSize > 0) {
        sizes.push_back(profile_.inputSize);
    } else {
        sizes.assign(std::begin(kProbeInputSizes), std::end(kProbeInputSizes));
    }

    // A model exported with a fixed shape only accepts its own input size, so the first size that
    // runs is the right one. A fully convolutional model accepts any size and gets the first.
    for (const int size : sizes) {
        ncnn::Mat output;
        if (!runOnce(net, makeProbeImage(size), output) || output.empty() || output.c != 1 ||
            output.elempack != 1 || output.elemsize != sizeof(float)) {
            continue;
        }
        DecodeSpec spec;
        if (!inferDecodeSpec(tensorData(output), output.h, output.w, profile_.endToEndOutput, &spec)) {
            continue;
        }
        const int anchors = spec.layout == OutputLayout::kChannelsFirst ? output.w : output.h;
        if (spec.layout != OutputLayout::kEndToEnd && inferInputSize(anchors) != size) {
            LOGW("Model output has %d anchors, unusual for a %d px input", anchors, size);
        }
        spec_ = spec;
        inputSize_ = size;
        LOGI("Model reads %dx%d input, output %dx%d, %d classes, %s coordinates", size, size, output.h,
             output.w, spec.classCount, spec.normalized ? "normalised" : "pixel");
        return true;
    }
    LOGE("The model did not produce a readable detection tensor");
    return false;
}

bool YoloDetector::load(const char* paramPath, const char* binPath, const ModelProfile& profile) {
    unload();
    profile_ = profile;
    paramPath_ = paramPath != nullptr ? paramPath : "";
    binPath_ = binPath != nullptr ? binPath : "";

    applyCommonOptions(cpuNet_.opt);
    cpuNet_.opt.use_vulkan_compute = false;
    cpuNet_.opt.num_threads = cpuThreadCount();
    if (!loadNet(cpuNet_, paramPath_.c_str(), binPath_.c_str()) || cpuNet_.input_indexes().empty() ||
        cpuNet_.output_indexes().empty()) {
        unload();
        return false;
    }
    inputIndex_ = cpuNet_.input_indexes()[0];
    outputIndex_ = cpuNet_.output_indexes()[0];

    if (!probeOutput(cpuNet_)) {
        unload();
        return false;
    }
    if (profile_.declaredClassCount == 0) {
        // No description saved with the model: guess the roles from how many classes it has.
        const bool endToEnd = profile_.endToEndOutput;
        const int inputSize = profile_.inputSize;
        profile_ = ModelProfile::defaultsFor(spec_.layout == OutputLayout::kEndToEnd ? 1 : spec_.classCount);
        profile_.endToEndOutput = endToEnd;
        profile_.inputSize = inputSize;
    } else if (spec_.layout != OutputLayout::kEndToEnd && profile_.declaredClassCount != spec_.classCount) {
        LOGW("Model has %d classes but its description lists %d", spec_.classCount, profile_.declaredClassCount);
    }

    active_ = &cpuNet_;
    backend_ = Backend::kCpu;
    loaded_ = true;
    return true;
}

void YoloDetector::unload() {
    cpuNet_.clear();
    gpuNet_.clear();
    inputPool_.clear();
    active_ = nullptr;
    loaded_ = false;
    gpuLoaded_ = false;
    backend_ = Backend::kCpu;
}

float YoloDetector::medianLatencyMs(ncnn::Net& net, const std::function<bool()>& shouldStop) {
    const ncnn::Mat probe = makeProbeImage(inputSize_);
    ncnn::Mat output;
    for (int i = 0; i < kBenchmarkWarmupRuns; ++i) {
        if (shouldStop() || !runOnce(net, probe, output)) {
            return -1.0f;
        }
    }
    std::vector<float> times;
    for (int i = 0; i < kBenchmarkTimedRuns; ++i) {
        if (shouldStop()) {
            return -1.0f;
        }
        const auto start = std::chrono::steady_clock::now();
        if (!runOnce(net, probe, output)) {
            return -1.0f;
        }
        times.push_back(elapsedMs(start));
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

bool YoloDetector::useBackend(Backend backend) {
    if (!loaded_) {
        return false;
    }
    if (backend == Backend::kVulkan) {
        if (!gpuLoaded_) {
            return false;
        }
        active_ = &gpuNet_;
    } else {
        active_ = &cpuNet_;
    }
    backend_ = backend;
    return true;
}

void YoloDetector::chooseBackend(const std::function<bool()>& shouldStop) {
    if (!loaded_) {
        return;
    }
    // Bind this thread and ncnn's CPU workers to the fast cores; the little cores stay free for the game and system.
    ncnn::set_cpu_powersave(2);
    ncnn::set_kmp_blocktime(kSpinWaitMs);

    // A stop leaves the CPU net that load() set up as the backend, and nothing will run on it anyway.
    const float cpuMs = medianLatencyMs(cpuNet_, shouldStop);
    if (shouldStop()) {
        return;
    }
    float gpuMs = -1.0f;
    if (ncnn::get_gpu_count() > 0 && !gpuLoaded_) {
        applyCommonOptions(gpuNet_.opt);
        gpuNet_.opt.use_vulkan_compute = true;
        gpuNet_.set_vulkan_device(ncnn::get_default_gpu_index());
        gpuLoaded_ = loadNet(gpuNet_, paramPath_.c_str(), binPath_.c_str());
        if (!gpuLoaded_) {
            gpuNet_.clear();
        }
    }
    if (shouldStop()) {
        return;
    }
    if (gpuLoaded_) {
        gpuMs = medianLatencyMs(gpuNet_, shouldStop);
        if (gpuMs < 0.0f) {
            gpuNet_.clear();
            gpuLoaded_ = false;
        }
    }
    if (shouldStop()) {
        return;
    }

    const bool useGpu = gpuLoaded_ && cpuMs > 0.0f && gpuMs < cpuMs * kGpuMustBeFasterBy;
    useBackend(useGpu ? Backend::kVulkan : Backend::kCpu);
    LOGI("Backend: %s (CPU %.1f ms with %d threads, GPU %.1f ms)", backendName(), cpuMs,
         cpuNet_.opt.num_threads, gpuMs);
}

bool YoloDetector::detect(const RoiFrame& frame, DetectionBatch& batch) {
    if (!loaded_ || active_ == nullptr) {
        return false;
    }
    const int side = frame.roi.side;
    if (side <= 0 || frame.rgba.size() < static_cast<size_t>(side) * static_cast<size_t>(side) * 4) {
        return false;
    }
    const auto start = std::chrono::steady_clock::now();

    // One pass converts RGBA to RGB and resizes the region to the network input size.
    ncnn::Mat input = ncnn::Mat::from_pixels_resize(frame.rgba.data(), ncnn::Mat::PIXEL_RGBA2RGB, side, side,
                                                    side * 4, inputSize_, inputSize_, &inputPool_);
    static const float kMean[3] = {0.0f, 0.0f, 0.0f};
    static const float kScale[3] = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
    input.substract_mean_normalize(kMean, kScale);

    ncnn::Mat output;
    if (!runOnce(*active_, input, output) || output.empty() || output.elempack != 1 ||
        output.elemsize != sizeof(float)) {
        return false;
    }

    DecodeParams params;
    params.minScore = minScore_.load(std::memory_order_relaxed);
    params.modelSize = inputSize_;
    ModelBox boxes[kMaxDetections];
    const int count =
        decodeDetections(tensorData(output), output.h, output.w, spec_, params, profile_, boxes, kMaxDetections);

    const float screenPerCapture = frame.geometry.screenPerCapture();
    batch.count = count;
    for (int i = 0; i < count; ++i) {
        batch.detections[i] = mapToScreen(boxes[i], frame.roi, frame.geometry, inputSize_);
    }
    batch.scanned.left = static_cast<float>(frame.roi.x) * screenPerCapture;
    batch.scanned.top = static_cast<float>(frame.roi.y) * screenPerCapture;
    batch.scanned.right = static_cast<float>(frame.roi.x + frame.roi.side) * screenPerCapture;
    batch.scanned.bottom = static_cast<float>(frame.roi.y + frame.roi.side) * screenPerCapture;
    batch.captureNs = frame.captureTimeNs;
    batch.inferenceMs = elapsedMs(start);
    return true;
}

}  // namespace aimbuddy
