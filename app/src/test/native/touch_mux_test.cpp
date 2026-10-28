#include <algorithm>
#include <string>
#include <vector>

#include "core/touch_mux.h"
#include "test_harness.h"

using namespace aimbuddy::touch;
using namespace aimbuddy::touch::evdev;

namespace {

constexpr int kRealSlots = 10;
constexpr int kAimSlot = kRealSlots;   // the merged screen's extra slot
constexpr int kMaxX = 8639;
constexpr int kMaxY = 19199;

/// A touchscreen shaped like the one in the phone this was developed on.
TouchDeviceInfo makeDeviceInfo() {
    TouchDeviceInfo info;
    info.slotCount = kRealSlots;
    info.axis(kAbsMtPositionX) = {true, 0, kMaxX};
    info.axis(kAbsMtPositionY) = {true, 0, kMaxY};
    info.axis(kAbsMtTrackingId) = {true, 0, 65535};
    info.axis(kAbsMtTouchMajor) = {true, 0, 255};
    info.axis(kAbsMtToolType) = {true, 0, 0};
    info.axis(kAbsMtPressure) = {true, 0, 0};
    return info;
}

/// One finger as a reader of the merged screen sees it.
struct SeenContact {
    int slot;
    int trackingId;
    int x;
    int y;
    int touchMajor;
};

/// What the reader concluded when a group of events ended.
struct SeenFrame {
    std::vector<SeenContact> contacts;
    bool buttonTouch = false;

    const SeenContact* inSlot(int slot) const {
        for (const SeenContact& contact : contacts) {
            if (contact.slot == slot) {
                return &contact;
            }
        }
        return nullptr;
    }

    // Field accessors that give -1 when no finger is in the slot, so a missing finger fails a
    // check instead of crashing the test run.
    bool has(int slot) const { return inSlot(slot) != nullptr; }
    int xIn(int slot) const { return has(slot) ? inSlot(slot)->x : -1; }
    int yIn(int slot) const { return has(slot) ? inSlot(slot)->y : -1; }
    int idIn(int slot) const { return has(slot) ? inSlot(slot)->trackingId : -1; }
    int majorIn(int slot) const { return has(slot) ? inSlot(slot)->touchMajor : -1; }
};

/**
 * Reads the merged event stream the way Android's input reader does and records what it sees.
 *
 * It keeps per-slot values between contacts, treats a value for a slot that is not in use as a
 * mistake, and checks at every report that BTN_TOUCH agrees with the number of fingers, that
 * tracking ids are unique, and that every finger has both coordinates.
 */
class StreamReader : public EventSink {
public:
    explicit StreamReader(int slotCount) : slots_(static_cast<size_t>(slotCount)) {}

    void emit(uint16_t type, uint16_t code, int32_t value) override {
        if (type == kEvAbs) {
            readAbs(code, value);
        } else if (type == kEvKey && code == kBtnTouch) {
            buttonTouch_ = value != 0;
        } else if (type == kEvSyn && code == kSynReport) {
            recordFrame();
        }
    }

    const std::vector<SeenFrame>& frames() const { return frames_; }
    const std::vector<std::string>& problems() const { return problems_; }
    const SeenFrame& lastFrame() const { return frames_.empty() ? empty_ : frames_.back(); }
    void forgetFrames() { frames_.clear(); }

private:
    struct Slot {
        bool inUse = false;
        int trackingId = -1;
        int x = 0;
        int y = 0;
        int touchMajor = 0;
        bool hasX = false;
        bool hasY = false;
    };

    void readAbs(uint16_t code, int32_t value) {
        if (code == kAbsMtSlot) {
            currentSlot_ = value;
            if (value < 0 || value >= static_cast<int>(slots_.size())) {
                problems_.push_back("slot out of range");
            }
            return;
        }
        if (currentSlot_ < 0 || currentSlot_ >= static_cast<int>(slots_.size())) {
            problems_.push_back("value before any slot was selected");
            return;
        }
        Slot& slot = slots_[static_cast<size_t>(currentSlot_)];
        if (code == kAbsMtTrackingId) {
            if (value >= 0) {
                if (slot.inUse) {
                    problems_.push_back("tracking id given to a slot already in use");
                }
                slot.inUse = true;
                slot.trackingId = value;
            } else {
                if (!slot.inUse) {
                    problems_.push_back("lift of a slot that is not in use");
                }
                slot.inUse = false;
            }
            return;
        }
        if (!slot.inUse) {
            problems_.push_back("value written to a slot that is not in use");
        }
        if (code == kAbsMtPositionX) {
            slot.x = value;
            slot.hasX = true;
        } else if (code == kAbsMtPositionY) {
            slot.y = value;
            slot.hasY = true;
        } else if (code == kAbsMtTouchMajor) {
            slot.touchMajor = value;
        }
    }

