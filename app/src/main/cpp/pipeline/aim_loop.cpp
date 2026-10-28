#include "aim_loop.h"

#include <sys/resource.h>
#include <time.h>

#include <algorithm>
#include <cstdio>

#include "../utils/logger.h"

namespace aimbuddy {

namespace {

/// How often the controller steps while the assist is working: 125 times a second.
constexpr int64_t kActivePeriodNs = 8'000'000;

/// How often the loop looks around while the assist is off: just often enough to notice the switch.
constexpr int64_t kIdlePeriodNs = 20'000'000;

/// How often the player's wishes are read.
constexpr int64_t kConfigRefreshNs = 100'000'000;

/// How often what was learned about the camera is passed on to be kept.
constexpr int64_t kLearnedReportNs = 5'000'000'000;

/// Wait before asking the helper again to take the touchscreen over.
constexpr int64_t kEngageRetryNs = 500'000'000;
constexpr int64_t kEngageRetryAfterRefusalNs = 2'000'000'000;

/// Longest silence towards the helper while it holds the touchscreen, and while it only waits.
constexpr int64_t kHeartbeatNs = 100'000'000;
constexpr int64_t kIdleHeartbeatNs = 1'000'000'000;

/// Time the system is given to settle on its touchscreen, after the helper takes it over and
/// after the screen turns, before the assist's finger goes down. A touch that begins sooner can be
/// lost without a trace, and a finger that went down before the screen turned is lost with it.
constexpr int64_t kSettleNs = 700'000'000;

/// Priority of the loop thread; a late step is a late correction. The system may refuse a value this low.
constexpr int kThreadNice = -8;

/// How much steering (0 to 1) the log calls the player steering; it calls them stopped well below that.
constexpr float kLoggedSteering = 0.5f;

#ifdef AIMBUDDY_DEBUG_HOOKS
/// Rows reserved for a trace: two minutes of steps.
constexpr size_t kTraceReserve = 15000;
#endif

int64_t steadyNowNs() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;
}

}  // namespace

AimLoop::AimLoop(Tracker* tracker, const ModelProfile* profile, touch::TouchLink* link, CameraMotion* motion)
    : tracker_(tracker), profile_(profile), link_(link), motion_(motion), controller_(AimSettings(), motion) {}

void AimLoop::setDisplay(int width, int height, int rotation) {
    displayChangedNs_.store(steadyNowNs(), std::memory_order_release);
    std::lock_guard<std::mutex> lock(displayMutex_);
    display_.width = width;
    display_.height = height;
    display_.rotation = rotation;
    ++display_.version;
}

AimLoop::Display AimLoop::currentDisplay() const {
    std::lock_guard<std::mutex> lock(displayMutex_);
    return display_;
}

void AimLoop::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    thread_ = std::thread([this] { run(); });
}

void AimLoop::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel) && !thread_.joinable()) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

AimLoop::Stats AimLoop::stats() const {
    Stats stats;
    stats.helperConnected = link_->isConnected();
    stats.helperState = link_->state();
    stats.helperReason = link_->reason();
    stats.fingerDown = fingerDown_.load(std::memory_order_relaxed);
    stats.lockedTrack = lockedTrack_.load(std::memory_order_relaxed);
    return stats;
}

void AimLoop::run() {
    LOGI("Aim loop started");
    setpriority(PRIO_PROCESS, 0, kThreadNice);

    // Start from what an earlier run measured, so the first move is already close to right.
    if (source_) {
        const AimLoopConfig first = source_();
        motion_->setPrior(first.cameraGuess, first.learnCamera);
        if (first.learnCamera && first.hasLearned) {
            motion_->seed(first.learned, first.learnedConfidence);
        }
    }

    int64_t nextNs = steadyNowNs();
    while (running_.load(std::memory_order_acquire)) {
        const int64_t nowNs = steadyNowNs();
        refreshConfig(nowNs);
        link_->pollReports();

        const bool active = config_.enabled && link_->isConnected();
        if (active) {
            syncDisplayWithHelper();
            stepWhileActive(nowNs);
        } else {
            standDown(nowNs);
        }

        // Sleep to the next step on an absolute clock so the rate does not drift; after a long
        // stall start afresh rather than trying to catch up with a burst of steps.
        nextNs += active ? kActivePeriodNs : kIdlePeriodNs;
        const int64_t afterNs = steadyNowNs();
        if (nextNs < afterNs) {
            nextNs = afterNs;
        }
        const timespec wake{static_cast<time_t>(nextNs / 1'000'000'000LL), static_cast<long>(nextNs % 1'000'000'000LL)};
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, nullptr);
    }

    // Leaving: whatever the settings say, the finger comes off and the touchscreen goes back.
    config_.enabled = false;
    standDown(steadyNowNs());
    LOGI("Aim loop stopped");
}

