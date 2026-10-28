#ifndef AIMBUDDY_TOUCH_TOUCH_MUX_H
#define AIMBUDDY_TOUCH_TOUCH_MUX_H

#include <array>
#include <cstdint>

#include "evdev_codes.h"

namespace aimbuddy::touch {

/// Most contacts a touchscreen can report. Android itself stops at 16 pointers.
constexpr int kMaxRealSlots = 32;

/// Value range of one axis of the touchscreen.
struct AxisRange {
    bool present = false;
    int32_t minimum = 0;
    int32_t maximum = 0;
};

/// What the multiplexer needs to know about the physical touchscreen.
struct TouchDeviceInfo {
    int slotCount = 0;                                    ///< Contacts the real screen can report at once.
    std::array<AxisRange, evdev::kMtAxisCount> axes{};    ///< Indexed with evdev::axisIndex().

    const AxisRange& axis(uint16_t code) const { return axes[evdev::axisIndex(code)]; }
    AxisRange& axis(uint16_t code) { return axes[evdev::axisIndex(code)]; }

    /// True for a protocol B touchscreen the multiplexer can work with.
    bool isUsable() const {
        return slotCount > 0 && slotCount <= kMaxRealSlots && axis(evdev::kAbsMtPositionX).present &&
               axis(evdev::kAbsMtPositionY).present && axis(evdev::kAbsMtTrackingId).present;
    }
};

/// Where the multiplexer writes the events of the merged touchscreen.
class EventSink {
public:
    virtual ~EventSink() = default;

    /// One input event. A group of events ends with EV_SYN / SYN_REPORT, which is the moment to flush.
    virtual void emit(uint16_t type, uint16_t code, int32_t value) = 0;
};

/// The touchscreen's contacts as the kernel reports them, used to catch up after events were lost.
struct ContactSnapshot {
    struct Slot {
        int32_t trackingId = -1;                           ///< -1 when no finger is in this slot.
        std::array<int32_t, evdev::kMtAxisCount> values{}; ///< Indexed with evdev::axisIndex().
    };
    std::array<Slot, kMaxRealSlots> slots{};
    int currentSlot = 0;   ///< Slot the real screen will report next, if it does not name one.
};

/**
 * Merges the player's fingers and the assist's finger into one touchscreen.
 *
 * Android's input dispatcher follows one input device at a time per window: when a second device
 * starts a touch while the first has a finger down, the first one's moves are dropped. So the
 * assist cannot simply be a second virtual screen next to the real one. Instead the real
 * touchscreen is taken over, every event it reports is passed on to a single virtual copy of it,
 * and the assist's finger is added to that copy as one extra contact. The game sees one screen with
 * the player's fingers plus one more.
 *
 * This class is only the bookkeeping. It reads events from the real screen (onRealEvent), takes
 * commands for the extra finger (aimDown / aimMove / aimUp) and writes the combined stream to an
 * EventSink. It does no input or output itself, which is why it can be tested on any machine.
 *
 * The output follows the multi-touch "protocol B" the real screen uses: contacts live in slots,
 * and a slot is selected before its values are written. The extra finger gets the slot after the
 * real ones. Three details are easy to get wrong and are handled here:
 *  - After the extra finger's events, the next slot-specific event from the real screen needs its
 *    slot selected again, because the real screen skips the selection when the slot is unchanged.
 *  - BTN_TOUCH must be on while any contact is down, whoever's it is. Android treats contacts as
 *    hovering otherwise, so the button is derived here, never copied.
 *  - The extra finger's events must not cut into the middle of a group of events from the real
 *    screen, so they wait for its next SYN_REPORT.
 *
 * Not thread safe: call everything from one thread.
 */
class TouchMux {
public:
    TouchMux(const TouchDeviceInfo& info, EventSink* sink);

    /// Number of slots the merged screen needs to declare: the real ones plus one.
    int mergedSlotCount() const { return info_.slotCount + 1; }

    /// Feeds one event read from the real touchscreen.
    void onRealEvent(uint16_t type, uint16_t code, int32_t value);

    /// True when the kernel reported dropped events; call resync() before feeding more.
    bool needsResync() const { return needsResync_; }

    /// Catches up with the real screen's current contacts after events were dropped.
    void resync(const ContactSnapshot& snapshot);

    /// Puts the extra finger down at a position given in the real screen's units.
    void aimDown(int32_t x, int32_t y);
    /// Slides the extra finger to a new position; puts it down first if it is up.
    void aimMove(int32_t x, int32_t y);
    /// Lifts the extra finger.
    void aimUp();

    /// Lifts every contact on the merged screen and forgets them. Used when taking leave of the screen.
    void releaseAll();

    bool isAimDown() const { return contacts_[aimSlot()].active; }
    int realContactCount() const;

    /// A finger the player has on the screen, as the real touchscreen last reported it.
    struct RealContact {
        int32_t trackingId = -1;
        int32_t x = 0;   ///< Raw units of the touchscreen.
        int32_t y = 0;
    };

    /// Copies the player's fingers that are down and whose position is known, up to `capacity` of them. Returns how many.
    int copyRealContacts(RealContact* out, int capacity) const;

private:
    struct Contact {
        bool active = false;
        int32_t trackingId = -1;
        std::array<int32_t, evdev::kMtAxisCount> values{};
        std::array<bool, evdev::kMtAxisCount> known{};   ///< Whether the value has been written for this contact.
    };

    enum class AimKind : uint8_t { kDown, kMove, kUp };
    struct AimCommand {
        AimKind kind = AimKind::kMove;
        int32_t x = 0;
        int32_t y = 0;
    };

    int aimSlot() const { return info_.slotCount; }

    // Output helpers.
    void emit(uint16_t type, uint16_t code, int32_t value) { sink_->emit(type, code, value); }
    void selectSlot(int slot);
    void writeAxis(int slot, uint16_t code, int32_t value);
    void writeTrackingId(int slot, int32_t trackingId);
    void finishFrame();
    int activeContactCount() const;

    // Real screen handling.
    void endRealFrame();
    void applyRealAxis(uint16_t code, int32_t value);
    void learnContactStyle(uint16_t code, int32_t value);

    // Extra finger handling.
    void submitAim(const AimCommand& command);
    void applyAim(const AimCommand& command);
    void applyAimDown(int32_t x, int32_t y);
    void applyAimMove(int32_t x, int32_t y);
    void applyAimUp();
    int32_t pickAimTrackingId();
    int32_t clampToAxis(uint16_t code, int32_t value) const;

    TouchDeviceInfo info_;
    EventSink* sink_;

    std::array<Contact, kMaxRealSlots + 1> contacts_{};
    std::array<int32_t, evdev::kMtAxisCount> contactStyle_{};   ///< Typical size and pressure of a real finger.

    int currentRealSlot_ = 0;      ///< Slot the real screen's next values belong to (-1 when out of range).
    int outputSlot_ = -1;          ///< Slot last selected on the merged screen (-1 when unknown).
    bool outputTouching_ = false;  ///< BTN_TOUCH as last written.
    bool inRealFrame_ = false;     ///< Part of a group of real events has been seen but not its SYN_REPORT.
    bool needsResync_ = false;
    int32_t lastAimTrackingId_ = 0;

    static constexpr int kMaxQueuedAim = 16;
    std::array<AimCommand, kMaxQueuedAim> queuedAim_{};
    int queuedAimCount_ = 0;
};

}  // namespace aimbuddy::touch

#endif  // AIMBUDDY_TOUCH_TOUCH_MUX_H