    void recordFrame() {
        SeenFrame frame;
        frame.buttonTouch = buttonTouch_;
        for (size_t i = 0; i < slots_.size(); ++i) {
            const Slot& slot = slots_[i];
            if (!slot.inUse) {
                continue;
            }
            if (!slot.hasX || !slot.hasY) {
                problems_.push_back("finger reported without both coordinates");
            }
            frame.contacts.push_back({static_cast<int>(i), slot.trackingId, slot.x, slot.y, slot.touchMajor});
        }
        for (size_t a = 0; a < frame.contacts.size(); ++a) {
            for (size_t b = a + 1; b < frame.contacts.size(); ++b) {
                if (frame.contacts[a].trackingId == frame.contacts[b].trackingId) {
                    problems_.push_back("two fingers share a tracking id");
                }
            }
        }
        if (frame.buttonTouch != !frame.contacts.empty()) {
            problems_.push_back("BTN_TOUCH disagrees with the number of fingers");
        }
        frames_.push_back(frame);
    }

    std::vector<Slot> slots_;
    std::vector<SeenFrame> frames_;
    SeenFrame empty_;
    std::vector<std::string> problems_;
    int currentSlot_ = 0;
    bool buttonTouch_ = false;
};

/**
 * Feeds a TouchMux the way a protocol B kernel driver reports a touchscreen: a slot is only named
 * when it differs from the previous one, and the screen's own BTN_TOUCH is sent along.
 */
class FakeScreen {
public:
    explicit FakeScreen(TouchMux* mux) : mux_(mux) {}

    void down(int slot, int trackingId, int x, int y, int touchMajor = 30) {
        select(slot);
        send(kEvAbs, kAbsMtTrackingId, trackingId);
        send(kEvAbs, kAbsMtPositionX, x);
        send(kEvAbs, kAbsMtPositionY, y);
        send(kEvAbs, kAbsMtTouchMajor, touchMajor);
        send(kEvKey, kBtnTouch, 1);
        sync();
    }

    void move(int slot, int x, int y) {
        select(slot);
        send(kEvAbs, kAbsMtPositionX, x);
        send(kEvAbs, kAbsMtPositionY, y);
        sync();
    }

    void up(int slot) {
        select(slot);
        send(kEvAbs, kAbsMtTrackingId, -1);
        send(kEvKey, kBtnTouch, 0);
        sync();
    }

    void select(int slot) {
        if (slot != currentSlot_) {
            send(kEvAbs, kAbsMtSlot, slot);
            currentSlot_ = slot;
        }
    }

    void send(uint16_t type, uint16_t code, int32_t value) { mux_->onRealEvent(type, code, value); }
    void sync() { send(kEvSyn, kSynReport, 0); }

private:
    TouchMux* mux_;
    int currentSlot_ = 0;
};

/// A multiplexer with a reader on its output and a fake screen on its input.
struct Rig {
    Rig() : reader(kRealSlots + 1), mux(makeDeviceInfo(), &reader), screen(&mux) {}

    StreamReader reader;
    TouchMux mux;
    FakeScreen screen;
};

}  // namespace

