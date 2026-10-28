#ifndef AIMBUDDY_TOUCH_TOUCH_LINK_H
#define AIMBUDDY_TOUCH_TOUCH_LINK_H

#include <atomic>
#include <cstdint>
#include <mutex>

#include "core/player_fingers.h"
#include "core/touch_protocol.h"

namespace aimbuddy::touch {

/**
 * The app's end of the connection to the touch helper.
 *
 * It sends the helper what to do with the extra finger and keeps track of what the helper says
 * about itself. Sending never blocks: if the helper cannot take a message, the message is dropped,
 * because every message is either replaced by the next one (positions, heartbeats) or repeated by
 * the caller (engage).
 *
 * Safe to use from several threads: the aim loop sends and polls, while the app's main thread
 * connects and disconnects.
 */
class TouchLink {
public:
    TouchLink() = default;
    TouchLink(const TouchLink&) = delete;
    TouchLink& operator=(const TouchLink&) = delete;
    ~TouchLink() { disconnect(); }

    /**
     * Connects to the helper listening on `port` and proves who is calling with `token`. Waits up
     * to `timeoutMs` for the helper to find the touchscreen and say it is ready.
     * Returns protocol::Reason::kNone on success, otherwise why not (kBadClient if nothing answered).
     */
    protocol::Reason connect(int port, const protocol::Token& token, int timeoutMs);

    /// Lets the helper go (it gives the touchscreen back) and closes the connection.
    void disconnect();

    bool isConnected() const { return connected_.load(std::memory_order_acquire); }

    /// Counts up with every connection, so a user can tell a new helper from the one it configured.
    uint32_t session() const { return session_.load(std::memory_order_acquire); }

    // --- Requests ---
    void configure(int displayWidth, int displayHeight, int rotation);
    void engage();
    void release();
    void aimDown(float x, float y);
    void aimMove(float x, float y);
    void aimUp();
    void ping();

    /// Reads what the helper has said, without waiting.
    void pollReports();

    // --- What the helper last reported ---
    protocol::State state() const { return static_cast<protocol::State>(state_.load(std::memory_order_acquire)); }
    protocol::Reason reason() const { return static_cast<protocol::Reason>(reason_.load(std::memory_order_acquire)); }
    bool isMerging() const { return state() == protocol::State::kMerging; }

    /// When the last request was sent (steady clock), so a caller can keep the heartbeat going.
    int64_t lastSendNs() const { return lastSendNs_.load(std::memory_order_acquire); }

    /// What the helper last said about the player's own fingers, and when it arrived (steady clock; 0 when nothing has).
    struct PlayerReport {
        PlayerFingers::Activity activity;
        int64_t atNs = 0;
    };
    PlayerReport playerReport() const;

private:
    bool sendLocked(const protocol::RequestPacket& packet);
    void handleReport(const protocol::ReportPacket& report);
    void closeLocked();

    mutable std::mutex mutex_;
    int fd_ = -1;
    uint8_t received_[protocol::kPacketSize] = {};
    int receivedBytes_ = 0;

    std::atomic<bool> connected_{false};
    std::atomic<bool> ready_{false};
    std::atomic<uint32_t> session_{0};
    std::atomic<uint8_t> state_{static_cast<uint8_t>(protocol::State::kStopped)};
    std::atomic<uint16_t> reason_{0};
    std::atomic<int64_t> lastSendNs_{0};

    mutable std::mutex playerMutex_;   ///< Guards the report below, which the aim loop reads while the poll writes it.
    PlayerReport player_;
};

}  // namespace aimbuddy::touch

#endif  // AIMBUDDY_TOUCH_TOUCH_LINK_H
