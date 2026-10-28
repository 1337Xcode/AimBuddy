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

/// The camera is looked at on a frame at least this long after the last one it was looked at on. That is 30 times a second:
/// often enough that the newest reading is never more than a frame or two old when a box is drawn, and cheap enough
/// not to take much from the game.
constexpr int64_t kCameraLookIntervalNs = 30'000'000;

/// How often debug builds print the statistics to the log.
constexpr int64_t kStatsLogIntervalNs = 5'000'000'000;

/// With a colour to look for there is no model to say how large the region should be: it is sized as for a model this big.
constexpr int kColourModelSize = 256;

/// The score a patch of the colour is given, and the detection confidence the tracker works with for it. The score
/// does not vary: a patch that has the colour and the shape of a person is as certain as it gets.
constexpr float kColourScore = 0.9f;
constexpr float kColourConfidence = 0.5f;

/// Learning looks at a square round the crosshair that reaches a fourth of the region's width to each side (and at
/// least this many pixels), so that an outline a little way from the crosshair is in it.
constexpr int kLearnHalfShareOfSide = 4;
constexpr int kLearnMinHalfPx = 12;

/// A patch of the colour is never counted shorter than this, whatever the minimum height set in screen pixels comes to in the region.
constexpr int kLeastPatchHeightPx = 8;

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

DetectionPipeline::DetectionPipeline() {
    capture_.setFrameObserver([this](const RoiFrame& frame) { measureCameraMotion(frame); });
}

bool DetectionPipeline::loadModel(const std::string& paramPath, const std::string& binPath, const ModelProfile& profile) {
    if (isRunning()) {
        LOGE("Cannot load a model while the pipeline is running");
        return false;
    }
    if (!detector_.load(paramPath.c_str(), binPath.c_str(), profile)) {
        return false;
    }
    profile_ = detector_.profile();
    if (!detectsByColour()) {
        capture_.setModelSize(detector_.inputSize());
    }
    return true;
}

void DetectionPipeline::setColourDetection(bool on) {
    if (isRunning()) {
        LOGE("Cannot change what finds the targets while running");
        return;
    }
    colourMode_.store(on, std::memory_order_release);
    if (on) {
        profile_ = ModelProfile::defaultsFor(1);   // a patch of the colour is one enemy body
        capture_.setModelSize(kColourModelSize);
    } else if (detector_.isLoaded()) {
        profile_ = detector_.profile();
        capture_.setModelSize(detector_.inputSize());
    }
}

void DetectionPipeline::setColourTarget(const ColourTarget& target) {
    std::lock_guard<std::mutex> lock(colourMutex_);
    colourTarget_ = target;
}