TEST(mux_passes_a_real_finger_through_unchanged) {
    Rig rig;
    rig.screen.down(0, 7, 100, 200);
    rig.screen.move(0, 110, 210);
    rig.screen.up(0);

    const auto& frames = rig.reader.frames();
    CHECK(frames.size() == 3);
    if (frames.size() != 3) {
        return;
    }
    CHECK(frames[0].contacts.size() == 1 && frames[0].buttonTouch);
    CHECK(frames[0].idIn(0) == 7);
    CHECK(frames[0].xIn(0) == 100 && frames[0].yIn(0) == 200);
    CHECK(frames[1].xIn(0) == 110 && frames[1].yIn(0) == 210);
    CHECK(frames[2].contacts.empty() && !frames[2].buttonTouch);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_keeps_two_real_fingers_apart) {
    Rig rig;
    rig.screen.down(0, 1, 100, 100);
    rig.screen.down(1, 2, 7000, 15000);
    rig.screen.move(0, 120, 130);       // back to slot 0: the screen names it again
    rig.screen.move(1, 7100, 15100);

    const SeenFrame& last = rig.reader.lastFrame();
    CHECK(last.contacts.size() == 2);
    CHECK(last.xIn(0) == 120 && last.yIn(0) == 130);
    CHECK(last.xIn(1) == 7100 && last.yIn(1) == 15100);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_lists_the_fingers_the_player_has_down_and_never_the_extra_one) {
    Rig rig;
    TouchMux::RealContact fingers[kRealSlots];
    CHECK(rig.mux.copyRealContacts(fingers, kRealSlots) == 0);

    rig.screen.down(0, 4, 1000, 2000);
    rig.screen.down(3, 9, 7000, 15000);
    rig.mux.aimDown(6000, 9000);
    int count = rig.mux.copyRealContacts(fingers, kRealSlots);
    CHECK(count == 2);   // the extra finger is not the player's
    if (count == 2) {
        CHECK(fingers[0].trackingId == 4 && fingers[0].x == 1000 && fingers[0].y == 2000);
        CHECK(fingers[1].trackingId == 9 && fingers[1].x == 7000 && fingers[1].y == 15000);
    }

    rig.screen.move(0, 1100, 2300);
    rig.screen.up(3);
    count = rig.mux.copyRealContacts(fingers, kRealSlots);
    CHECK(count == 1);
    if (count == 1) {
        CHECK(fingers[0].trackingId == 4 && fingers[0].x == 1100 && fingers[0].y == 2300);
    }

    CHECK(rig.mux.copyRealContacts(fingers, 0) == 0);   // no room, none copied
}

TEST(mux_adds_the_extra_finger_in_its_own_slot) {
    Rig rig;
    rig.screen.down(0, 5, 1000, 2000);
    rig.mux.aimDown(6000, 9000);

    const SeenFrame& both = rig.reader.lastFrame();
    CHECK(both.contacts.size() == 2);
    CHECK(both.xIn(0) == 1000);
    CHECK(both.has(kAimSlot));
    CHECK(both.xIn(kAimSlot) == 6000 && both.yIn(kAimSlot) == 9000);
    CHECK(both.idIn(kAimSlot) != 5);
    CHECK(rig.mux.isAimDown());

    rig.mux.aimMove(6100, 9050);
    CHECK(rig.reader.lastFrame().xIn(kAimSlot) == 6100);
    CHECK(rig.reader.lastFrame().xIn(0) == 1000);

    rig.mux.aimUp();
    CHECK(rig.reader.lastFrame().contacts.size() == 1);
    CHECK(rig.reader.lastFrame().buttonTouch);
    CHECK(!rig.mux.isAimDown());
    CHECK(rig.reader.problems().empty());
}

