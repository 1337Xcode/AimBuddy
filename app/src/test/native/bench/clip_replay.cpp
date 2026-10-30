// Replays prepared motion clips (training/universal/clip_frames.py) through the app's own decoder, camera motion
// estimator and tracker, in the order the detection pipeline runs them, and writes the targets the overlay would
// draw at every frame. training/universal/score_clips.py scores them against the clips' ground truth.
//
// Usage: clip_replay <detections per second> <latency ms> <confidence> <clip model folder> [...]
// Writes <folder>/tracks_<detections per second>.txt: one line a frame, "frame count" and then for each target drawn
// "id role centreX centreY width height opacity", in capture pixels.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/camera_motion.h"
#include "core/detection_decoder.h"
#include "core/geometry.h"
#include "core/global_motion.h"
#include "core/model_profile.h"
#include "core/tracker.h"

namespace {

using namespace aimbuddy;

constexpr int64_t kStartNs = 1'000'000'000;           // clips start one second into the clock, away from zero
constexpr int64_t kCameraLookIntervalNs = 33'333'333;  // the pipeline measures the camera 30 times a second at most

/// What clip_frames.py wrote about one clip and model.
struct ClipMeta {
    double fps = 0.0;
    int frames = 0;
    int captureWidth = 0;
    int captureHeight = 0;
    float screenPerCapture = 1.0f;
    RoiRect square;
    int motionSide = 0;
    int inputSize = 0;
    int rows = 0;
    int cols = 0;
    std::string roles;
    bool endToEnd = false;
};

bool readMeta(const std::string& path, ClipMeta* meta) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream words(line);
        std::string key;
        words >> key;
        if (key == "fps") {
            words >> meta->fps;
        } else if (key == "frames") {
            words >> meta->frames;
        } else if (key == "capture") {
            words >> meta->captureWidth >> meta->captureHeight;
        } else if (key == "screen_per_capture") {
            words >> meta->screenPerCapture;
        } else if (key == "square") {
            words >> meta->square.x >> meta->square.y >> meta->square.side;
        } else if (key == "motion") {
            words >> meta->motionSide;
        } else if (key == "input") {
            words >> meta->inputSize;
        } else if (key == "output") {
            words >> meta->rows >> meta->cols;
        } else if (key == "roles") {
            words >> meta->roles;
        } else if (key == "end_to_end") {
            int flag = 0;
            words >> flag;
            meta->endToEnd = flag != 0;
        }
    }
    return meta->fps > 0.0 && meta->frames > 0 && meta->square.side > 0 && meta->motionSide > 0 && meta->rows > 0 &&
           meta->cols > 0;
}

template <typename T>
bool readAll(const std::string& path, size_t count, std::vector<T>* out) {
    std::ifstream file(path, std::ios::binary);
    out->resize(count);
    file.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(count * sizeof(T)));
    return static_cast<size_t>(file.gcount()) == count * sizeof(T);
}

/// A frame handed to the detector, waiting for its result to arrive.
struct PendingResult {
    int frame = 0;
    int64_t captureNs = 0;
    int64_t arrivalNs = 0;
};

