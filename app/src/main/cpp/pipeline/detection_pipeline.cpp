#include "detection_pipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "../utils/logger.h"

namespace aimbuddy {

namespace {

/// How long the pipeline thread waits for a frame before checking whether it should stop.
constexpr std::chrono::milliseconds kFrameWait{50};

/// Weight of the newest sample in the smoothed statistics (about the last 20 samples).
constexpr float kStatsSmoothing = 0.05f;

/// How often debug builds print the statistics to the log.
constexpr int64_t kStatsLogIntervalNs = 5'000'000'000;

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

void DetectionPipeline::measureCameraMotion(const RoiFrame& frame) {
    if (cameraMotion_ == nullptr) {
        return;
    }
    const GlobalShift shift = sceneMotion_.push(frame.rgba.data(), frame.roi.side, frame.roi.side * 4);
    if (lastFrameNs_ > 0 && frame.captureTimeNs > lastFrameNs_) {
        // The measurement is in capture pixels; everything else works in screen pixels.
        const float scale = frame.geometry.screenPerCapture();
        cameraMotion_->addMeasurement(lastFrameNs_, frame.captureTimeNs, shift.dx * scale, shift.dy * scale, shift.quality);
    }
    lastFrameNs_ = frame.captureTimeNs;
}

void DetectionPipeline::recordStats(const DetectionBatch& batch, int64_t finishedNs) {
    inferenceMs_.store(smooth(inferenceMs_.load(std::memory_order_relaxed), batch.inferenceMs),
        slideSum_ += std::hypot(shift.dx, shift.dy) * scale;
        qualitySum_ += shift.quality;
        ++slideFrames_;
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
        measureCameraMotion(*frame);
        const bool detected = detector_.detect(*frame, batch);
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
