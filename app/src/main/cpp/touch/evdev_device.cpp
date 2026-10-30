#include "evdev_device.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace aimbuddy::touch {

using namespace evdev;

// The constants in core/evdev_codes.h are copies, so that the logic using them builds anywhere.
// These checks make sure they match the kernel's.
static_assert(EV_SYN == kEvSyn && EV_KEY == kEvKey && EV_ABS == kEvAbs, "event types differ from the kernel's");
static_assert(SYN_REPORT == kSynReport && SYN_DROPPED == kSynDropped, "sync codes differ from the kernel's");
static_assert(BTN_TOUCH == kBtnTouch, "BTN_TOUCH differs from the kernel's");
static_assert(ABS_MT_SLOT == kAbsMtSlot && ABS_MT_TOUCH_MAJOR == kAbsMtTouchMajor &&
                  ABS_MT_POSITION_X == kAbsMtPositionX && ABS_MT_POSITION_Y == kAbsMtPositionY &&
                  ABS_MT_TOOL_TYPE == kAbsMtToolType && ABS_MT_TRACKING_ID == kAbsMtTrackingId &&
                  ABS_MT_PRESSURE == kAbsMtPressure && ABS_MT_TOOL_Y == kAbsMtToolY,
              "multi-touch axes differ from the kernel's");

namespace {

/// Ending the virtual device's name carries, so a left-over copy is never mistaken for a real screen.
constexpr const char kVirtualNameSuffix[] = "-ab";

bool isBitSet(const unsigned char* bits, int bit) { return (bits[bit / 8] & (1u << (bit % 8))) != 0; }

/// Reads a text property of an input device, such as its name.
std::string readText(int fd, unsigned long request) {
    char buffer[256] = {0};
    if (ioctl(fd, request, buffer) < 0) {
        return std::string();
    }
    return buffer;
}

/**
 * Describes the device at `path` if it is a touchscreen the multiplexer can copy, and says how
 * well it fits (see classifyTouchscreen).
 */
bool describeTouchscreen(const std::string& path, TouchscreenInfo* out, TouchscreenFit* fit) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    unsigned char absBits[ABS_MAX / 8 + 1] = {0};
    unsigned char propBits[INPUT_PROP_MAX / 8 + 1] = {0};
    unsigned char keyBits[KEY_MAX / 8 + 1] = {0};
    const bool readable = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) >= 0 &&
                          ioctl(fd, EVIOCGPROP(sizeof(propBits)), propBits) >= 0 &&
                          ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0;
    DeviceTraits traits;
    if (readable) {
        traits.direct = isBitSet(propBits, INPUT_PROP_DIRECT);
        traits.pointer = isBitSet(propBits, INPUT_PROP_POINTER);
        traits.touchButton = isBitSet(keyBits, BTN_TOUCH);
        traits.slots = isBitSet(absBits, ABS_MT_SLOT);
        traits.positions = isBitSet(absBits, ABS_MT_POSITION_X) && isBitSet(absBits, ABS_MT_POSITION_Y);
        traits.trackingId = isBitSet(absBits, ABS_MT_TRACKING_ID);
    }
    *fit = classifyTouchscreen(traits);
    if (*fit == TouchscreenFit::kNone) {
        ::close(fd);
        return false;
    }

    TouchscreenInfo info;
    info.path = path;
    info.name = readText(fd, EVIOCGNAME(256));
    info.phys = readText(fd, EVIOCGPHYS(256));
    ioctl(fd, EVIOCGID, &info.id);

    input_absinfo slotRange{};
    ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slotRange);
    info.device.slotCount = slotRange.maximum + 1;
    for (uint16_t code = kMtAxisFirst; code <= kMtAxisLast; ++code) {
        if (!isBitSet(absBits, code)) {
            continue;
        }
        input_absinfo range{};
        if (ioctl(fd, EVIOCGABS(code), &range) < 0) {
            continue;
        }
        info.absinfo[axisIndex(code)] = range;
        info.device.axis(code) = {true, range.minimum, range.maximum};
    }
    ::close(fd);

    const bool endsWithSuffix =
        info.name.size() >= sizeof(kVirtualNameSuffix) - 1 &&
        info.name.compare(info.name.size() - (sizeof(kVirtualNameSuffix) - 1), std::string::npos, kVirtualNameSuffix) == 0;
    if (!info.device.isUsable() || info.device.slotCount < 2 || endsWithSuffix) {
        return false;
    }
    *out = info;
    return true;
}