void DetectionPipeline::start() {
    if (isRunning() || !canStart()) {
        return;
    }
    capture_.mailbox().reopen();
    tracker_.reset();
    {
        std::lock_guard<std::mutex> lock(motionMutex_);
        sceneMotion_.reset();
        lastFrameNs_ = 0;
    }
    lastBatchNs_ = 0;
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
 * Measures how the picture slid since the last frame it was looked at on, 30 times a second at
 * most. This runs on the capture thread, for frames the detector never sees as well: a box found
 * in a frame is drawn well over a tenth of a second later, and the camera must be known up to
 * that moment, not only up to the picture the box was found in. The enemies being followed move
 * by themselves, so they are left out: a large one crossing the middle of the view would otherwise
 * be taken for the camera turning.
 */
void DetectionPipeline::measureCameraMotion(const RoiFrame& frame) {
#ifdef AIMBUDDY_DEBUG_HOOKS
    dumpFrameIfAsked(frame);
#endif
    if (cameraMotion_ == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(motionMutex_);
    const float limit = maxRate_.load(std::memory_order_relaxed);
    const int64_t intervalNs = std::max(kCameraLookIntervalNs, limit > 0.0f ? static_cast<int64_t>(1e9f / limit) : int64_t{0});
    if (lastFrameNs_ > 0 && frame.captureTimeNs > lastFrameNs_ && frame.captureTimeNs - lastFrameNs_ < intervalNs) {
        return;
    }

    // Where the targets are in this frame, in pixels of the region the odometry is given.
    MovingBox movers[kMaxTracks];
    int moverCount = 0;
    TrackView views[kMaxTracks];
    const int viewCount = tracker_.snapshot(frame.captureTimeNs, views, kMaxTracks, true);
    const float captureOfScreen = 1.0f / frame.geometry.screenPerCapture();
    for (int i = 0; i < viewCount; ++i) {
        const TrackView& view = views[i];
        const float left = (view.centerX - view.width * 0.5f) * captureOfScreen - static_cast<float>(frame.roi.x);
        const float top = (view.centerY - view.height * 0.5f) * captureOfScreen - static_cast<float>(frame.roi.y);
        movers[moverCount++] = {left, top, left + view.width * captureOfScreen, top + view.height * captureOfScreen};
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
void DetectionPipeline::requestFrameDump(const std::string& path) {
    std::lock_guard<std::mutex> lock(dumpMutex_);
    dumpPath_ = path;
}

/// Writes the frame as a binary PPM (RGB, no alpha) if a dump was asked for, once.
void DetectionPipeline::dumpFrameIfAsked(const RoiFrame& frame) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(dumpMutex_);
        path.swap(dumpPath_);
    }
    if (path.empty()) {
        return;
    }
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        LOGE("Could not write the frame to %s", path.c_str());
        return;
    }
    const int side = frame.roi.side;
    std::fprintf(file, "P6\n%d %d\n255\n", side, side);
    std::vector<uint8_t> row(static_cast<size_t>(side) * 3);
    for (int y = 0; y < side; ++y) {
        const uint8_t* source = frame.rgba.data() + static_cast<size_t>(y) * side * 4;
        for (int x = 0; x < side; ++x) {
            row[x * 3] = source[x * 4];
            row[x * 3 + 1] = source[x * 4 + 1];
            row[x * 3 + 2] = source[x * 4 + 2];
        }
        std::fwrite(row.data(), 1, row.size(), file);
    }
    std::fclose(file);
    LOGI("Wrote the %dx%d frame to %s", side, side, path.c_str());
}

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

/// Finds the patches of the learned colour that could be players, and reports them as the model would.
bool DetectionPipeline::detectByColour(const RoiFrame& frame, DetectionBatch& batch) {
    const int side = frame.roi.side;
    if (side <= 0 || frame.rgba.size() < static_cast<size_t>(side) * static_cast<size_t>(side) * 4) {
        return false;
    }
    const auto start = std::chrono::steady_clock::now();
    ColourTarget target;
    {
        std::lock_guard<std::mutex> lock(colourMutex_);
        target = colourTarget_;
    }
    const float screenPerCapture = frame.geometry.screenPerCapture();
    batch.count = 0;
    if (target.learned) {
        BlobFilter filter;
        filter.minHeightPx = std::max(kLeastPatchHeightPx, static_cast<int>(std::lround(target.minHeightScreenPx / screenPerCapture)));
        ColourBlob blobs[ColourBlobFinder::kMaxBlobs];
        const int found = colourFinder_.find(frame.rgba.data(), side, side * 4, target.range, filter, blobs, ColourBlobFinder::kMaxBlobs);
        for (int i = 0; i < found && batch.count < kMaxDetections; ++i) {
            const ColourBlob& blob = blobs[i];
            Detection detection;
            detection.x = static_cast<float>(frame.roi.x + blob.left) * screenPerCapture;
            detection.y = static_cast<float>(frame.roi.y + blob.top) * screenPerCapture;
            detection.width = static_cast<float>(blob.width()) * screenPerCapture;
            detection.height = static_cast<float>(blob.height()) * screenPerCapture;
            detection.score = kColourScore;
            detection.classId = 0;
            // A patch that reaches the edge of the region goes on beyond it: its box is cut off there.
            detection.clipFlags = static_cast<uint8_t>((blob.left <= 0 ? kClipLeft : 0) | (blob.right >= side ? kClipRight : 0) |
                                                       (blob.top <= 0 ? kClipTop : 0) | (blob.bottom >= side ? kClipBottom : 0));
            batch.detections[batch.count++] = detection;
        }
    }
    batch.scanned.left = static_cast<float>(frame.roi.x) * screenPerCapture;
    batch.scanned.top = static_cast<float>(frame.roi.y) * screenPerCapture;
    batch.scanned.right = static_cast<float>(frame.roi.x + frame.roi.side) * screenPerCapture;
    batch.scanned.bottom = static_cast<float>(frame.roi.y + frame.roi.side) * screenPerCapture;
    batch.captureNs = frame.captureTimeNs;
    batch.inferenceMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    return true;
}

/// Takes the most vivid colour round the crosshair, the middle of the region, as the one to look for.
void DetectionPipeline::learnColourFrom(const RoiFrame& frame) {
    const int side = frame.roi.side;
    ColourRange range;
    {
        std::lock_guard<std::mutex> lock(colourMutex_);
        range = colourTarget_.range;   // keeps the player's tolerance
    }
    const int half = std::max(kLearnMinHalfPx, side / kLearnHalfShareOfSide);
    const bool found = side > 0 && frame.rgba.size() >= static_cast<size_t>(side) * static_cast<size_t>(side) * 4 &&
                       learnColour(frame.rgba.data(), side, side * 4, side / 2, side / 2, half, &range);
    if (found) {
        LOGI("Learned the colour: hue %.0f, saturation from %.2f, brightness from %.2f", range.hueDegrees, range.minSaturation,
             range.minValue);
    } else {
        LOGI("No vivid colour near the crosshair to learn");
    }
    if (colourSink_) {
        colourSink_(found, range);
    }
    learningResult_.store(static_cast<int>(found ? ColourLearning::kLearned : ColourLearning::kNotFound), std::memory_order_release);
}

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
        if (cameraMotion_ != nullptr) {
            float slidePx = 0.0f;
            float quality = 0.0f;
            int frames = 0;
            {
                std::lock_guard<std::mutex> lock(motionMutex_);
                frames = slideFrames_;
                slidePx = frames > 0 ? slideSum_ / static_cast<float>(frames) : 0.0f;
                quality = frames > 0 ? qualitySum_ / static_cast<float>(frames) : 0.0f;
                slideSum_ = 0.0f;
                qualitySum_ = 0.0f;
                slideFrames_ = 0;
            }
            if (frames > 0) {
                const CameraResponse response = cameraMotion_->response();
                LOGI("Camera: scene slid %.1f px a look on average (measurement quality %.2f, %d looks); response gain %.2f/%.2f, delay %.0f ms, confidence %.2f",
                     slidePx, quality, frames, response.gainX, response.gainY, response.delayS * 1000.0f, cameraMotion_->confidence());
            }
        }
        lastLogNs_ = finishedNs;
    }
}