TEST(mux_reselects_the_real_slot_after_the_extra_finger) {
    // The real screen does not name slot 3 again when it is unchanged, but the extra finger's
    // events selected another slot in between. Without a fresh selection the move would land on it.
    Rig rig;
    rig.screen.down(3, 9, 2000, 3000);
    rig.mux.aimDown(5000, 5000);
    rig.screen.send(kEvAbs, kAbsMtPositionX, 2100);
    rig.screen.send(kEvAbs, kAbsMtPositionY, 3100);
    rig.screen.sync();

    const SeenFrame& last = rig.reader.lastFrame();
    CHECK(last.xIn(3) == 2100 && last.yIn(3) == 3100);
    CHECK(last.xIn(kAimSlot) == 5000 && last.yIn(kAimSlot) == 5000);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_keeps_the_touch_button_down_while_any_finger_is_down) {
    Rig rig;
    rig.screen.down(0, 1, 100, 100);
    rig.mux.aimDown(500, 500);
    rig.screen.up(0);   // the real screen reports BTN_TOUCH 0 here, which must not reach the game

    CHECK(rig.reader.lastFrame().contacts.size() == 1);
    CHECK(rig.reader.lastFrame().buttonTouch);

    rig.mux.aimUp();
    CHECK(rig.reader.lastFrame().contacts.empty());
    CHECK(!rig.reader.lastFrame().buttonTouch);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_turns_the_touch_button_on_for_the_extra_finger_alone) {
    Rig rig;
    rig.mux.aimDown(300, 400);
    CHECK(rig.reader.lastFrame().buttonTouch);
    CHECK(rig.reader.lastFrame().contacts.size() == 1);
    rig.mux.aimUp();
    CHECK(!rig.reader.lastFrame().buttonTouch);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_waits_for_the_end_of_a_group_of_real_events) {
    // A real finger's group of events has begun but not ended. The extra finger must not cut in:
    // a group sliced in two would show the real finger without its second coordinate.
    Rig rig;
    rig.screen.send(kEvAbs, kAbsMtTrackingId, 4);
    rig.screen.send(kEvAbs, kAbsMtPositionX, 700);
    rig.mux.aimDown(6000, 6000);
    CHECK(rig.reader.frames().empty());   // nothing was written for the extra finger yet

    rig.screen.send(kEvAbs, kAbsMtPositionY, 800);
    rig.screen.sync();
    CHECK(rig.reader.frames().size() == 2);
    CHECK(rig.reader.frames()[0].contacts.size() == 1);   // the real finger, complete
    CHECK(rig.reader.frames()[1].contacts.size() == 2);   // then the extra finger joins
    CHECK(rig.reader.problems().empty());
}

TEST(mux_keeps_only_the_latest_of_waiting_moves) {
    Rig rig;
    rig.mux.aimDown(100, 100);
    rig.screen.send(kEvAbs, kAbsMtTrackingId, 4);
    rig.screen.send(kEvAbs, kAbsMtPositionX, 700);
    rig.screen.send(kEvAbs, kAbsMtPositionY, 800);
    rig.mux.aimMove(200, 200);
    rig.mux.aimMove(300, 300);
    rig.mux.aimMove(400, 400);
    rig.reader.forgetFrames();
    rig.screen.sync();

    CHECK(rig.reader.frames().size() == 2);   // the real group, then one move to the newest position
    CHECK(rig.reader.lastFrame().xIn(kAimSlot) == 400);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_applies_a_waiting_lift_after_the_group) {
    Rig rig;
    rig.mux.aimDown(100, 100);
    rig.screen.send(kEvAbs, kAbsMtTrackingId, 4);
    rig.screen.send(kEvAbs, kAbsMtPositionX, 700);
    rig.screen.send(kEvAbs, kAbsMtPositionY, 800);
    rig.mux.aimUp();
    rig.screen.sync();

    CHECK(!rig.mux.isAimDown());
    CHECK(rig.reader.lastFrame().contacts.size() == 1);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_gives_the_extra_finger_an_id_no_real_finger_uses) {
    // The extra finger's ids count up from 1, so a real finger holding id 1 must be avoided.
    Rig rig;
    rig.screen.down(0, 1, 100, 100);
    rig.mux.aimDown(500, 500);
    CHECK(rig.reader.lastFrame().idIn(kAimSlot) != 1);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_gives_the_extra_finger_a_new_id_each_time) {
    Rig rig;
    rig.mux.aimDown(500, 500);
    const int first = rig.reader.lastFrame().idIn(kAimSlot);
    rig.mux.aimUp();
    rig.mux.aimDown(500, 500);
    CHECK(rig.reader.lastFrame().idIn(kAimSlot) != first);
}

TEST(mux_gives_the_extra_finger_the_size_of_a_real_one) {
    Rig rig;
    rig.screen.down(0, 3, 100, 100, 42);
    rig.mux.aimDown(500, 500);
    CHECK(rig.reader.lastFrame().majorIn(kAimSlot) == 42);
}

