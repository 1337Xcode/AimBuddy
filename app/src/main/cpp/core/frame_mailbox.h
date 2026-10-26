#ifndef AIMBUDDY_CORE_FRAME_MAILBOX_H
#define AIMBUDDY_CORE_FRAME_MAILBOX_H

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace aimbuddy {

/**
 * Hands the newest value from one producer thread to one consumer thread.
 *
 * A value the consumer has not picked up yet is overwritten, so a slow consumer always sees
 * the latest frame, the producer never blocks, and nothing is allocated after construction.
 * Three slots are enough: one being written, one waiting, one being read.
 */
template <typename T>
class LatestMailbox {
public:
    /// Returns a slot the producer may fill. It is never the slot waiting or being read.
    T& beginWrite() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < kSlots; ++i) {
            if (i != latest_ && i != reading_) {
                writing_ = i;
                break;
            }
        }
        return slots_[writing_];
    }

    /// Marks the slot from beginWrite() as the newest value and wakes the consumer.
    void publish() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (latest_ != -1) {
                ++overwritten_;  // the consumer never saw the previous value
            }
            latest_ = writing_;
            writing_ = -1;
        }
        wake_.notify_one();
    }

    /// Waits for a value newer than the last one read. Returns nullptr on timeout or close().
    T* waitForLatest(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, timeout, [this] { return latest_ != -1 || closed_; });
        if (latest_ == -1) {
            return nullptr;
        }
        reading_ = latest_;
        latest_ = -1;
        return &slots_[reading_];
    }

    /// Releases the slot from waitForLatest() so the producer may reuse it.
    void endRead() {
        std::lock_guard<std::mutex> lock(mutex_);
        reading_ = -1;
    }

    /// Wakes a waiting consumer and makes waitForLatest() return nullptr until reopen().
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        wake_.notify_all();
    }

    void reopen() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = false;
        latest_ = -1;
    }

    /// Values replaced before the consumer read them (frames the pipeline could not keep up with).
    uint64_t overwrittenCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return overwritten_;
    }

private:
    static constexpr int kSlots = 3;

    std::array<T, kSlots> slots_{};
    int writing_ = -1;
    int latest_ = -1;
    int reading_ = -1;
    bool closed_ = false;
    uint64_t overwritten_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_FRAME_MAILBOX_H
