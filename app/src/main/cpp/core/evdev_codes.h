#ifndef AIMBUDDY_TOUCH_EVDEV_CODES_H
#define AIMBUDDY_TOUCH_EVDEV_CODES_H

#include <cstdint>

namespace aimbuddy::touch::evdev {

// Event types and codes of the Linux input subsystem (linux/input-event-codes.h). They are spelled
// out here so the touch logic builds and is tested on a development machine without Linux headers;
// the code that talks to the kernel checks them against the real headers at compile time.
constexpr uint16_t kEvSyn = 0x00;
constexpr uint16_t kEvKey = 0x01;
constexpr uint16_t kEvAbs = 0x03;

constexpr uint16_t kSynReport = 0;
constexpr uint16_t kSynDropped = 3;

constexpr uint16_t kBtnTouch = 0x14a;

constexpr uint16_t kAbsMtSlot = 0x2f;
constexpr uint16_t kAbsMtTouchMajor = 0x30;
constexpr uint16_t kAbsMtTouchMinor = 0x31;
constexpr uint16_t kAbsMtWidthMajor = 0x32;
constexpr uint16_t kAbsMtWidthMinor = 0x33;
constexpr uint16_t kAbsMtOrientation = 0x34;
constexpr uint16_t kAbsMtPositionX = 0x35;
constexpr uint16_t kAbsMtPositionY = 0x36;
constexpr uint16_t kAbsMtToolType = 0x37;
constexpr uint16_t kAbsMtBlobId = 0x38;
constexpr uint16_t kAbsMtTrackingId = 0x39;
constexpr uint16_t kAbsMtPressure = 0x3a;
constexpr uint16_t kAbsMtDistance = 0x3b;
constexpr uint16_t kAbsMtToolX = 0x3c;
constexpr uint16_t kAbsMtToolY = 0x3d;

/// The per-contact axes of the multi-touch protocol, which are numbered consecutively.
constexpr uint16_t kMtAxisFirst = kAbsMtTouchMajor;
constexpr uint16_t kMtAxisLast = kAbsMtToolY;
constexpr int kMtAxisCount = kMtAxisLast - kMtAxisFirst + 1;

/// True for a code that describes one contact (everything from touch major to tool y).
constexpr bool isContactAxis(uint16_t code) { return code >= kMtAxisFirst && code <= kMtAxisLast; }

/// Position of an axis code in the per-contact arrays.
constexpr int axisIndex(uint16_t code) { return static_cast<int>(code) - static_cast<int>(kMtAxisFirst); }

}  // namespace aimbuddy::touch::evdev

#endif  // AIMBUDDY_TOUCH_EVDEV_CODES_H
