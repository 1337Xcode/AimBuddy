#include "detection_pipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "../utils/logger.h"

namespace aimbuddy {

namespace {

/// How long the pipeline thread waits for a frame before checking whether it should stop.
constexpr std::chrono::milliseconds kFrameWait{50};

/// Weight of the newest sample in the smoothed statistics (about the last 20 samples).
constexpr float kStatsSmoothing = 0.05f;

/// How often debug builds print the statistics to the log.
constexpr int64_t kStatsLogIntervalNs = 5'000'000'000;

#ifdef AIMBUDDY_DEBUG_HOOKS
/// A test scene is shown on screen about this long after it is drawn, and no more than this many are kept.
constexpr int64_t kSyntheticScreenLatencyNs = 8'000'000;
constexpr size_t kSyntheticBacklog = 16;

/// Rows reserved for a motion trace: a couple of minutes of frames.
constexpr size_t kMotionTraceReserve = 8000;
#endif

int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Blends a new sample into a smoothed value; the first sample is taken as is.
float smooth(float current, float sample) {
    return current <= 0.0f ? sample : current + (sample - current) * kStatsSmoothing;
}

}  // namespace

bool DetectionPipeline::loadModel(AAssetManager* assets, const std::string& paramPath, const std::string& binPath,
                                  const ModelProfile& profile) {
    if (isRunning()) {
        LOGE("Cannot load a model while the pipeline is running");
        return false;
    }
    if (!detector_.load(assets, paramPath.c_str(), binPath.c_str(), profile)) {
        return false;
    }
    capture_.setModelSize(detector_.inputSize());
    return true;
}

void DetectionPipeline::start() {
    if (isRunning() || !detector_.isLoaded()) {
        return;
    }
    capture_.mailbox().reopen();
    tracker_.reset();
    sceneMotion_.reset();
    lastBatchNs_ = 0;
    lastFrameNs_ = 0;
    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { run(); });
}

void DetectionPipeline::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel) && !worker_.joinable()) {
        return;
    }
    capture_.mailbox().close();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void DetectionPipeline::applyConfidenceIfChanged(float& applied) {
    const float requested = confidence_.load(std::memory_order_relaxed);
    if (std::fabs(requested - applied) < 1e-4f) {
        return;
    }
    const TrackerConfig config = TrackerConfig::forConfidence(requested);
    tracker_.setConfig(config);
    detector_.setMinScore(config.lowScore);
    applied = requested;
}

/**
 * Measures how the picture slid since the previous frame. The enemies the detector found in this
 * frame (if it was run) move by themselves, so they are left out: a large one crossing the middle
 * of the view would otherwise be taken for the camera turning.
 */
void DetectionPipeline::measureCameraMotion(const RoiFrame& frame, const DetectionBatch* detections) {
    if (cameraMotion_ == nullptr) {
        return;
    }
    MovingBox movers[kMaxDetections];
    int moverCount = 0;
    if (detections != nullptr) {
        // Detections are in screen pixels; the odometry works in pixels of the region it is given.
        const float captureOfScreen = 1.0f / frame.geometry.screenPerCapture();
        for (int i = 0; i < detections->count && moverCount < kMaxDetections; ++i) {
            const Detection& d = detections->detections[i];
            movers[moverCount++] = {d.x * captureOfScreen - static_cast<float>(frame.roi.x),
                                    d.y * captureOfScreen - static_cast<float>(frame.roi.y),
                                    (d.x + d.width) * captureOfScreen - static_cast<float>(frame.roi.x),
                                    (d.y + d.height) * captureOfScreen - static_cast<float>(frame.roi.y)};
        }
    }
    const GlobalShift shift = sceneMotion_.push(frame.rgba.data(), frame.roi.side, frame.roi.side * 4, movers, moverCount);
    if (lastFrameNs_ > 0 && frame.captureTimeNs > lastFrameNs_) {
        // The measurement is in capture pixels; everything else works in screen pixels.
        const float scale = frame.geometry.screenPerCapture();
        cameraMotion_->addMeasurement(lastFrameNs_, frame.captureTimeNs, shift.dx * scale, shift.dy * scale, shift.quality);
#ifdef AIMBUDDY_DEBUG_HOOKS
        if (tracingMotion_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(motionTraceMutex_);
            MotionRow row;
            row.fromNs = lastFrameNs_;
            row.toNs = frame.captureTimeNs;
            row.dx = shift.dx * scale;
            row.dy = shift.dy * scale;
            row.quality = shift.quality;
            row.movers = moverCount;
            const float toGrid = static_cast<float>(GlobalMotionEstimator::kGrid) / static_cast<float>(frame.roi.side);
            for (int i = 0; i < moverCount && i < kTracedMovers; ++i) {
                row.boxes[i] = {movers[i].left * toGrid, movers[i].top * toGrid, movers[i].right * toGrid, movers[i].bottom * toGrid};
            }
            row.grey = sceneMotion_.lastGrey();
            motionTrace_.push_back(row);
        }
#endif
        slideSum_ += std::hypot(shift.dx, shift.dy) * scale;
        qualitySum_ += shift.quality;
        ++slideFrames_;
    }
    lastFrameNs_ = frame.captureTimeNs;
}

