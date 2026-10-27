#include "aim_loop.h"

#include <sys/resource.h>
#include <time.h>

#include "../utils/logger.h"

namespace aimbuddy {

namespace {

/// How often the controller steps while the assist is working: 125 times a second.
constexpr int64_t kActivePeriodNs = 8'000'000;

/// How often the loop looks around while the assist is off: just often enough to notice the switch.
constexpr int64_t kIdlePeriodNs = 20'000'000;

/// How often the player's wishes are read.
constexpr int64_t kConfigRefreshNs = 100'000'000;

/// Wait before asking the helper again to take the touchscreen over.
constexpr int64_t kEngageRetryNs = 500'000'000;
constexpr int64_t kEngageRetryAfterRefusalNs = 2'000'000'000;

/// Longest silence towards the helper while it holds the touchscreen, and while it only waits.
constexpr int64_t kHeartbeatNs = 100'000'000;
constexpr int64_t kIdleHeartbeatNs = 1'000'000'000;

/// Priority of the loop thread; a late step is a late correction. The system may refuse a value this low.
constexpr int kThreadNice = -8;

int64_t steadyNowNs() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;
}

}  // namespace

AimLoop::AimLoop(Tracker* tracker, const ModelProfile* profile, touch::TouchLink* link)
    : tracker_(tracker), profile_(profile), link_(link) {}

void AimLoop::setDisplay(int width, int height, int rotation) {
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
    tracker_->setEgoMotion(&controller_.commandedMotion());

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
    tracker_->setEgoMotion(nullptr);
    LOGI("Aim loop stopped");
}

void AimLoop::refreshConfig(int64_t nowNs) {
    if (!source_ || nowNs - lastConfigNs_ < kConfigRefreshNs) {
        return;
    }
    lastConfigNs_ = nowNs;
    config_ = source_();
    controller_.setSettings(config_.aim);
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
    configuredSession_ = session;
    configuredDisplayVersion_ = display.version;
}

void AimLoop::keepHeartbeat(int64_t nowNs, int64_t maxSilenceNs) {
    if (nowNs - link_->lastSendNs() >= maxSilenceNs) {
        link_->ping();
    }
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
        wasMerging_ = true;
        sendOutput(controller_.step(nowNs, *tracker_, *profile_, PlayerInput()));
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
    fingerDown_.store(controller_.isTouching(), std::memory_order_relaxed);
    lockedTrack_.store(controller_.lockedTrackId(), std::memory_order_relaxed);
}

void AimLoop::forgetFinger(int64_t nowNs) {
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

}  // namespace aimbuddy