bool replay(const std::string& folder, double perSecond, double latencyMs, float confidence) {
    ClipMeta meta;
    if (!readMeta(folder + "/meta.txt", &meta)) {
        std::fprintf(stderr, "%s/meta.txt is missing or incomplete\n", folder.c_str());
        return false;
    }
    const size_t tensorFloats = static_cast<size_t>(meta.rows) * static_cast<size_t>(meta.cols);
    const size_t motionBytes = static_cast<size_t>(meta.motionSide) * static_cast<size_t>(meta.motionSide) * 4;
    std::vector<float> raw;
    std::vector<uint8_t> motion;
    if (!readAll(folder + "/raw.bin", tensorFloats * static_cast<size_t>(meta.frames), &raw) ||
        !readAll(folder + "/motion.bin", motionBytes * static_cast<size_t>(meta.frames), &motion)) {
        std::fprintf(stderr, "%s: raw.bin or motion.bin is shorter than meta.txt says\n", folder.c_str());
        return false;
    }

    DecodeSpec spec;
    if (!inferDecodeSpec(raw.data(), meta.rows, meta.cols, meta.endToEnd, &spec)) {
        std::fprintf(stderr, "%s: the output is not a detector's\n", folder.c_str());
        return false;
    }
    ModelProfile profile = ModelProfile::parseRoles(meta.roles, spec.classCount);
    profile.inputSize = meta.inputSize;
    profile.endToEndOutput = meta.endToEnd;

    const TrackerConfig config = TrackerConfig::forConfidence(confidence);
    CameraMotion camera;
    Tracker tracker(config);
    tracker.setEgoMotion(&camera);
    GlobalMotionEstimator scene;
    DecodeParams params;
    params.minScore = config.lowScore;   // as the pipeline sets the detector's floor
    params.modelSize = meta.inputSize;
    CaptureGeometry geometry;
    geometry.captureWidth = meta.captureWidth;
    geometry.captureHeight = meta.captureHeight;
    geometry.screenWidth = static_cast<int>(std::lround(meta.captureWidth * meta.screenPerCapture));
    geometry.screenHeight = static_cast<int>(std::lround(meta.captureHeight * meta.screenPerCapture));
    const float spc = geometry.screenPerCapture();
    ScanRegion scanned;
    scanned.left = static_cast<float>(meta.square.x) * spc;
    scanned.top = static_cast<float>(meta.square.y) * spc;
    scanned.right = static_cast<float>(meta.square.x + meta.square.side) * spc;
    scanned.bottom = static_cast<float>(meta.square.y + meta.square.side) * spc;
    const float motionPerCapture = static_cast<float>(meta.motionSide) / static_cast<float>(meta.square.side);

    const double frameNs = 1e9 / meta.fps;
    const int step = std::max(1, static_cast<int>(std::lround(meta.fps / perSecond)));
    const int64_t latencyNs = static_cast<int64_t>(latencyMs * 1e6);
    std::deque<PendingResult> pending;
    int64_t lastLookNs = -1;
    TrackView views[kMaxTracks];
    Detection detections[kMaxDetections];
    ModelBox boxes[kMaxDetections];

    char name[64];
    std::snprintf(name, sizeof(name), "/tracks_%g.txt", perSecond);
    std::FILE* out = std::fopen((folder + name).c_str(), "w");
    if (out == nullptr) {
        std::fprintf(stderr, "%s%s cannot be written\n", folder.c_str(), name);
        return false;
    }
    for (int frame = 0; frame < meta.frames; ++frame) {
        const int64_t nowNs = kStartNs + static_cast<int64_t>(std::llround(frame * frameNs));

        // results that have arrived by now reach the tracker, oldest first
        while (!pending.empty() && pending.front().arrivalNs <= nowNs) {
            const PendingResult result = pending.front();
            pending.pop_front();
            const float* tensor = raw.data() + tensorFloats * static_cast<size_t>(result.frame);
            const int count = decodeDetections(tensor, meta.rows, meta.cols, spec, params, profile, boxes, kMaxDetections);
            for (int i = 0; i < count; ++i) {
                detections[i] = mapToScreen(boxes[i], meta.square, geometry, meta.inputSize);
            }
            tracker.update(detections, count, result.captureNs, &scanned, result.arrivalNs);
        }

        // the capture thread measures how the picture slid, leaving out the targets it follows
        if (lastLookNs < 0 || nowNs - lastLookNs >= kCameraLookIntervalNs) {
            MovingBox movers[kMaxTracks];
            const int viewCount = tracker.snapshot(nowNs, views, kMaxTracks, true);
            for (int i = 0; i < viewCount; ++i) {
                const float left = (views[i].centerX - views[i].width * 0.5f) / spc - static_cast<float>(meta.square.x);
                const float top = (views[i].centerY - views[i].height * 0.5f) / spc - static_cast<float>(meta.square.y);
                movers[i] = {left * motionPerCapture, top * motionPerCapture,
                             (left + views[i].width / spc) * motionPerCapture, (top + views[i].height / spc) * motionPerCapture};
            }
            const uint8_t* pixels = motion.data() + motionBytes * static_cast<size_t>(frame);
            const GlobalShift shift = scene.push(pixels, meta.motionSide, meta.motionSide * 4, movers, viewCount);
            if (lastLookNs >= 0) {
                const float toScreen = spc / motionPerCapture;
                camera.addMeasurement(lastLookNs, nowNs, shift.dx * toScreen, shift.dy * toScreen, shift.quality);
            }
            lastLookNs = nowNs;
        }

        // the detector takes this frame when one is due
        if (frame % step == 0) {
            pending.push_back({frame, nowNs, nowNs + latencyNs});
        }

        // what the overlay draws now
        const int drawn = tracker.snapshot(nowNs, views, kMaxTracks);
        std::fprintf(out, "%d %d", frame, drawn);
        for (int i = 0; i < drawn; ++i) {
            const char role = profile.roleOf(views[i].classId) == ClassRole::kHead ? 'h' : 'b';
            std::fprintf(out, " %d %c %.1f %.1f %.1f %.1f %.2f", views[i].id, role, views[i].centerX / spc,
                         views[i].centerY / spc, views[i].width / spc, views[i].height / spc, views[i].opacity);
        }
        std::fprintf(out, "\n");
    }
    std::fclose(out);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "Usage: clip_replay <detections per second> <latency ms> <confidence> <clip model folder> [...]\n");
        return 2;
    }
    const double perSecond = std::atof(argv[1]);
    const double latencyMs = std::atof(argv[2]);
    const float confidence = static_cast<float>(std::atof(argv[3]));
    if (!(perSecond > 0.0) || !(latencyMs >= 0.0) || !(confidence > 0.0f)) {
        std::fprintf(stderr, "Detections per second and confidence must be above 0, latency 0 or more\n");
        return 2;
    }
    int failed = 0;
    for (int i = 4; i < argc; ++i) {
        failed += replay(argv[i], perSecond, latencyMs, confidence) ? 0 : 1;
    }
    return failed == 0 ? 0 : 1;
}