#ifdef AIMBUDDY_DEBUG_HOOKS
void DetectionPipeline::feedSynthetic(const Detection* detections, int count, int64_t sceneNs) {
    std::lock_guard<std::mutex> lock(syntheticMutex_);
    synthetic_.emplace_back();
    SyntheticScene& scene = synthetic_.back();
    scene.sceneNs = sceneNs;
    scene.count = std::min(count, kMaxDetections);
    std::copy(detections, detections + scene.count, scene.detections);
    while (synthetic_.size() > kSyntheticBacklog) {
        synthetic_.pop_front();
    }
}

void DetectionPipeline::startMotionTrace() {
    std::lock_guard<std::mutex> lock(motionTraceMutex_);
    motionTrace_.clear();
    motionTrace_.reserve(kMotionTraceReserve);
    tracingMotion_.store(true, std::memory_order_release);
}

bool DetectionPipeline::writeMotionTrace(const std::string& path) {
    tracingMotion_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(motionTraceMutex_);
    FILE* file = std::fopen(path.c_str(), "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "from_ns,to_ns,dx,dy,quality,movers\n");
    for (const MotionRow& row : motionTrace_) {
        std::fprintf(file, "%lld,%lld,%.3f,%.3f,%.3f,%d\n", static_cast<long long>(row.fromNs), static_cast<long long>(row.toNs),
                     row.dx, row.dy, row.quality, row.movers);
    }
    std::fclose(file);

    // The pictures: for each row the time it ends at, then its values.
    FILE* pictures = std::fopen((path + ".grey").c_str(), "wb");
    if (pictures != nullptr) {
        for (const MotionRow& row : motionTrace_) {
            std::fwrite(&row.toNs, sizeof(row.toNs), 1, pictures);
            std::fwrite(row.grey.data(), sizeof(float), row.grey.size(), pictures);
            const int kept = std::min(row.movers, kTracedMovers);
            std::fwrite(&kept, sizeof(kept), 1, pictures);
            std::fwrite(row.boxes.data(), sizeof(MovingBox), static_cast<size_t>(kept), pictures);
        }
        std::fclose(pictures);
    }
    motionTrace_.clear();
    return true;
}

bool DetectionPipeline::detectSynthetic(const RoiFrame& frame, DetectionBatch& batch) {
    // The frame shows the scene as it was drawn a moment before it was composed: take the newest
    // scene at least that old, like a detector that looked at this very frame.
    SyntheticScene scene;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(syntheticMutex_);
        while (!synthetic_.empty() && synthetic_.front().sceneNs <= frame.captureTimeNs - kSyntheticScreenLatencyNs) {
            scene = synthetic_.front();
            synthetic_.pop_front();
            found = true;
        }
    }
    if (!found) {
        return false;
    }
    const float scale = frame.geometry.screenPerCapture();
    batch.scanned.left = static_cast<float>(frame.roi.x) * scale;
    batch.scanned.top = static_cast<float>(frame.roi.y) * scale;
    batch.scanned.right = static_cast<float>(frame.roi.x + frame.roi.side) * scale;
    batch.scanned.bottom = static_cast<float>(frame.roi.y + frame.roi.side) * scale;
    batch.count = 0;
    for (int i = 0; i < scene.count; ++i) {
        const Detection& d = scene.detections[i];
        if (batch.scanned.contains(d.x + d.width * 0.5f, d.y + d.height * 0.5f)) {
            batch.detections[batch.count++] = d;
        }
    }
    batch.captureNs = frame.captureTimeNs;
    batch.inferenceMs = 0.0f;
    return true;
}
#endif