TEST(mux_lifts_every_finger_on_release) {
    Rig rig;
    rig.screen.down(0, 1, 100, 100);
    rig.screen.down(1, 2, 300, 300);
    rig.mux.aimDown(500, 500);
    rig.mux.releaseAll();

    CHECK(rig.reader.lastFrame().contacts.empty());
    CHECK(!rig.reader.lastFrame().buttonTouch);
    CHECK(!rig.mux.isAimDown());
    CHECK(rig.mux.realContactCount() == 0);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_keeps_the_extra_finger_inside_the_screen) {
    Rig rig;
    rig.mux.aimDown(-50, 999999);
    CHECK(rig.reader.lastFrame().xIn(kAimSlot) == 0);
    CHECK(rig.reader.lastFrame().yIn(kAimSlot) == kMaxY);
    rig.mux.aimMove(kMaxX + 500, -5);
    CHECK(rig.reader.lastFrame().xIn(kAimSlot) == kMaxX);
    CHECK(rig.reader.lastFrame().yIn(kAimSlot) == 0);
}

TEST(mux_writes_nothing_when_the_extra_finger_stays_put) {
    Rig rig;
    rig.mux.aimDown(500, 500);
    const size_t before = rig.reader.frames().size();
    rig.mux.aimMove(500, 500);
    CHECK(rig.reader.frames().size() == before);
}

TEST(mux_treats_a_move_without_a_down_as_a_down) {
    Rig rig;
    rig.mux.aimMove(500, 500);
    CHECK(rig.mux.isAimDown());
    CHECK(rig.reader.lastFrame().contacts.size() == 1);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_ignores_slots_the_screen_does_not_have) {
    Rig rig;
    rig.screen.send(kEvAbs, kAbsMtSlot, 15);
    rig.screen.send(kEvAbs, kAbsMtTrackingId, 3);
    rig.screen.send(kEvAbs, kAbsMtPositionX, 100);
    rig.screen.sync();
    CHECK(rig.reader.frames().empty());
    CHECK(rig.mux.realContactCount() == 0);
}

TEST(mux_ignores_repeated_facts_from_the_real_screen) {
    Rig rig;
    rig.screen.down(0, 7, 100, 100);
    rig.screen.send(kEvAbs, kAbsMtTrackingId, 7);   // the same id again
    rig.screen.sync();
    rig.screen.up(0);
    rig.screen.send(kEvAbs, kAbsMtTrackingId, -1);  // a second lift
    rig.screen.sync();
    CHECK(rig.reader.problems().empty());
}

TEST(mux_catches_up_after_dropped_events) {
    Rig rig;
    rig.screen.down(0, 1, 100, 100);
    rig.screen.down(1, 2, 300, 300);

    // Events are lost. Stale events that follow are ignored until the state is read afresh.
    rig.screen.send(kEvSyn, kSynDropped, 0);
    CHECK(rig.mux.needsResync());
    rig.screen.select(0);
    rig.screen.send(kEvAbs, kAbsMtPositionX, 999);
    rig.screen.sync();
    CHECK(rig.mux.needsResync());

    // Meanwhile finger 1 moved on, finger 2 lifted and a new finger 3 touched down in slot 2.
    ContactSnapshot snapshot;
    snapshot.currentSlot = 2;
    snapshot.slots[0].trackingId = 1;
    snapshot.slots[0].values[axisIndex(kAbsMtPositionX)] = 150;
    snapshot.slots[0].values[axisIndex(kAbsMtPositionY)] = 160;
    snapshot.slots[2].trackingId = 3;
    snapshot.slots[2].values[axisIndex(kAbsMtPositionX)] = 4000;
    snapshot.slots[2].values[axisIndex(kAbsMtPositionY)] = 5000;
    rig.mux.resync(snapshot);

    CHECK(!rig.mux.needsResync());
    const SeenFrame& last = rig.reader.lastFrame();
    CHECK(last.contacts.size() == 2);
    CHECK(last.xIn(0) == 150 && last.yIn(0) == 160);
    CHECK(!last.has(1));
    CHECK(last.xIn(2) == 4000 && last.yIn(2) == 5000);
    CHECK(last.buttonTouch);

    // The screen carries on from slot 2 without naming it.
    rig.screen.send(kEvAbs, kAbsMtPositionX, 4100);
    rig.screen.send(kEvAbs, kAbsMtPositionY, 5100);
    rig.screen.sync();
    CHECK(rig.reader.lastFrame().xIn(2) == 4100);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_does_not_mistake_a_replaced_finger_for_a_moving_one) {
    Rig rig;
    rig.screen.down(0, 1, 100, 100);
    rig.screen.send(kEvSyn, kSynDropped, 0);

    ContactSnapshot snapshot;                    // slot 0 now holds a different finger
    snapshot.slots[0].trackingId = 8;
    snapshot.slots[0].values[axisIndex(kAbsMtPositionX)] = 900;
    snapshot.slots[0].values[axisIndex(kAbsMtPositionY)] = 900;
    rig.reader.forgetFrames();
    rig.mux.resync(snapshot);

    CHECK(rig.reader.frames().size() == 2);      // the old finger lifts, then the new one lands
    CHECK(rig.reader.frames()[0].contacts.empty());
    CHECK(rig.reader.frames()[1].contacts.size() == 1);
    CHECK(rig.reader.frames().size() >= 2 && rig.reader.frames()[1].idIn(0) == 8);
    CHECK(rig.reader.problems().empty());
}