void DetectionPipeline::run() {
    LOGI("Detection pipeline started");
    // Pins this thread to the fast cores and times the CPU against the GPU. It runs here, not at
    // load time, because the thread that does it is the one that gets pinned. A stop does not have
    // to wait for the timing to end: it takes seconds on a slow phone.
    if (detectsByColour()) {
        backend_.store("Colour", std::memory_order_relaxed);
    } else {
        detector_.chooseBackend([this] { return !running_.load(std::memory_order_acquire); });
        backend_.store(detector_.backendName(), std::memory_order_relaxed);
    }

    DetectionBatch batch;
    float appliedConfidence = -1.0f;
    while (running_.load(std::memory_order_acquire)) {
        RoiFrame* frame = capture_.mailbox().waitForLatest(kFrameWait);
        if (frame == nullptr) {
            continue;
        }
        if (!thinner_.accept(frame->captureTimeNs)) {
            capture_.mailbox().endRead();   // the phone asked for fewer detections: this frame is left out
            continue;
        }
        const bool byColour = detectsByColour();
        if (byColour) {
            if (appliedConfidence != kColourConfidence) {
                tracker_.setConfig(TrackerConfig::forConfidence(kColourConfidence));   // the player's confidence is for the model
                appliedConfidence = kColourConfidence;
            }
            if (learnRequested_.exchange(false, std::memory_order_acq_rel)) {
                learnColourFrom(*frame);
            }
        } else {
            applyConfidenceIfChanged(appliedConfidence);
        }
#ifdef AIMBUDDY_DEBUG_HOOKS
        const bool detected = syntheticMode_.load(std::memory_order_acquire)
                                  ? detectSynthetic(*frame, batch)
                                  : (byColour ? detectByColour(*frame, batch) : detector_.detect(*frame, batch));
#else
        const bool detected = byColour ? detectByColour(*frame, batch) : detector_.detect(*frame, batch);
#endif
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
