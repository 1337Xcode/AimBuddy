#include "core/touchscreen_fit.h"
#include "test_harness.h"

using namespace aimbuddy::touch;

namespace {

// A phone's touchscreen: direct, with a touch button and slots.
DeviceTraits phoneScreen() {
    DeviceTraits traits;
    traits.direct = true;
    traits.touchButton = true;
    traits.slots = true;
    traits.positions = true;
    traits.trackingId = true;
    return traits;
}

}  // namespace

TEST(a_phone_touchscreen_is_a_likely_fit) {
    CHECK(classifyTouchscreen(phoneScreen()) == TouchscreenFit::kLikely);
}

TEST(a_multi_touch_device_without_the_direct_flag_is_a_possible_fit) {
    DeviceTraits traits = phoneScreen();
    traits.direct = false;
    CHECK(classifyTouchscreen(traits) == TouchscreenFit::kPossible);
}

TEST(the_emulator_screen_without_a_touch_button_is_a_possible_fit) {
    DeviceTraits traits = phoneScreen();
    traits.direct = false;
    traits.touchButton = false;
    CHECK(classifyTouchscreen(traits) == TouchscreenFit::kPossible);
}

TEST(a_touchpad_is_never_a_fit) {
    DeviceTraits traits = phoneScreen();
    traits.pointer = true;
    CHECK(classifyTouchscreen(traits) == TouchscreenFit::kNone);
}

TEST(a_device_without_slots_or_positions_is_not_a_fit) {
    DeviceTraits noSlots = phoneScreen();
    noSlots.slots = false;
    CHECK(classifyTouchscreen(noSlots) == TouchscreenFit::kNone);
    DeviceTraits noPositions = phoneScreen();
    noPositions.positions = false;
    CHECK(classifyTouchscreen(noPositions) == TouchscreenFit::kNone);
    DeviceTraits noTrackingId = phoneScreen();
    noTrackingId.trackingId = false;
    CHECK(classifyTouchscreen(noTrackingId) == TouchscreenFit::kNone);
}