void DetectionPipeline::recordStats(const DetectionBatch& batch, int64_t finishedNs) {
    inferenceMs_.store(smooth(inferenceMs_.load(std::memory_order_relaxed), batch.inferenceMs),
                       std::memory_order_relaxed);
    const float ageMs = static_cast<float>(finishedNs - batch.captureNs) * 1e-6f;
    frameAgeMs_.store(smooth(frameAgeMs_.load(std::memory_order_relaxed), ageMs), std::memory_order_relaxed);
    if (lastBatchNs_ > 0 && finishedNs > lastBatchNs_) {
        const float perSecond = 1e9f / static_cast<float>(finishedNs - lastBatchNs_);
        detectionsPerSecond_.store(smooth(detectionsPerSecond_.load(std::memory_order_relaxed), perSecond),
                                   std::memory_order_relaxed);
    }
    lastBatchNs_ = finishedNs;

    if (finishedNs - lastLogNs_ >= kStatsLogIntervalNs) {
        const Stats snapshot = stats();
        LOGI("Pipeline: %.1f detections/s, inference %.1f ms, frame age %.1f ms, %llu frames replaced, %s",
             snapshot.detectionsPerSecond, snapshot.inferenceMs, snapshot.frameAgeMs,
             static_cast<unsigned long long>(snapshot.replacedFrames), snapshot.backend);
        float bestScore = 0.0f;
        for (int i = 0; i < batch.count; ++i) {
            bestScore = std::max(bestScore, batch.detections[i].score);
        }
        LOGI("Last frame: %d detections, best score %.2f", batch.count, bestScore);
        if (cameraMotion_ != nullptr && slideFrames_ > 0) {
            const CameraResponse response = cameraMotion_->response();
            LOGI("Camera: scene slid %.1f px a frame on average (measurement quality %.2f); response gain %.2f/%.2f, delay %.0f ms, confidence %.2f",
                 slideSum_ / static_cast<float>(slideFrames_), qualitySum_ / static_cast<float>(slideFrames_), response.gainX,
                 response.gainY, response.delayS * 1000.0f, cameraMotion_->confidence());
            slideSum_ = 0.0f;
            qualitySum_ = 0.0f;
            slideFrames_ = 0;
        }
        lastLogNs_ = finishedNs;
    }
}

void DetectionPipeline::run() {
    LOGI("Detection pipeline started");
    // Pins this thread to the fast cores and times the CPU against the GPU. It runs here, not at
    // load time, because the thread that does it is the one that gets pinned.
    detector_.chooseBackend();
    backend_.store(detector_.backendName(), std::memory_order_relaxed);

    DetectionBatch batch;
    float appliedConfidence = -1.0f;
    while (running_.load(std::memory_order_acquire)) {
        RoiFrame* frame = capture_.mailbox().waitForLatest(kFrameWait);
        if (frame == nullptr) {
            continue;
        }
        applyConfidenceIfChanged(appliedConfidence);
#ifdef AIMBUDDY_DEBUG_HOOKS
        const bool detected = syntheticMode_.load(std::memory_order_acquire) ? detectSynthetic(*frame, batch)
                                                                              : detector_.detect(*frame, batch);
#else
        const bool detected = detector_.detect(*frame, batch);
#endif
        measureCameraMotion(*frame, detected ? &batch : nullptr);
        capture_.mailbox().endRead();  // the pixels are consumed; let the capture thread reuse the slot
        if (!detected) {
            continue;
        }

        tracker_.update(batch.detections, batch.count, batch.captureNs, &batch.scanned);
        if (observer_) {
            observer_(batch);
        }
        recordStats(batch, steadyNowNs());
    }
    LOGI("Detection pipeline stopped");
}

DetectionPipeline::Stats DetectionPipeline::stats() const {
    Stats stats;
    stats.inferenceMs = inferenceMs_.load(std::memory_order_relaxed);
    stats.frameAgeMs = frameAgeMs_.load(std::memory_order_relaxed);
    stats.detectionsPerSecond = detectionsPerSecond_.load(std::memory_order_relaxed);
    stats.replacedFrames = capture_.mailbox().overwrittenCount();
    stats.backend = backend_.load(std::memory_order_relaxed);
    return stats;
}

}  // namespace aimbuddy
