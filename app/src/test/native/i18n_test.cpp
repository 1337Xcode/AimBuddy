#include <cstring>

#include "test_harness.h"
#include "utils/i18n.h"

using aimbuddy::i18n::Key;
using aimbuddy::i18n::kTranslations;
using aimbuddy::i18n::Language;

namespace {

/// True if the table's English words for a key are exactly `expected`; a row that is missing is not.
bool saysInEnglish(Key key, const char* expected) {
    const char* text = kTranslations[static_cast<int>(key)][static_cast<int>(Language::English)];
    return text != nullptr && std::strcmp(text, expected) == 0;
}

}  // namespace

// The table is read by position, so a key added without its row (or a row without its key) would
// quietly give every key after it the words of the one before.
TEST(menu_text_lines_up_with_its_keys) {
    CHECK(saysInEnglish(Key::TabVisuals, "Visuals"));
    CHECK(saysInEnglish(Key::ShowAimPoint, "Aim point marker"));
    CHECK(saysInEnglish(Key::ShowDetectionArea, "Detection area outline"));
    CHECK(saysInEnglish(Key::ShowAimReach, "Aim reach circle"));
    CHECK(saysInEnglish(Key::ShowCount, "Target count"));
    CHECK(saysInEnglish(Key::BoxColor, "Box colour"));
    CHECK(saysInEnglish(Key::ResetAllConfirm, "Tap again to reset"));
}

TEST(every_menu_text_exists_in_every_language) {
    for (int key = 0; key < static_cast<int>(Key::Count); ++key) {
        for (int language = 0; language < static_cast<int>(Language::Count); ++language) {
            const char* text = kTranslations[key][language];
            CHECK(text != nullptr && text[0] != '\0');
        }
    }
}
