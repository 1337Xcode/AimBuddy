#include "touch_mux.h"

#include <algorithm>

namespace aimbuddy::touch {

using namespace evdev;

namespace {

/// Axes that describe a finger's size and pressure, which the extra finger gets typical values for.
constexpr uint16_t kStyleAxes[] = {kAbsMtTouchMajor, kAbsMtTouchMinor, kAbsMtWidthMajor,
                                   kAbsMtWidthMinor, kAbsMtPressure};

bool isStyleAxis(uint16_t code) {
    return std::find(std::begin(kStyleAxes), std::end(kStyleAxes), code) != std::end(kStyleAxes);
}

}  // namespace

TouchMux::TouchMux(const TouchDeviceInfo& info, EventSink* sink) : info_(info), sink_(sink) {
    // Until a real finger shows what its size and pressure look like, use a sixth of each range.
    for (const uint16_t code : kStyleAxes) {
        const AxisRange& range = info_.axis(code);
        if (range.present) {
            contactStyle_[axisIndex(code)] = range.minimum + (range.maximum - range.minimum) / 6;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------------------------

void TouchMux::selectSlot(int slot) {
    if (outputSlot_ != slot) {
        emit(kEvAbs, kAbsMtSlot, slot);
        outputSlot_ = slot;
    }
}

void TouchMux::writeAxis(int slot, uint16_t code, int32_t value) {
    selectSlot(slot);
    emit(kEvAbs, code, value);
    Contact& contact = contacts_[slot];
    contact.values[axisIndex(code)] = value;
    contact.known[axisIndex(code)] = true;
}

void TouchMux::startContact(int slot, int32_t realId) {
    Contact& contact = contacts_[slot];
    // A new contact in a slot that held one is a new pointer, so it never keeps the id the slot had.
    const int32_t previous = contact.active ? contact.outputId : -1;
    contact.active = false;
    const bool ownIdFree = slot != aimSlot() && realId != previous && !outputIdInUse(realId);
    contact.outputId = ownIdFree ? realId : nextFreeOutputId(previous);
    contact.active = true;
    contact.trackingId = realId;
    selectSlot(slot);
    emit(kEvAbs, kAbsMtTrackingId, contact.outputId);
    if (slot == aimSlot()) {
        return;
    }
    // The values the real screen will not repeat for this finger: those its slot already holds.
    for (uint16_t code = kMtAxisFirst; code <= kMtAxisLast; ++code) {
        const int index = axisIndex(code);
        if (code == kAbsMtTrackingId || !info_.axis(code).present || !realKnown_[slot][index]) {
            continue;
        }
        if (!contact.known[index] || contact.values[index] != realValues_[slot][index]) {
            writeAxis(slot, code, realValues_[slot][index]);
        }
    }
}

void TouchMux::endContact(int slot) {
    selectSlot(slot);
    emit(kEvAbs, kAbsMtTrackingId, -1);
    Contact& contact = contacts_[slot];
    contact.active = false;
    contact.trackingId = -1;
    contact.outputId = -1;
}

bool TouchMux::outputIdInUse(int32_t id) const {
    for (int slot = 0; slot <= info_.slotCount; ++slot) {
        if (contacts_[slot].active && contacts_[slot].outputId == id) {
            return true;
        }
    }
    return false;
}

int32_t TouchMux::nextFreeOutputId(int32_t avoid) {
    const int32_t maximum = std::max(info_.axis(kAbsMtTrackingId).maximum, 1);
    for (int attempt = 0; attempt <= kMaxRealSlots + 1; ++attempt) {
        lastPickedId_ = (lastPickedId_ + 1) % (maximum + 1);
        if (lastPickedId_ != avoid && !outputIdInUse(lastPickedId_)) {
            break;
        }
    }
    return lastPickedId_;
}

int TouchMux::activeContactCount() const {
    int count = 0;
    for (const Contact& contact : contacts_) {
        count += contact.active ? 1 : 0;
    }
    return count;
}

void TouchMux::finishFrame() {
    // BTN_TOUCH follows the number of contacts on the merged screen. Android reads a contact as
    // hovering while the button is off, so the real screen's own button must never be passed on:
    // it goes off when the player's last finger lifts even if the extra finger is still down.
    const bool touching = activeContactCount() > 0;
    if (touching != outputTouching_) {
        emit(kEvKey, kBtnTouch, touching ? 1 : 0);
        outputTouching_ = touching;
    }
    emit(kEvSyn, kSynReport, 0);
}

int32_t TouchMux::clampToAxis(uint16_t code, int32_t value) const {
    const AxisRange& range = info_.axis(code);
    return range.present ? std::clamp(value, range.minimum, range.maximum) : value;
}

// ---------------------------------------------------------------------------------------------
// The real screen
// ---------------------------------------------------------------------------------------------

void TouchMux::onRealEvent(uint16_t type, uint16_t code, int32_t value) {
    if (type == kEvSyn) {
        if (code == kSynDropped) {
            // The kernel ran out of room and lost events. Everything up to the next report is
            // stale; resync() reads the screen's true state instead.
            needsResync_ = true;
            inRealFrame_ = false;
        } else if (code == kSynReport && !needsResync_) {
            endRealFrame();
        }
        return;
    }
    // Keys (the real BTN_TOUCH among them), timestamps and everything else are not passed on.
    if (needsResync_ || type != kEvAbs) {
        return;
    }
    if (code == kAbsMtSlot) {
        currentRealSlot_ = (value >= 0 && value < info_.slotCount) ? value : -1;
        return;
    }
    if (!isContactAxis(code) || !info_.axis(code).present || currentRealSlot_ < 0) {
        return;
    }
    inRealFrame_ = true;
    applyRealAxis(code, value);
}

void TouchMux::applyRealAxis(uint16_t code, int32_t value) {
    const int slot = currentRealSlot_;
    if (code == kAbsMtTrackingId) {
        const Contact& contact = contacts_[slot];
        const bool lifting = value < 0;
        if ((lifting && !contact.active) || (!lifting && contact.active && contact.trackingId == value)) {
            return;  // nothing changes: the same fact reported twice
        }
        if (lifting) {
            endContact(slot);
        } else {
            startContact(slot, value);
        }
        return;
    }
    learnContactStyle(code, value);
    realValues_[slot][axisIndex(code)] = value;
    realKnown_[slot][axisIndex(code)] = true;
    writeAxis(slot, code, value);
}

void TouchMux::learnContactStyle(uint16_t code, int32_t value) {
    if (isStyleAxis(code) && value > info_.axis(code).minimum) {
        contactStyle_[axisIndex(code)] = value;
    }
}

void TouchMux::endRealFrame() {
    if (inRealFrame_) {
        finishFrame();
        inRealFrame_ = false;
    }
    // Commands for the extra finger that arrived in the middle of the group go out now, each as its own group.
    flushQueuedAim();
}

void TouchMux::flushQueuedAim() {
    for (int i = 0; i < queuedAimCount_; ++i) {
        applyAim(queuedAim_[i]);
    }
    queuedAimCount_ = 0;
}

void TouchMux::resync(const ContactSnapshot& snapshot) {
    needsResync_ = false;
    inRealFrame_ = false;
    currentRealSlot_ = (snapshot.currentSlot >= 0 && snapshot.currentSlot < info_.slotCount) ? snapshot.currentSlot : -1;
    for (int slot = 0; slot < info_.slotCount; ++slot) {
        realValues_[slot] = snapshot.slots[slot].values;
        realKnown_[slot].fill(true);
    }

    // Fingers that are gone (or were replaced) are lifted in one group, new ones put down in the
    // next, so a replaced finger is never mistaken for the same one moving.
    bool lifted = false;
    for (int slot = 0; slot < info_.slotCount; ++slot) {
        const ContactSnapshot::Slot& now = snapshot.slots[slot];
        const Contact& known = contacts_[slot];
        const bool replaced = known.active && now.trackingId >= 0 && known.trackingId != now.trackingId;
        if (known.active && (now.trackingId < 0 || replaced)) {
            endContact(slot);
            lifted = true;
        }
    }
    if (lifted) {
        finishFrame();
    }

    bool changed = false;
    for (int slot = 0; slot < info_.slotCount; ++slot) {
        const ContactSnapshot::Slot& now = snapshot.slots[slot];
        if (now.trackingId < 0) {
            continue;
        }
        if (!contacts_[slot].active) {
            startContact(slot, now.trackingId);
            changed = true;
        }
        for (uint16_t code = kMtAxisFirst; code <= kMtAxisLast; ++code) {
            if (code == kAbsMtTrackingId || !info_.axis(code).present) {
                continue;
            }
            const int index = axisIndex(code);
            const Contact& contact = contacts_[slot];
            if (!contact.known[index] || contact.values[index] != now.values[index]) {
                writeAxis(slot, code, now.values[index]);
                changed = true;
            }
        }
    }
    if (changed) {
        finishFrame();
    }
    // The extra finger's commands that waited for the catch up go out now, in the order they came.
    flushQueuedAim();
}

int TouchMux::realContactCount() const {
    int count = 0;
    for (int slot = 0; slot < info_.slotCount; ++slot) {
        count += contacts_[slot].active ? 1 : 0;
    }
    return count;
}

int TouchMux::copyRealContacts(RealContact* out, int capacity) const {
    const int xIndex = axisIndex(kAbsMtPositionX);
    const int yIndex = axisIndex(kAbsMtPositionY);
    int count = 0;
    for (int slot = 0; slot < info_.slotCount && count < capacity; ++slot) {
        const Contact& contact = contacts_[slot];
        if (contact.active && contact.known[xIndex] && contact.known[yIndex]) {
            out[count++] = {contact.trackingId, contact.values[xIndex], contact.values[yIndex]};
        }
    }
    return count;
}

// ---------------------------------------------------------------------------------------------
// The extra finger
// ---------------------------------------------------------------------------------------------

void TouchMux::aimDown(int32_t x, int32_t y) { submitAim({AimKind::kDown, x, y}); }

void TouchMux::aimMove(int32_t x, int32_t y) { submitAim({AimKind::kMove, x, y}); }

void TouchMux::aimUp() { submitAim({AimKind::kUp, 0, 0}); }

void TouchMux::submitAim(const AimCommand& command) {
    // A command that is not behind an earlier one goes straight out, unless the real screen's events are in the way.
    if (!inRealFrame_ && !needsResync_) {
        applyAim(command);
        return;
    }
    // In the middle of a group of events from the real screen, or while lost events are made good: wait
    // for the end of the group, or for resync(). Only the newest position matters, so a move replaces a
    // move that is still waiting.
    if (command.kind == AimKind::kMove && queuedAimCount_ > 0 &&
        queuedAim_[queuedAimCount_ - 1].kind == AimKind::kMove) {
        queuedAim_[queuedAimCount_ - 1] = command;
        return;
    }
    if (queuedAimCount_ == kMaxQueuedAim) {
        // Far more commands than one group of real events can take in practice; give up on
        // keeping the group whole rather than lose a finger-up.
        flushQueuedAim();
    }
    queuedAim_[queuedAimCount_++] = command;
}

void TouchMux::applyAim(const AimCommand& command) {
    switch (command.kind) {
        case AimKind::kDown:
            applyAimDown(command.x, command.y);
            break;
        case AimKind::kMove:
            applyAimMove(command.x, command.y);
            break;
        case AimKind::kUp:
            applyAimUp();
            break;
    }
}

void TouchMux::applyAimDown(int32_t x, int32_t y) {
    if (isAimDown()) {
        applyAimMove(x, y);
        return;
    }
    const int slot = aimSlot();
    startContact(slot, 0);
    for (const uint16_t code : kStyleAxes) {
        if (info_.axis(code).present) {
            writeAxis(slot, code, contactStyle_[axisIndex(code)]);
        }
    }
    if (info_.axis(kAbsMtToolType).present) {
        writeAxis(slot, kAbsMtToolType, 0);  // a finger
    }
    writeAxis(slot, kAbsMtPositionX, clampToAxis(kAbsMtPositionX, x));
    writeAxis(slot, kAbsMtPositionY, clampToAxis(kAbsMtPositionY, y));
    finishFrame();
}

void TouchMux::applyAimMove(int32_t x, int32_t y) {
    if (!isAimDown()) {
        applyAimDown(x, y);
        return;
    }
    const int slot = aimSlot();
    const Contact& contact = contacts_[slot];
    const int32_t newX = clampToAxis(kAbsMtPositionX, x);
    const int32_t newY = clampToAxis(kAbsMtPositionY, y);
    bool moved = false;
    if (contact.values[axisIndex(kAbsMtPositionX)] != newX) {
        writeAxis(slot, kAbsMtPositionX, newX);
        moved = true;
    }
    if (contact.values[axisIndex(kAbsMtPositionY)] != newY) {
        writeAxis(slot, kAbsMtPositionY, newY);
        moved = true;
    }
    if (moved) {
        finishFrame();
    }
}

void TouchMux::applyAimUp() {
    if (!isAimDown()) {
        return;
    }
    endContact(aimSlot());
    finishFrame();
}

// ---------------------------------------------------------------------------------------------
// Leaving
// ---------------------------------------------------------------------------------------------

void TouchMux::releaseAll() {
    queuedAimCount_ = 0;
    inRealFrame_ = false;
    bool lifted = false;
    for (int slot = 0; slot <= info_.slotCount; ++slot) {
        if (contacts_[slot].active) {
            endContact(slot);
            lifted = true;
        }
    }
    if (lifted) {
        finishFrame();
    }
}

}  // namespace aimbuddy::touch
