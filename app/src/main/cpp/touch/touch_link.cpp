#include "touch_link.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace aimbuddy::touch {

using namespace protocol;

namespace {

/// Wait between attempts to reach a helper that has only just been started.
constexpr int kRetryIntervalMs = 100;

int64_t steadyNowNs() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;
}

int msUntil(int64_t deadlineNs) {
    const int64_t remaining = (deadlineNs - steadyNowNs()) / 1'000'000LL;
    return remaining > 0 ? static_cast<int>(remaining) : 0;
}

/// Opens a connection to 127.0.0.1:port, retrying until `deadlineNs`. Returns the socket or -1.
int openConnection(int port, int64_t deadlineNs) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    while (steadyNowNs() < deadlineNs) {
        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            return -1;
        }
        const int result = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        if (result == 0) {
            return fd;
        }
        if (errno == EINPROGRESS) {
            pollfd waiting{fd, POLLOUT, 0};
            int error = 0;
            socklen_t length = sizeof(error);
            if (poll(&waiting, 1, msUntil(deadlineNs)) > 0 &&
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0) {
                return fd;
            }
        }
        ::close(fd);
        usleep(kRetryIntervalMs * 1000);   // nothing is listening yet
    }
    return -1;
}

/// Sends one whole packet on a socket that may be non-blocking, waiting up to `deadlineNs`.
bool sendWhole(int fd, const RequestPacket& packet, int64_t deadlineNs) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&packet);
    size_t sent = 0;
    while (sent < sizeof(packet)) {
        const ssize_t n = ::send(fd, bytes + sent, sizeof(packet) - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            pollfd waiting{fd, POLLOUT, 0};
            if (poll(&waiting, 1, msUntil(deadlineNs)) <= 0) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

}  // namespace

Reason TouchLink::connect(int port, const Token& token, int timeoutMs) {
    disconnect();
    const int64_t deadline = steadyNowNs() + static_cast<int64_t>(timeoutMs) * 1'000'000LL;

    const int fd = openConnection(port, deadline);
    if (fd < 0) {
        return Reason::kUnreachable;
    }
    const int enable = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));

    state_.store(static_cast<uint8_t>(State::kStarting), std::memory_order_release);
    reason_.store(0, std::memory_order_release);
    ready_.store(false, std::memory_order_release);
    if (!sendWhole(fd, makeRequest(Request::kHello, kHelloMagic, kVersion), deadline) ||
        !sendWhole(fd, makeRequest(Request::kToken, token.first, token.second), deadline)) {
        ::close(fd);
        return Reason::kUnreachable;
    }

    // The helper answers once it has found the touchscreen and made the virtual one, or says it could not.
    uint8_t buffer[kPacketSize];
    int have = 0;
    while (!ready_.load(std::memory_order_acquire)) {
        if (state() == State::kStopped && reason() != Reason::kNone) {
            break;
        }
        pollfd waiting{fd, POLLIN, 0};
        if (poll(&waiting, 1, msUntil(deadline)) <= 0) {
            break;
        }
        const ssize_t n = ::recv(fd, buffer + have, sizeof(buffer) - static_cast<size_t>(have), 0);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            break;
        }
        if (n > 0) {
            have += static_cast<int>(n);
        }
        if (have == kPacketSize) {
            ReportPacket report;
            std::memcpy(&report, buffer, sizeof(report));
            handleReport(report);
            have = 0;
        }
    }

    if (!ready_.load(std::memory_order_acquire)) {
        ::close(fd);
        const Reason why = reason();
        return why != Reason::kNone ? why : Reason::kUnreachable;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fd_ = fd;
        receivedBytes_ = 0;
    }
    session_.fetch_add(1, std::memory_order_acq_rel);
    lastSendNs_.store(steadyNowNs(), std::memory_order_release);
    connected_.store(true, std::memory_order_release);
    return Reason::kNone;
}

void TouchLink::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ >= 0) {
        // Ask for a tidy shutdown; if the helper cannot be reached it notices the closed connection.
        sendLocked(makeRequest(Request::kQuit));
    }
    closeLocked();
}

void TouchLink::closeLocked() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    connected_.store(false, std::memory_order_release);
    ready_.store(false, std::memory_order_release);
    state_.store(static_cast<uint8_t>(State::kStopped), std::memory_order_release);
}

bool TouchLink::sendLocked(const RequestPacket& packet) {
    if (fd_ < 0) {
        return false;
    }
    const ssize_t n = ::send(fd_, &packet, sizeof(packet), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n == static_cast<ssize_t>(sizeof(packet))) {
        lastSendNs_.store(steadyNowNs(), std::memory_order_release);
        return true;
    }
    if (n < 0 && errno != EAGAIN && errno != EINTR) {
        closeLocked();   // the helper is gone
    }
    return false;
}

void TouchLink::configure(int displayWidth, int displayHeight, int rotation) {
    std::lock_guard<std::mutex> lock(mutex_);
    sendLocked(makeRequest(Request::kConfigure, displayWidth, displayHeight, rotation));
}

void TouchLink::engage() {
    std::lock_guard<std::mutex> lock(mutex_);
    sendLocked(makeRequest(Request::kEngage));
}

void TouchLink::release() {
    std::lock_guard<std::mutex> lock(mutex_);
    sendLocked(makeRequest(Request::kRelease));
}

void TouchLink::aimDown(float x, float y) {
    std::lock_guard<std::mutex> lock(mutex_);
    sendLocked(makeAimRequest(Request::kAimDown, x, y));
}

void TouchLink::aimMove(float x, float y) {
    std::lock_guard<std::mutex> lock(mutex_);
    sendLocked(makeAimRequest(Request::kAimMove, x, y));
}

void TouchLink::aimUp() {
    std::lock_guard<std::mutex> lock(mutex_);
    sendLocked(makeRequest(Request::kAimUp));
}

void TouchLink::ping() {
    std::lock_guard<std::mutex> lock(mutex_);
    sendLocked(makeRequest(Request::kPing));
}

void TouchLink::pollReports() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (fd_ >= 0) {
        const ssize_t n = ::recv(fd_, received_ + receivedBytes_,
                                 static_cast<size_t>(kPacketSize - receivedBytes_), MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            closeLocked();   // the helper closed the connection
            return;
        }
        if (n < 0) {
            return;   // nothing more for now
        }
        receivedBytes_ += static_cast<int>(n);
        if (receivedBytes_ == kPacketSize) {
            ReportPacket report;
            std::memcpy(&report, received_, sizeof(report));
            receivedBytes_ = 0;
            handleReport(report);
        }
    }
}

void TouchLink::handleReport(const ReportPacket& report) {
    if (report.type == static_cast<uint8_t>(Report::kReady)) {
        ready_.store(true, std::memory_order_release);
    } else if (report.type == static_cast<uint8_t>(Report::kStatus)) {
        state_.store(report.state, std::memory_order_release);
        reason_.store(report.reason, std::memory_order_release);
    }
}

}  // namespace aimbuddy::touch