void AimLoop::refreshConfig(int64_t nowNs) {
    if (!source_ || nowNs - lastConfigNs_ < kConfigRefreshNs) {
        return;
    }
    lastConfigNs_ = nowNs;
    config_ = source_();
    controller_.setSettings(config_.aim);
    motion_->setPrior(config_.cameraGuess, config_.learnCamera);
    reportLearned(nowNs);
}

void AimLoop::reportLearned(int64_t nowNs) {
    if (!learnedSink_ || !motion_->hasLearned() || nowNs - lastLearnedReportNs_ < kLearnedReportNs) {
        return;
    }
    lastLearnedReportNs_ = nowNs;
    learnedSink_(motion_->response(), motion_->confidence());
}

void AimLoop::syncDisplayWithHelper() {
    const Display display = currentDisplay();
    const uint32_t session = link_->session();
    if (display.width <= 0 || display.height <= 0) {
        return;
    }
    if (session == configuredSession_ && display.version == configuredDisplayVersion_) {
        return;
    }
    link_->configure(display.width, display.height, display.rotation);
    controller_.setCrosshair(static_cast<float>(display.width) * 0.5f, static_cast<float>(display.height) * 0.5f);
    if (display.version != configuredDisplayVersion_) {
        controller_.forgetScene();   // the screen turned: the places of the boxes given up on are not the same places now
    }
    configuredSession_ = session;
    configuredDisplayVersion_ = display.version;
}

void AimLoop::keepHeartbeat(int64_t nowNs, int64_t maxSilenceNs) {
    if (nowNs - link_->lastSendNs() >= maxSilenceNs) {
        link_->ping();
    }
}

/// What the player's own hands are doing, as far as the touch helper has said.
PlayerInput AimLoop::readPlayer(int64_t nowNs) {
    const touch::TouchLink::PlayerReport report = link_->playerReport();
    const Display display = currentDisplay();
    PlayerInput player;
    player.steering = lookWatch_.update(nowNs, report.activity, report.atNs, config_.aim.anchorX,
                                        static_cast<float>(display.width), static_cast<float>(display.height));
    // The log says when the player starts and stops steering (with some slack, so it does not flicker).
    if (player.steering > kLoggedSteering && !loggedSteering_) {
        LOGI("The player is steering the camera");
        loggedSteering_ = true;
    } else if (player.steering < kLoggedSteering * 0.2f && loggedSteering_) {
        LOGI("The player stopped steering the camera");
        loggedSteering_ = false;
    }
    return player;
}

void AimLoop::sendOutput(const AimOutput& output) {
    switch (output.action) {
        case AimAction::kTouchDown:
            link_->aimDown(output.x, output.y);
            break;
        case AimAction::kTouchMove:
            link_->aimMove(output.x, output.y);
            break;
        case AimAction::kTouchUp:
            link_->aimUp();
            break;
        case AimAction::kNone:
            break;
    }
}

void AimLoop::stepWhileActive(int64_t nowNs) {
    wasActive_ = true;
    const touch::protocol::State state = link_->state();
    if (state == touch::protocol::State::kMerging) {
        if (!wasMerging_) {
            mergingSinceNs_ = nowNs;
        }
        wasMerging_ = true;
        const int64_t displayChangedNs = displayChangedNs_.load(std::memory_order_acquire);
        if (displayChangedNs != handledDisplayChangeNs_) {
            handledDisplayChangeNs_ = displayChangedNs;
            controller_.release();   // a finger that went down before the screen turned may have been lost with it
            sendOutput(controller_.step(nowNs, *tracker_, *profile_, PlayerInput()));
        }
        if (nowNs - std::max(mergingSinceNs_, displayChangedNs) < kSettleNs) {
            keepHeartbeat(nowNs, kHeartbeatNs);
            return;
        }
        sendOutput(controller_.step(nowNs, *tracker_, *profile_, readPlayer(nowNs)));
#ifdef AIMBUDDY_DEBUG_HOOKS
        recordTrace(nowNs);
#endif
        keepHeartbeat(nowNs, kHeartbeatNs);
    } else {
        if (wasMerging_) {
            // The helper gave the screen back (heartbeat, volume keys, ...): the finger is gone with it.
            forgetFinger(nowNs);
            wasMerging_ = false;
        }
        if (state == touch::protocol::State::kReleased) {
            const bool refused = link_->reason() == touch::protocol::Reason::kGrabFailed;
            if (nowNs - lastEngageNs_ >= (refused ? kEngageRetryAfterRefusalNs : kEngageRetryNs)) {
                link_->engage();
                lastEngageNs_ = nowNs;
            }
        }
        keepHeartbeat(nowNs, state == touch::protocol::State::kWaitingForFingers ? kHeartbeatNs : kIdleHeartbeatNs);
    }
    logChanges(state);
    fingerDown_.store(controller_.isTouching(), std::memory_order_relaxed);
    lockedTrack_.store(controller_.lockedTrackId(), std::memory_order_relaxed);
}