TEST(mux_leaves_the_extra_finger_alone_during_a_resync) {
    Rig rig;
    rig.mux.aimDown(500, 500);
    rig.screen.send(kEvSyn, kSynDropped, 0);
    ContactSnapshot snapshot;    // no real fingers
    rig.mux.resync(snapshot);
    CHECK(rig.mux.isAimDown());
    CHECK(rig.reader.lastFrame().has(kAimSlot));
    CHECK(rig.reader.problems().empty());
}

TEST(mux_keeps_commands_in_order_when_a_group_was_lost_with_commands_waiting) {
    Rig rig;
    rig.mux.aimDown(100, 100);
    rig.screen.send(kEvAbs, kAbsMtTrackingId, 4);   // a group of real events begins, so the lift waits for its end
    rig.screen.send(kEvAbs, kAbsMtPositionX, 700);
    rig.mux.aimUp();
    rig.screen.send(kEvSyn, kSynDropped, 0);        // the end never comes: the kernel lost events
    rig.mux.aimDown(300, 300);                      // a newer command must not overtake the waiting lift

    ContactSnapshot snapshot;                       // no real fingers
    rig.mux.resync(snapshot);
    CHECK(rig.mux.isAimDown());
    CHECK(rig.reader.lastFrame().xIn(kAimSlot) == 300);

    rig.screen.down(0, 9, 50, 50);                  // the next real group must not bring the old lift out late
    CHECK(rig.mux.isAimDown());
    CHECK(rig.reader.problems().empty());
}

TEST(mux_files_a_first_finger_that_names_no_slot_under_the_slot_the_kernel_is_at) {
    // The kernel's current slot outlives a touch, and the next touch names no slot when it uses that one.
    // A multiplexer that starts at the screen's state has to know it, or the finger lands in slot 0.
    Rig rig;
    ContactSnapshot snapshot;
    snapshot.currentSlot = 2;
    rig.mux.resync(snapshot);

    rig.screen.send(kEvAbs, kAbsMtTrackingId, 5);
    rig.screen.send(kEvAbs, kAbsMtPositionX, 4000);
    rig.screen.send(kEvAbs, kAbsMtPositionY, 5000);
    rig.screen.sync();
    CHECK(rig.reader.lastFrame().xIn(2) == 4000 && rig.reader.lastFrame().yIn(2) == 5000);
    CHECK(!rig.reader.lastFrame().has(0));
    CHECK(rig.reader.problems().empty());
}

TEST(mux_declares_one_slot_more_than_the_real_screen) {
    Rig rig;
    CHECK(rig.mux.mergedSlotCount() == kRealSlots + 1);
}

TEST(device_info_must_be_a_protocol_b_touchscreen) {
    TouchDeviceInfo info = makeDeviceInfo();
    CHECK(info.isUsable());
    info.axis(kAbsMtTrackingId).present = false;
    CHECK(!info.isUsable());
    info = makeDeviceInfo();
    info.slotCount = 0;
    CHECK(!info.isUsable());
    info.slotCount = kMaxRealSlots + 1;
    CHECK(!info.isUsable());
}
