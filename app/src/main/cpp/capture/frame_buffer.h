#ifndef ESP_FRAME_BUFFER_H
#define ESP_FRAME_BUFFER_H

#include <atomic>
#include <array>
#include <android/hardware_buffer.h>
#include "../settings.h"
#include "../utils/logger.h"

/**
 * @file frame_buffer.h
 * @brief Lock-free SPSC ring buffer for frame handoff
 * 
 * Single Producer (capture thread) Single Consumer (inference thread)
 * lock-free queue using atomic indices. Pre-allocated storage for
 * zero heap allocation in hot path.
 */

namespace ESP {

/**
 * @struct Frame
 * @brief Frame data container
 */
struct Frame {
    AHardwareBuffer* hardwareBuffer;  ///< Hardware buffer pointer (zero-copy)
    int64_t timestamp;                ///< Capture timestamp in nanoseconds
    int width;                        ///< Frame width
    int height;                       ///< Frame height
    
    Frame() : hardwareBuffer(nullptr), timestamp(0), width(0), height(0) {}
};

/**
 * @class FrameBuffer
 * @brief Lock-free ring buffer for frame storage
 */
class FrameBuffer {
public:
    static_assert((Config::RING_BUFFER_CAPACITY & (Config::RING_BUFFER_CAPACITY - 1)) == 0,
                  "RING_BUFFER_CAPACITY must be a power of two for bitmask indexing");

    static constexpr size_t kCapacity = static_cast<size_t>(Config::RING_BUFFER_CAPACITY);
    static constexpr size_t kIndexMask = kCapacity - 1;

    FrameBuffer() : writeIndex_(0), readIndex_(0), droppedFrameCount_(0) {
        LOGD("FrameBuffer initialized with capacity %d", Config::RING_BUFFER_CAPACITY);
    }
    
    /**
     * @brief Push frame to buffer (producer)
     * @param frame Frame to push
     * @return true if successful, false if buffer full
     */
    bool push(const Frame& frame) {
        size_t currentWrite = writeIndex_.load(std::memory_order_relaxed);
        size_t nextWrite = (currentWrite + 1) & kIndexMask;
        
        // Check if buffer is full
        if (nextWrite == readIndex_.load(std::memory_order_acquire)) {
            droppedFrameCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        
        buffer_[currentWrite] = frame;
        writeIndex_.store(nextWrite, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Pop frame from buffer (consumer)
     * @param frame Output frame
     * @return true if successful, false if buffer empty
     */
    bool pop(Frame& frame) {
        size_t currentRead = readIndex_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (currentRead == writeIndex_.load(std::memory_order_acquire)) {
            return false;
        }
        
        frame = buffer_[currentRead];
        size_t nextRead = (currentRead + 1) & kIndexMask;
        readIndex_.store(nextRead, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Check if buffer is empty
     * @return true if empty
     */
    bool isEmpty() const {
        return readIndex_.load(std::memory_order_acquire) == 
               writeIndex_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Get current buffer size
     * @return Number of frames in buffer
     */
    size_t size() const {
        size_t write = writeIndex_.load(std::memory_order_acquire);
        size_t read = readIndex_.load(std::memory_order_acquire);
        
        if (write >= read) {
            return write - read;
        } else {
            return kCapacity - read + write;
        }
    }

    uint32_t consumeDroppedFrameCount() {
        return droppedFrameCount_.exchange(0, std::memory_order_relaxed);
    }

private:
    std::array<Frame, Config::RING_BUFFER_CAPACITY> buffer_;
    alignas(64) std::atomic<size_t> writeIndex_;
    alignas(64) std::atomic<size_t> readIndex_;
    alignas(64) std::atomic<uint32_t> droppedFrameCount_;
};

} // namespace ESP

#endif // ESP_FRAME_BUFFER_H
