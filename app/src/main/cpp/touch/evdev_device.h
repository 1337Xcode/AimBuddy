#ifndef AIMBUDDY_TOUCH_EVDEV_DEVICE_H
#define AIMBUDDY_TOUCH_EVDEV_DEVICE_H

#include <linux/input.h>

#include <array>
#include <string>
#include <vector>

#include "core/touch_mux.h"
#include "core/touchscreen_fit.h"

namespace aimbuddy::touch {

/// A touchscreen found under /dev/input, with everything needed to copy it.
struct TouchscreenInfo {
    std::string path;
    std::string name;
    std::string phys;
    input_id id{};
    TouchDeviceInfo device;
    std::array<input_absinfo, evdev::kMtAxisCount> absinfo{};   ///< Full range details, by axis index.

    int64_t area() const;
};

/**
 * Finds the touchscreens the calling user can read, largest first. If `deniedNodes` is given, it
 * receives the number of input devices that exist but may not be opened, which tells a missing
 * touchscreen from missing permission.
 */
std::vector<TouchscreenInfo> findTouchscreens(int* deniedNodes = nullptr);

/// Finds the input devices that have a volume key.
std::vector<std::string> findVolumeKeyDevices();

/**
 * The physical touchscreen, opened for reading.
 *
 * Reading does not disturb the system: Android reads the same events. Grabbing stops that, so the
 * events reach this program alone, and is the way the merged screen takes the real one's place.
 * The kernel lets the grab go by itself if this program dies.
 */
class RealTouchscreen {
public:
    RealTouchscreen() = default;
    RealTouchscreen(const RealTouchscreen&) = delete;
    RealTouchscreen& operator=(const RealTouchscreen&) = delete;
    ~RealTouchscreen() { close(); }

    /// Opens the device for non-blocking reads. Returns 0 or the errno that stopped it.
    int open(const std::string& path);
    void close();

    int fd() const { return fd_; }
    bool isOpen() const { return fd_ >= 0; }
    bool isGrabbed() const { return grabbed_; }

    /// Takes the screen away from the system. Returns 0 or the errno that stopped it.
    int grab();
    void release();

    /**
     * Reads the events that are waiting. Returns how many were read (0 when none), or -1 if the
     * device is gone.
     */
    int readEvents(input_event* events, int capacity);

    /// Reads the screen's current contacts from the kernel, for catching up after lost events.
    bool readSnapshot(const TouchDeviceInfo& info, ContactSnapshot* snapshot);

private:
    int fd_ = -1;
    bool grabbed_ = false;
};

/**
 * The virtual copy of the touchscreen that the merged events are written to.
 *
 * It is created with the real screen's identity and value ranges and one more slot, and every
 * event it receives is passed to Android as if a touchscreen had produced it. Events are held back
 * until the end of a group and written together, so a reader never sees half a group.
 */
class VirtualTouchscreen : public EventSink {
public:
    VirtualTouchscreen() = default;
    VirtualTouchscreen(const VirtualTouchscreen&) = delete;
    VirtualTouchscreen& operator=(const VirtualTouchscreen&) = delete;
    ~VirtualTouchscreen() override { destroy(); }

    /// Creates the device as a copy of `real` with `slotCount` slots. Returns 0 or the errno that stopped it.
    int create(const TouchscreenInfo& real, int slotCount);
    void destroy();

    bool isOpen() const { return fd_ >= 0; }

    void emit(uint16_t type, uint16_t code, int32_t value) override;

    /// The error of the last write that failed since this was last asked, or 0.
    int takeWriteError() {
        const int error = writeError_;
        writeError_ = 0;
        return error;
    }

private:
    void flush();

    /// Longest group of events held back before writing, in events.
    static constexpr size_t kPendingCapacity = 256;

    int fd_ = -1;
    std::array<input_event, kPendingCapacity> pending_{};
    int pendingCount_ = 0;
    int writeError_ = 0;
};

}  // namespace aimbuddy::touch

#endif  // AIMBUDDY_TOUCH_EVDEV_DEVICE_H