/// Paths of every /dev/input/event* node, in the order of their numbers (event2 before event10).
std::vector<std::string> listEventNodes() {
    std::vector<std::pair<int, std::string>> nodes;
    DIR* directory = opendir("/dev/input");
    if (directory == nullptr) {
        return {};
    }
    while (const dirent* entry = readdir(directory)) {
        if (std::strncmp(entry->d_name, "event", 5) == 0) {
            nodes.emplace_back(std::atoi(entry->d_name + 5), std::string("/dev/input/") + entry->d_name);
        }
    }
    closedir(directory);
    std::sort(nodes.begin(), nodes.end());
    std::vector<std::string> paths;
    paths.reserve(nodes.size());
    for (const auto& node : nodes) {
        paths.push_back(node.second);
    }
    return paths;
}

}  // namespace

int64_t TouchscreenInfo::area() const {
    const AxisRange& x = device.axis(kAbsMtPositionX);
    const AxisRange& y = device.axis(kAbsMtPositionY);
    return static_cast<int64_t>(x.maximum - x.minimum + 1) * static_cast<int64_t>(y.maximum - y.minimum + 1);
}

std::vector<TouchscreenInfo> findTouchscreens(int* deniedNodes) {
    std::vector<TouchscreenInfo> likely;
    std::vector<TouchscreenInfo> possible;
    for (const std::string& path : listEventNodes()) {
        if (access(path.c_str(), R_OK) != 0 && errno == EACCES && deniedNodes != nullptr) {
            ++*deniedNodes;
            continue;
        }
        TouchscreenInfo info;
        TouchscreenFit fit = TouchscreenFit::kNone;
        if (describeTouchscreen(path, &info, &fit)) {
            (fit == TouchscreenFit::kLikely ? likely : possible).push_back(info);
        }
    }
    // A device that looks like a phone's touchscreen wins; the looser matches are only for devices
    // that leave out what phones have (an emulator's screen, some old drivers).
    std::vector<TouchscreenInfo> found = likely.empty() ? possible : likely;
    // The main screen is the largest; a phone may also have small ones for a fingerprint reader or a pen.
    std::stable_sort(found.begin(), found.end(),
                     [](const TouchscreenInfo& a, const TouchscreenInfo& b) { return a.area() > b.area(); });
    return found;
}

std::vector<std::string> findVolumeKeyDevices() {
    std::vector<std::string> found;
    for (const std::string& path : listEventNodes()) {
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        unsigned char keyBits[KEY_MAX / 8 + 1] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0 &&
            (isBitSet(keyBits, KEY_VOLUMEUP) || isBitSet(keyBits, KEY_VOLUMEDOWN))) {
            found.push_back(path);
        }
        ::close(fd);
    }
    return found;
}

// ---------------------------------------------------------------------------------------------
// RealTouchscreen
// ---------------------------------------------------------------------------------------------

int RealTouchscreen::open(const std::string& path) {
    close();
    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    return fd_ >= 0 ? 0 : errno;
}

void RealTouchscreen::close() {
    if (fd_ >= 0) {
        release();
        ::close(fd_);
        fd_ = -1;
    }
}

int RealTouchscreen::grab() {
    if (fd_ < 0) {
        return ENODEV;
    }
    if (ioctl(fd_, EVIOCGRAB, 1) < 0) {
        return errno;
    }
    grabbed_ = true;
    return 0;
}

void RealTouchscreen::release() {
    if (fd_ >= 0 && grabbed_) {
        ioctl(fd_, EVIOCGRAB, 0);
    }
    grabbed_ = false;
}

int RealTouchscreen::readEvents(input_event* events, int capacity) {
    if (fd_ < 0) {
        return -1;
    }
    const ssize_t bytes = ::read(fd_, events, static_cast<size_t>(capacity) * sizeof(input_event));
    if (bytes < 0) {
        return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    }
    return static_cast<int>(bytes / static_cast<ssize_t>(sizeof(input_event)));
}

