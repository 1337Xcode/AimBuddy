#ifndef AIMBUDDY_CORE_TOUCHSCREEN_FIT_H
#define AIMBUDDY_CORE_TOUCHSCREEN_FIT_H

namespace aimbuddy::touch {

/// What an input device says about itself, as far as telling a touchscreen from everything else.
struct DeviceTraits {
    bool direct = false;       ///< INPUT_PROP_DIRECT: the contacts are on the display itself.
    bool pointer = false;      ///< INPUT_PROP_POINTER: a touchpad that moves a cursor.
    bool touchButton = false;  ///< Has BTN_TOUCH.
    bool slots = false;        ///< Numbers its contacts in slots (ABS_MT_SLOT, protocol B).
    bool positions = false;    ///< Reports ABS_MT_POSITION_X and ABS_MT_POSITION_Y.
    bool trackingId = false;   ///< Reports ABS_MT_TRACKING_ID.
};

/// How well a device matches a phone's touchscreen.
enum class TouchscreenFit {
    kNone,      ///< Not a touchscreen the multiplexer can copy.
    kPossible,  ///< Multi-touch with slots, but it does not announce itself as a direct touchscreen (some drivers, emulators).
    kLikely,    ///< A direct multi-touch device with a touch button, which is what phones have.
};

/// Judges a device from what it reports. A touchpad never counts.
inline TouchscreenFit classifyTouchscreen(const DeviceTraits& traits) {
    if (!traits.slots || !traits.positions || !traits.trackingId || traits.pointer) {
        return TouchscreenFit::kNone;
    }
    return traits.direct && traits.touchButton ? TouchscreenFit::kLikely : TouchscreenFit::kPossible;
}

}  // namespace aimbuddy::touch

#endif  // AIMBUDDY_CORE_TOUCHSCREEN_FIT_H
