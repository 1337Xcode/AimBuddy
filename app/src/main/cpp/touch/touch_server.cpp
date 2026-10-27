#include "touch_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace aimbuddy::touch {

using namespace protocol;

namespace {

/// How often the housekeeping runs: heartbeats, the escape keys and waiting for fingers to lift.
constexpr int kTickMs = 20;

constexpr int kEventsPerRead = 64;
constexpr int kMaxEpollEvents = 8;

/// Connections with a wrong hello or token the helper tolerates before it gives up.
constexpr int kMaxBadClients = 5;

/// Time Android needs to notice a new virtual device before events should be sent to it.
constexpr int kVirtualDeviceSettleMs = 300;

constexpr uint16_t kKeyVolumeDown = 114;
constexpr uint16_t kKeyVolumeUp = 115;

bool g_verbose = false;

/// Prints a line to stderr, where the app collects what the helper has to say.
void say(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::fputs("aimtouch: ", stderr);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
}

#define SAY_VERBOSE(...)    \
    do {                    \
        if (g_verbose) {    \
            say(__VA_ARGS__); \
        }                   \
    } while (0)

/// Takes events and throws them away: for following the real screen without a virtual one.
class DiscardingSink : public EventSink {
public:
    void emit(uint16_t, uint16_t, int32_t) override {}
};
DiscardingSink g_discard;

bool addToEpoll(int epollFd, int fd, uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    return epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event) == 0;
}

/// The exit code that goes with a reason the helper had to stop.
int exitCodeFor(Reason reason) {
    switch (reason) {
        case Reason::kNoTouchscreen:
        case Reason::kTouchscreenDenied:
            return kExitNoTouchscreen;
        case Reason::kVirtualDeviceDenied:
            return kExitVirtualDevice;
        case Reason::kDeviceLost:
            return kExitDeviceLost;
        case Reason::kBadClient:
            return kExitBadClient;
        default:
            return kExitOk;
    }
}

}  // namespace

TouchServer::TouchServer(const ServerOptions& options) : options_(options) { g_verbose = options.verbose; }

TouchServer::~TouchServer() { closeEverything(); }

int64_t TouchServer::nowNs() const {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;
}

// ---------------------------------------------------------------------------------------------
// Setup and teardown
// ---------------------------------------------------------------------------------------------

int TouchServer::run() {
    startedNs_ = nowNs();
    if (!openListener() || !openLoop()) {
        closeEverything();
        return exitCode_ == kExitOk ? kExitSocket : exitCode_;
    }
    SAY_VERBOSE("listening on 127.0.0.1:%d", options_.port);

    epoll_event events[kMaxEpollEvents];
    while (!stopping_) {
        const int count = epoll_wait(epollFd_, events, kMaxEpollEvents, -1);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            say("epoll_wait failed: %s", std::strerror(errno));
            stop(kExitSocket);
            break;
        }
        for (int i = 0; i < count && !stopping_; ++i) {
            dispatch(events[i].data.fd, events[i].events);
        }
    }
    closeEverything();
    return exitCode_;
}

bool TouchServer::openListener() {
    listenFd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) {
        say("socket failed: %s", std::strerror(errno));
        return false;
    }
    const int enable = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(options_.port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // only this device can connect
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(listenFd_, 2) < 0) {
        say("cannot listen on port %d: %s", options_.port, std::strerror(errno));
        return false;
    }
    return true;
}

bool TouchServer::openLoop() {
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ < 0) {
        say("epoll_create1 failed: %s", std::strerror(errno));
        return false;
    }

    // Signals arrive as readable events, so a stop request is handled in order with everything else.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGHUP);
    sigprocmask(SIG_BLOCK, &signals, nullptr);
    signal(SIGPIPE, SIG_IGN);
    signalFd_ = signalfd(-1, &signals, SFD_NONBLOCK | SFD_CLOEXEC);

    timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (signalFd_ < 0 || timerFd_ < 0) {
        say("cannot create the signal or timer descriptor: %s", std::strerror(errno));
        return false;
    }
    itimerspec interval{};
    interval.it_interval.tv_nsec = kTickMs * 1'000'000L;
    interval.it_value.tv_nsec = kTickMs * 1'000'000L;
    timerfd_settime(timerFd_, 0, &interval, nullptr);

    return addToEpoll(epollFd_, listenFd_, EPOLLIN) && addToEpoll(epollFd_, signalFd_, EPOLLIN) &&
           addToEpoll(epollFd_, timerFd_, EPOLLIN);
}