bool RealTouchscreen::readSnapshot(const TouchDeviceInfo& info, ContactSnapshot* snapshot) {
    input_absinfo slot{};
    if (fd_ < 0 || ioctl(fd_, EVIOCGABS(ABS_MT_SLOT), &slot) < 0) {
        return false;
    }
    *snapshot = ContactSnapshot();
    snapshot->currentSlot = slot.value;
    // EVIOCGMTSLOTS answers one axis for all slots: the code goes in, one value per slot comes out.
    struct {
        uint32_t code;
        int32_t values[kMaxRealSlots];
    } request;
    for (uint16_t code = kMtAxisFirst; code <= kMtAxisLast; ++code) {
        if (!info.axis(code).present) {
            continue;
        }
        request.code = code;
        std::fill(std::begin(request.values), std::end(request.values), -1);
        if (ioctl(fd_, EVIOCGMTSLOTS(sizeof(request)), &request) < 0) {
            return false;
        }
        for (int i = 0; i < info.slotCount && i < kMaxRealSlots; ++i) {
            if (code == kAbsMtTrackingId) {
                snapshot->slots[i].trackingId = request.values[i];
            }
            snapshot->slots[i].values[axisIndex(code)] = request.values[i];
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// VirtualTouchscreen
// ---------------------------------------------------------------------------------------------

int VirtualTouchscreen::create(const TouchscreenInfo& real, int slotCount) {
    destroy();
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return errno;
    }

    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
    if (!real.phys.empty()) {
        ioctl(fd, UI_SET_PHYS, real.phys.c_str());
    }

    uinput_user_dev description;
    std::memset(&description, 0, sizeof(description));
    const std::string name = real.name + kVirtualNameSuffix;
    std::strncpy(description.name, name.c_str(), UINPUT_MAX_NAME_SIZE - 1);
    description.id = real.id;

    ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    description.absmin[ABS_MT_SLOT] = 0;
    description.absmax[ABS_MT_SLOT] = slotCount - 1;
    for (uint16_t code = kMtAxisFirst; code <= kMtAxisLast; ++code) {
        if (!real.device.axis(code).present) {
            continue;
        }
        const input_absinfo& range = real.absinfo[axisIndex(code)];
        ioctl(fd, UI_SET_ABSBIT, code);
        description.absmin[code] = range.minimum;
        description.absmax[code] = range.maximum;
        // No fuzz: the kernel holds back or averages moves within the fuzz of the last value. The real screen's moves
        // reach this device already filtered by its own kernel, and the extra finger's are exact, so filtering again
        // would make both lag and swallow the extra finger's small steps.
        description.absfuzz[code] = 0;
        description.absflat[code] = range.flat;
    }
    // The merged screen's contact ids are its own (TouchMux gives them out), in a range of their own.
    description.absmin[ABS_MT_TRACKING_ID] = 0;
    description.absmax[ABS_MT_TRACKING_ID] = kMaxContactId;

    if (write(fd, &description, sizeof(description)) != static_cast<ssize_t>(sizeof(description)) ||
        ioctl(fd, UI_DEV_CREATE) < 0) {
        const int error = errno;
        ::close(fd);
        return error != 0 ? error : EIO;
    }
    fd_ = fd;
    pendingCount_ = 0;
    return 0;
}

void VirtualTouchscreen::destroy() {
    if (fd_ >= 0) {
        ioctl(fd_, UI_DEV_DESTROY);
        ::close(fd_);
        fd_ = -1;
    }
    pendingCount_ = 0;
}

void VirtualTouchscreen::emit(uint16_t type, uint16_t code, int32_t value) {
    if (fd_ < 0) {
        return;
    }
    input_event& event = pending_[static_cast<size_t>(pendingCount_++)];
    std::memset(&event, 0, sizeof(event));   // a zero time lets the kernel stamp the event
    event.type = type;
    event.code = code;
    event.value = value;
    if ((type == kEvSyn && code == kSynReport) || pendingCount_ == static_cast<int>(pending_.size())) {
        flush();
    }
}

void VirtualTouchscreen::flush() {
    if (pendingCount_ > 0 && fd_ >= 0) {
        const size_t bytes = static_cast<size_t>(pendingCount_) * sizeof(input_event);
        if (write(fd_, pending_.data(), bytes) != static_cast<ssize_t>(bytes)) {
            writeError_ = errno != 0 ? errno : EIO;   // the owner decides what a lost group means
        }
    }
    pendingCount_ = 0;
}

}  // namespace aimbuddy::touch