/** Writes a log line when the helper changes state or the assist finger goes down or up. */
void AimLoop::logChanges(touch::protocol::State state) {
    if (state != loggedState_) {
        LOGI("Touch helper state %d -> %d (reason %d)", static_cast<int>(loggedState_), static_cast<int>(state),
             static_cast<int>(link_->reason()));
        loggedState_ = state;
    }
    const int deadFingers = controller_.telemetry().deadFingers;
    if (deadFingers != loggedDeadFingers_) {
        LOGW("The game is not answering the assist finger (%d times; it moved %.0f px and the scene slid %.0f px): lifting it to try again",
             deadFingers, controller_.telemetry().deadFingerPx, controller_.telemetry().deadSlidePx);
        loggedDeadFingers_ = deadFingers;
    }
    const bool touching = controller_.isTouching();
    if (touching != loggedTouching_) {
        LOGI("Assist finger %s (target %d)", touching ? "down" : "up", controller_.lockedTrackId());
        loggedTouching_ = touching;
    }
}

void AimLoop::forgetFinger(int64_t nowNs) {
    lookWatch_.reset();
    controller_.release();
    (void)controller_.step(nowNs, *tracker_, *profile_, PlayerInput());   // the lift has nowhere to go; this clears the state
}

void AimLoop::standDown(int64_t nowNs) {
    if (!wasActive_) {
        if (link_->isConnected()) {
            keepHeartbeat(nowNs, kIdleHeartbeatNs);   // a helper that hears nothing for a few seconds shuts itself down
        }
        return;
    }
    if (link_->isConnected()) {
        if (controller_.isTouching()) {
            link_->aimUp();
        }
        link_->release();
    }
    forgetFinger(nowNs);
    wasActive_ = false;
    wasMerging_ = false;
    fingerDown_.store(false, std::memory_order_relaxed);
    lockedTrack_.store(-1, std::memory_order_relaxed);
}

#ifdef AIMBUDDY_DEBUG_HOOKS
void AimLoop::startTrace() {
    std::lock_guard<std::mutex> lock(traceMutex_);
    trace_.clear();
    trace_.reserve(kTraceReserve);
    tracing_.store(true, std::memory_order_release);
}

void AimLoop::recordTrace(int64_t nowNs) {
    if (!tracing_.load(std::memory_order_acquire)) {
        return;
    }
    TraceRow row;
    row.timeNs = nowNs;
    row.aim = controller_.telemetry();
    row.response = motion_->response();
    row.confidence = motion_->confidence();
    std::lock_guard<std::mutex> lock(traceMutex_);
    trace_.push_back(row);
}

bool AimLoop::writeTrace(const std::string& path) {
    tracing_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(traceMutex_);
    FILE* file = std::fopen(path.c_str(), "w");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "t_ns,target,err_x,err_y,deadzone,moving,stability,finger_x,finger_y,touching,gain_x,gain_y,delay_ms,confidence\n");
    for (const TraceRow& row : trace_) {
        std::fprintf(file, "%lld,%d,%.2f,%.2f,%.2f,%.3f,%.3f,%.1f,%.1f,%d,%.3f,%.3f,%.1f,%.3f\n",
                     static_cast<long long>(row.timeNs), row.aim.targetId, row.aim.errorX, row.aim.errorY, row.aim.deadzone,
                     row.aim.movingShare, row.aim.stability, row.aim.fingerX, row.aim.fingerY, row.aim.touching ? 1 : 0,
                     row.response.gainX, row.response.gainY, row.response.delayS * 1000.0f, row.confidence);
    }
    std::fclose(file);
    trace_.clear();
    return true;
}
#endif

}  // namespace aimbuddy