bool TouchServer::openDevices(Reason* reason) {
    int deniedNodes = 0;
    std::vector<TouchscreenInfo> screens = findTouchscreens(&deniedNodes);
    if (!options_.devicePath.empty()) {
        std::vector<TouchscreenInfo> wanted;
        for (const TouchscreenInfo& screen : screens) {
            if (screen.path == options_.devicePath) {
                wanted.push_back(screen);
            }
        }
        screens = wanted;
    }
    if (screens.empty()) {
        *reason = deniedNodes > 0 ? Reason::kTouchscreenDenied : Reason::kNoTouchscreen;
        say("no usable touchscreen found (%d input devices refused to open)", deniedNodes);
        return false;
    }
    screenInfo_ = screens.front();
    if (const int error = screen_.open(screenInfo_.path); error != 0) {
        say("cannot open %s: %s", screenInfo_.path.c_str(), std::strerror(error));
        *reason = Reason::kTouchscreenDenied;
        return false;
    }
    const AxisRange& x = screenInfo_.device.axis(evdev::kAbsMtPositionX);
    const AxisRange& y = screenInfo_.device.axis(evdev::kAbsMtPositionY);
    rawRange_ = {x.minimum, x.maximum, y.minimum, y.maximum};
    say("touchscreen %s \"%s\": %d slots, %d x %d", screenInfo_.path.c_str(), screenInfo_.name.c_str(),
        screenInfo_.device.slotCount, x.maximum - x.minimum + 1, y.maximum - y.minimum + 1);

    if (const int error = virtual_.create(screenInfo_, screenInfo_.device.slotCount + 1); error != 0) {
        say("cannot create the virtual touchscreen: %s", std::strerror(error));
        *reason = Reason::kVirtualDeviceDenied;
        return false;
    }

    addToEpoll(epollFd_, screen_.fd(), EPOLLIN);
    for (const std::string& path : findVolumeKeyDevices()) {
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0 && addToEpoll(epollFd_, fd, EPOLLIN)) {
            keyFds_.push_back(fd);
        }
    }
    resetObserver();
    usleep(kVirtualDeviceSettleMs * 1000);
    return true;
}

void TouchServer::stop(int exitCode) {
    if (!stopping_) {
        stopping_ = true;
        exitCode_ = exitCode;
    }
}

void TouchServer::closeEverything() {
    disengage(Reason::kRequested);
    if (clientFd_ >= 0) {
        sendStatus(State::kStopped);
    }
    virtual_.destroy();
    screen_.close();
    for (const int fd : keyFds_) {
        ::close(fd);
    }
    keyFds_.clear();
    for (int* fd : {&clientFd_, &listenFd_, &signalFd_, &timerFd_, &epollFd_}) {
        if (*fd >= 0) {
            ::close(*fd);
            *fd = -1;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------------------------

void TouchServer::dispatch(int fd, uint32_t events) {
    if (fd == listenFd_) {
        acceptClient();
    } else if (fd == clientFd_) {
        readClient();
    } else if (fd == signalFd_) {
        signalfd_siginfo info;
        while (read(signalFd_, &info, sizeof(info)) == static_cast<ssize_t>(sizeof(info))) {
        }
        SAY_VERBOSE("stop signal received");
        stop(kExitOk);
    } else if (fd == timerFd_) {
        uint64_t expirations;
        if (read(timerFd_, &expirations, sizeof(expirations)) > 0) {
            tick();
        }
    } else if (fd == screen_.fd()) {
        readTouchscreen();
    } else if (std::find(keyFds_.begin(), keyFds_.end(), fd) != keyFds_.end()) {
        readKeys(fd);
    }
}

void TouchServer::tick() {
    const int64_t now = nowNs();
    if (clientFd_ < 0 || login_ != Login::kDone) {
        if (now - startedNs_ > static_cast<int64_t>(options_.clientWaitMs) * 1'000'000LL) {
            say("the app never connected");
            stop(kExitNoClient);
        }
        return;
    }

    // The app must keep talking. When it does not, the screen goes back to the system at once.
    const int64_t silentNs = now - lastRequestNs_;
    if (phase_ != Phase::kReleased && silentNs > static_cast<int64_t>(options_.heartbeatMs) * 1'000'000LL) {
        say("no heartbeat for %lld ms: giving the touchscreen back", static_cast<long long>(silentNs / 1'000'000LL));
        disengage(Reason::kHeartbeatLost);
    }
    if (silentNs > static_cast<int64_t>(options_.deadClientMs) * 1'000'000LL) {
        say("the app has been silent for too long: stopping");
        stop(kExitClientGone);
        return;
    }

    // Both volume keys held for a moment: the player's way out if anything has gone wrong.
    if (volumeUpHeld_ && volumeDownHeld_) {
        if (keysHeldSinceNs_ == 0) {
            keysHeldSinceNs_ = now;
        } else if (now - keysHeldSinceNs_ > static_cast<int64_t>(options_.escapeHoldMs) * 1'000'000LL) {
            say("volume keys held: giving the touchscreen back and stopping");
            disengage(Reason::kEscapeKeys);
            stop(kExitEscapeKeys);
        }
    } else {
        keysHeldSinceNs_ = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// The app's connection
// ---------------------------------------------------------------------------------------------

void TouchServer::acceptClient() {
    const int fd = accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
        return;
    }
    if (clientFd_ >= 0) {
        ::close(fd);   // one app at a time
        return;
    }
    const int enable = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
    if (!addToEpoll(epollFd_, fd, EPOLLIN | EPOLLRDHUP)) {
        ::close(fd);
        return;
    }
    clientFd_ = fd;
    login_ = Login::kAwaitingHello;
    receivedBytes_ = 0;
    lastRequestNs_ = nowNs();
}

void TouchServer::dropClient() {
    if (clientFd_ >= 0) {
        epoll_ctl(epollFd_, EPOLL_CTL_DEL, clientFd_, nullptr);
        ::close(clientFd_);
        clientFd_ = -1;
    }
}

void TouchServer::readClient() {
    for (;;) {
        if (clientFd_ < 0) {
            return;
        }
        const ssize_t bytes = recv(clientFd_, received_ + receivedBytes_, kPacketSize - receivedBytes_, 0);
        if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EINTR)) {
            SAY_VERBOSE("the app went away");
            if (login_ == Login::kDone) {
                disengage(Reason::kRequested);
                stop(kExitClientGone);
            } else {
                dropClient();
            }
            return;
        }
        if (bytes < 0) {
            return;   // nothing more to read for now
        }
        receivedBytes_ += static_cast<int>(bytes);
        if (receivedBytes_ < kPacketSize) {
            continue;
        }
        receivedBytes_ = 0;
        RequestPacket packet;
        std::memcpy(&packet, received_, sizeof(packet));
        lastRequestNs_ = nowNs();
        if (!handleRequest(packet)) {
            return;
        }
    }
}

bool TouchServer::handleLogin(const RequestPacket& packet) {
    bool accepted = false;
    if (login_ == Login::kAwaitingHello) {
        accepted = packet.type == static_cast<uint8_t>(Request::kHello) && packet.a == kHelloMagic && packet.b == kVersion;
        login_ = Login::kAwaitingToken;
    } else {
        accepted = packet.type == static_cast<uint8_t>(Request::kToken) && packet.a == options_.token.first &&
                   packet.b == options_.token.second;
        login_ = Login::kDone;
    }
    if (!accepted) {
        say("a connection failed to log in");
        sendStatus(State::kStopped, Reason::kBadClient);
        dropClient();
        login_ = Login::kAwaitingHello;
        if (++badClients_ >= kMaxBadClients) {
            stop(kExitBadClient);
        }
        return false;
    }
    if (login_ != Login::kDone) {
        return true;
    }

    Reason reason = Reason::kNone;
    if (!openDevices(&reason)) {
        sendStatus(State::kStopped, reason);
        stop(exitCodeFor(reason));
        return false;
    }
    send(makeReady(screenInfo_.device.slotCount, rawRange_.maxX, rawRange_.maxY));
    sendStatus(State::kReleased);
    return true;
}

bool TouchServer::handleRequest(const RequestPacket& packet) {
    if (login_ != Login::kDone) {
        return handleLogin(packet);
    }
    switch (static_cast<Request>(packet.type)) {
        case Request::kConfigure:
            display_.width = packet.a;
            display_.height = packet.b;
            display_.rotation = packet.c;
            SAY_VERBOSE("display %d x %d, rotation %d", display_.width, display_.height, display_.rotation);
            break;
        case Request::kEngage:
            engage();
            break;
        case Request::kRelease:
            disengage(Reason::kRequested);
            break;
        case Request::kAimDown:
        case Request::kAimMove:
        case Request::kAimUp:
            handleAim(packet);
            break;
        case Request::kPing:
            break;   // arriving is the whole message: lastRequestNs_ was refreshed
        case Request::kQuit:
            stop(kExitOk);
            return false;
        default:
            SAY_VERBOSE("ignoring request type %d", packet.type);
            break;
    }
    return true;
}

void TouchServer::handleAim(const RequestPacket& packet) {
    if (phase_ != Phase::kMerging || !display_.isKnown()) {
        return;
    }
    if (static_cast<Request>(packet.type) == Request::kAimUp) {
        mux_->aimUp();
    } else {
        const float x = bitsToFloat(packet.a);
        const float y = bitsToFloat(packet.b);
        if (!std::isfinite(x) || !std::isfinite(y)) {
            return;
        }
        const RawPoint raw = displayToRaw(x, y, display_.width, display_.height, display_.rotation, rawRange_);
        if (static_cast<Request>(packet.type) == Request::kAimDown) {
            mux_->aimDown(raw.x, raw.y);
        } else {
            mux_->aimMove(raw.x, raw.y);
        }
    }
    checkVirtualDevice();
}

void TouchServer::send(const ReportPacket& packet) {
    if (clientFd_ < 0) {
        return;
    }
    // A report the app is too slow to take is simply skipped: the next one says the same.
    ::send(clientFd_, &packet, sizeof(packet), MSG_NOSIGNAL | MSG_DONTWAIT);
}

void TouchServer::sendStatus(State state, Reason reason) { send(makeStatus(state, reason)); }

// ---------------------------------------------------------------------------------------------
// The touchscreen
// ---------------------------------------------------------------------------------------------

void TouchServer::readTouchscreen() {
    TouchMux* target = phase_ == Phase::kMerging ? mux_.get() : observer_.get();
    drainTouchscreen(target);
    checkVirtualDevice();
    if (phase_ == Phase::kWaitingForFingers && observer_->realContactCount() == 0) {
        grabNow();
    }
}

void TouchServer::drainTouchscreen(TouchMux* target) {
    input_event events[kEventsPerRead];
    for (;;) {
        const int count = screen_.readEvents(events, kEventsPerRead);
        if (count < 0) {
            say("the touchscreen is gone");
            disengage(Reason::kDeviceLost);
            stop(kExitDeviceLost);
            return;
        }
        for (int i = 0; i < count; ++i) {
            target->onRealEvent(events[i].type, events[i].code, events[i].value);
        }
        if (target->needsResync()) {
            catchUp(target);
        }
        if (count < kEventsPerRead) {
            return;
        }
    }
}

void TouchServer::catchUp(TouchMux* target) {
    // Events were lost. Whatever is still queued is stale, so throw it away and read the screen's state afresh.
    input_event discarded[kEventsPerRead];
    while (screen_.readEvents(discarded, kEventsPerRead) == kEventsPerRead) {
    }
    ContactSnapshot snapshot;
    if (!screen_.readSnapshot(screenInfo_.device, &snapshot)) {
        say("cannot read the touchscreen's state");
        disengage(Reason::kDeviceLost);
        stop(kExitDeviceLost);
        return;
    }
    target->resync(snapshot);
}

void TouchServer::resetObserver() {
    observer_ = std::make_unique<TouchMux>(screenInfo_.device, &g_discard);
    ContactSnapshot snapshot;
    if (screen_.readSnapshot(screenInfo_.device, &snapshot)) {
        observer_->resync(snapshot);
    }
}

void TouchServer::readKeys(int fd) {
    input_event events[kEventsPerRead];
    for (;;) {
        const ssize_t bytes = read(fd, events, sizeof(events));
        if (bytes <= 0) {
            return;
        }
        const int count = static_cast<int>(bytes / static_cast<ssize_t>(sizeof(input_event)));
        for (int i = 0; i < count; ++i) {
            if (events[i].type != evdev::kEvKey) {
                continue;
            }
            const bool held = events[i].value != 0;
            if (events[i].code == kKeyVolumeUp) {
                volumeUpHeld_ = held;
            } else if (events[i].code == kKeyVolumeDown) {
                volumeDownHeld_ = held;
            }
        }
    }
}

void TouchServer::checkVirtualDevice() {
    if (const int error = virtual_.takeWriteError(); error != 0) {
        say("writing to the virtual touchscreen failed: %s", std::strerror(error));
        disengage(Reason::kDeviceLost);
        stop(kExitVirtualDevice);
    }
}

// ---------------------------------------------------------------------------------------------
// Taking the screen over and giving it back
// ---------------------------------------------------------------------------------------------

void TouchServer::engage() {
    if (phase_ == Phase::kMerging || !screen_.isOpen()) {
        return;
    }
    // A finger already on the screen would lose the end of its touch, so wait until all are up.
    if (observer_->realContactCount() == 0) {
        grabNow();
        return;
    }
    phase_ = Phase::kWaitingForFingers;
    sendStatus(State::kWaitingForFingers);
}

void TouchServer::grabNow() {
    if (const int error = screen_.grab(); error != 0) {
        say("cannot take over the touchscreen: %s", std::strerror(error));
        phase_ = Phase::kReleased;
        sendStatus(State::kReleased, Reason::kGrabFailed);
        return;
    }
    // Events that were already on their way when the grab began are still delivered here. If one
    // of them is a finger touching down, Android saw it too, so give the screen back and wait.
    drainTouchscreen(observer_.get());
    if (stopping_) {
        return;
    }
    if (observer_->realContactCount() > 0) {
        screen_.release();
        phase_ = Phase::kWaitingForFingers;
        sendStatus(State::kWaitingForFingers);
        return;
    }
    mux_ = std::make_unique<TouchMux>(screenInfo_.device, &virtual_);
    phase_ = Phase::kMerging;
    SAY_VERBOSE("touchscreen taken over");
    sendStatus(State::kMerging);
}

void TouchServer::disengage(Reason reason) {
    const bool wasActive = phase_ != Phase::kReleased;
    if (phase_ == Phase::kMerging && mux_) {
        // The virtual screen lets go of every finger first, so the game sees each touch end, and
        // only then does the real screen get its events back.
        mux_->releaseAll();
        mux_.reset();
        screen_.release();
        if (screen_.isOpen()) {
            resetObserver();
        }
    }
    phase_ = Phase::kReleased;
    if (wasActive) {
        SAY_VERBOSE("touchscreen given back (reason %d)", static_cast<int>(reason));
        sendStatus(State::kReleased, reason);
    }
}

}  // namespace aimbuddy::touch
