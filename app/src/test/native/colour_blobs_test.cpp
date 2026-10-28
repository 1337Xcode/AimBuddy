#include <cmath>
#include <cstdint>
#include <vector>

#include "core/colour_blobs.h"
#include "test_harness.h"

using namespace aimbuddy;

namespace {

constexpr int kSide = 256;

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// Colours whose hue is known from their channels: red at 0 degrees, blue at 240, orange (255, 140, 0) at about 33.
constexpr Rgb kRed{255, 30, 30};
constexpr Rgb kBlue{30, 30, 255};

/// A square picture of dull, grey-ish noise, like a game's scenery, to draw colours on.
class Picture {
public:
    explicit Picture(uint32_t seed = 1, int side = kSide) : side_(side), rgba_(static_cast<size_t>(side) * side * 4, 255) {
        testing::Random random(seed);
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const int grey = 50 + static_cast<int>(random.uniform() * 80.0);
                const int tint = static_cast<int>(random.uniform() * 12.0) - 6;
                set(x, y, {static_cast<uint8_t>(grey + tint), static_cast<uint8_t>(grey), static_cast<uint8_t>(grey - tint)});
            }
        }
    }

    void set(int x, int y, Rgb colour) {
        if (x < 0 || y < 0 || x >= side_ || y >= side_) {
            return;
        }
        uint8_t* pixel = &rgba_[(static_cast<size_t>(y) * side_ + x) * 4];
        pixel[0] = colour.r;
        pixel[1] = colour.g;
        pixel[2] = colour.b;
        pixel[3] = 255;
    }

    void fillRect(int left, int top, int width, int height, Rgb colour) {
        for (int y = top; y < top + height; ++y) {
            for (int x = left; x < left + width; ++x) {
                set(x, y, colour);
            }
        }
    }

    /// The outline of a rectangle, `thickness` pixels wide, like the highlight a game draws round a player.
    void outline(int left, int top, int width, int height, int thickness, Rgb colour) {
        for (int y = top; y < top + height; ++y) {
            for (int x = left; x < left + width; ++x) {
                const bool edge = x < left + thickness || x >= left + width - thickness || y < top + thickness ||
                                  y >= top + height - thickness;
                if (edge) {
                    set(x, y, colour);
                }
            }
        }
    }

    const uint8_t* data() const { return rgba_.data(); }
    int side() const { return side_; }
    int stride() const { return side_ * 4; }

private:
    int side_;
    std::vector<uint8_t> rgba_;
};

ColourRange redRange() {
    ColourRange range;
    range.hueDegrees = 0.0f;
    return range;
}

int findIn(ColourBlobFinder* finder, const Picture& picture, const ColourRange& range, ColourBlob* blobs, int capacity = 8,
           const BlobFilter& filter = BlobFilter()) {
    return finder->find(picture.data(), picture.side(), picture.stride(), range, filter, blobs, capacity);
}

}  // namespace

TEST(colour_finds_the_outline_of_a_person_in_the_colour) {
    Picture picture;
    picture.outline(100, 60, 40, 110, 3, kRed);
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    const int count = findIn(&finder, picture, redRange(), blobs);
    CHECK(count == 1);
    if (count == 1) {
        CHECK(std::abs(blobs[0].left - 100) <= 2);
        CHECK(std::abs(blobs[0].top - 60) <= 2);
        CHECK(std::abs(blobs[0].right - 140) <= 2);
        CHECK(std::abs(blobs[0].bottom - 170) <= 2);
    }
}

TEST(colour_leaves_other_hues_alone_and_the_range_can_change_between_pictures) {
    Picture red;
    red.outline(100, 60, 40, 110, 3, kRed);
    Picture blue;
    blue.outline(100, 60, 40, 110, 3, kBlue);
    ColourRange blueRange;
    blueRange.hueDegrees = 240.0f;
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    CHECK(findIn(&finder, red, redRange(), blobs) == 1);
    CHECK(findIn(&finder, blue, redRange(), blobs) == 0);
    CHECK(findIn(&finder, blue, blueRange, blobs) == 1);
    CHECK(findIn(&finder, red, blueRange, blobs) == 0);
    CHECK(findIn(&finder, red, redRange(), blobs) == 1);   // the table is filled afresh each time the range changes
}

TEST(colour_measures_hue_round_the_wheel) {
    // Hue 5 degrees, looking for 355: 10 degrees apart through zero, inside the 14 allowed. (255, 30, 60) is 352 degrees.
    ColourRange range = redRange();
    range.hueDegrees = 5.0f;
    Picture near;
    near.outline(100, 60, 40, 110, 3, {255, 30, 60});
    Picture far;
    far.outline(100, 60, 40, 110, 3, {255, 30, 112});   // 338 degrees: 27 from 5
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    CHECK(findIn(&finder, near, range, blobs) == 1);
    CHECK(findIn(&finder, far, range, blobs) == 0);
}

TEST(colour_ignores_specks_and_small_patches) {
    Picture picture;
    testing::Random random(9);
    for (int i = 0; i < 60; ++i) {
        picture.set(static_cast<int>(random.uniform() * kSide), static_cast<int>(random.uniform() * kSide), kRed);
    }
    picture.fillRect(150, 150, 6, 6, kRed);
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    CHECK(findIn(&finder, picture, redRange(), blobs) == 0);
}

TEST(colour_keeps_only_patches_shaped_like_a_person) {
    ColourBlobFinder finder;
    ColourBlob blobs[8];

    Picture standing;
    standing.fillRect(100, 60, 30, 90, kRed);       // 90 tall, 30 wide: three times as tall as wide
    CHECK(findIn(&finder, standing, redRange(), blobs) == 1);

    Picture bar;
    bar.fillRect(60, 120, 120, 30, kRed);           // a quarter as tall as wide: not a person
    CHECK(findIn(&finder, bar, redRange(), blobs) == 0);

    Picture pole;
    pole.fillRect(120, 40, 4, 150, kRed);           // 37 times as tall as wide: not a person
    CHECK(findIn(&finder, pole, redRange(), blobs) == 0);

    Picture wholeFrame;
    wholeFrame.fillRect(0, 0, kSide, kSide, kRed);  // the frame's own colour, taller than the frame allows
    CHECK(findIn(&finder, wholeFrame, redRange(), blobs) == 0);
}

TEST(colour_joins_an_outline_that_the_scenery_broke) {
    Picture picture;
    picture.outline(100, 60, 40, 110, 3, kRed);
    // Three gaps of three pixels, one in each of three sides.
    picture.fillRect(100, 100, 3, 3, {90, 90, 90});
    picture.fillRect(137, 130, 3, 3, {90, 90, 90});
    picture.fillRect(115, 167, 3, 3, {90, 90, 90});
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    const int count = findIn(&finder, picture, redRange(), blobs);
    CHECK(count == 1);
    if (count == 1) {
        CHECK(std::abs(blobs[0].height() - 110) <= 2);
        CHECK(std::abs(blobs[0].width() - 40) <= 2);
    }
}

TEST(colour_joins_a_dotted_outline) {
    // A stippled highlight: dots two pixels square with three pixels between them, too small to count one by one.
    Picture picture;
    const int left = 100;
    const int top = 60;
    const int width = 40;
    const int height = 110;
    for (int x = left; x < left + width; x += 5) {
        picture.fillRect(x, top, 2, 2, kRed);
        picture.fillRect(x, top + height - 2, 2, 2, kRed);
    }
    for (int y = top; y < top + height; y += 5) {
        picture.fillRect(left, y, 2, 2, kRed);
        picture.fillRect(left + width - 2, y, 2, 2, kRed);
    }
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    const int count = findIn(&finder, picture, redRange(), blobs);
    CHECK(count == 1);
    if (count == 1) {
        CHECK(std::abs(blobs[0].height() - height) <= 4);
        CHECK(std::abs(blobs[0].width() - width) <= 4);
    }
}

TEST(colour_joins_the_head_and_the_body_of_one_person) {
    Picture picture;
    picture.outline(110, 40, 18, 18, 2, kRed);   // the head
    picture.outline(99, 64, 40, 80, 3, kRed);    // the body, six pixels below it
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    const int count = findIn(&finder, picture, redRange(), blobs);
    CHECK(count == 1);
    if (count == 1) {
        CHECK(std::abs(blobs[0].top - 40) <= 2);
        CHECK(std::abs(blobs[0].bottom - 144) <= 2);
        CHECK(std::abs(blobs[0].left - 99) <= 2);
        CHECK(std::abs(blobs[0].right - 139) <= 2);
    }
}

TEST(colour_keeps_two_people_apart) {
    Picture picture;
    picture.outline(40, 60, 40, 110, 3, kRed);
    picture.outline(170, 60, 40, 110, 3, kRed);
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    const int count = findIn(&finder, picture, redRange(), blobs);
    CHECK(count == 2);
    if (count == 2) {
        const int firstCentre = (blobs[0].left + blobs[0].right) / 2;
        const int secondCentre = (blobs[1].left + blobs[1].right) / 2;
        CHECK((firstCentre < 100 && secondCentre > 150) || (secondCentre < 100 && firstCentre > 150));
    }
}

TEST(colour_reports_the_biggest_first_and_no_more_than_asked) {
    Picture picture;
    picture.outline(20, 40, 30, 70, 3, kRed);      // the smallest
    picture.outline(90, 40, 44, 120, 3, kRed);     // the biggest
    picture.outline(180, 40, 36, 95, 3, kRed);
    ColourBlobFinder finder;
    ColourBlob all[8];
    const int count = findIn(&finder, picture, redRange(), all);
    CHECK(count == 3);
    if (count == 3) {
        CHECK(all[0].left >= 85 && all[0].left <= 95);   // the biggest first
        CHECK(all[1].left >= 175 && all[1].left <= 185);
    }
    ColourBlob two[2];
    CHECK(findIn(&finder, picture, redRange(), two, 2) == 2);
    CHECK(two[0].left >= 85 && two[0].left <= 95);
}

TEST(colour_keeps_the_box_of_a_person_cut_by_the_frame_edge) {
    Picture picture;
    picture.outline(-10, 60, 40, 110, 3, kRed);   // the left part is off the frame
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    const int count = findIn(&finder, picture, redRange(), blobs);
    CHECK(count == 1);
    if (count == 1) {
        CHECK(blobs[0].left == 0);
        CHECK(std::abs(blobs[0].right - 30) <= 2);
    }
}

TEST(colour_finds_nothing_in_a_picture_it_cannot_read) {
    Picture picture;
    picture.outline(100, 60, 40, 110, 3, kRed);
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    CHECK(finder.find(nullptr, kSide, kSide * 4, redRange(), BlobFilter(), blobs, 8) == 0);
    CHECK(finder.find(picture.data(), 0, kSide * 4, redRange(), BlobFilter(), blobs, 8) == 0);
    CHECK(finder.find(picture.data(), kSide, kSide * 2, redRange(), BlobFilter(), blobs, 8) == 0);   // rows too short for the pixels
    CHECK(finder.find(picture.data(), kSide, kSide * 4, redRange(), BlobFilter(), blobs, 0) == 0);   // no room for an answer
}

TEST(learning_takes_the_most_vivid_colour_near_the_crosshair) {
    Picture picture;
    picture.fillRect(90, 90, 40, 40, {150, 110, 80});     // a large, dull brown: vivid enough to count, but far from the most vivid
    picture.outline(100, 80, 30, 70, 3, {255, 140, 0});   // a thin orange outline, 33 degrees
    ColourRange learned;
    CHECK(learnColour(picture.data(), kSide, picture.stride(), 115, 115, 40, &learned));
    CHECK_NEAR(learned.hueDegrees, 33.0, 4.0);
    CHECK_NEAR(learned.minSaturation, 0.6, 0.05);   // 60% of the outline's full saturation
    CHECK_NEAR(learned.minValue, 0.6, 0.05);
    CHECK_NEAR(learned.hueToleranceDegrees, ColourRange().hueToleranceDegrees, 1e-6);   // the tolerance is the player's to set
}

TEST(learning_finds_nothing_in_a_picture_without_a_vivid_colour) {
    Picture picture;
    picture.fillRect(80, 80, 60, 60, {120, 110, 100});   // dull
    ColourRange learned;
    CHECK(!learnColour(picture.data(), kSide, picture.stride(), 110, 110, 40, &learned));
    CHECK(!learnColour(nullptr, kSide, kSide * 4, 110, 110, 40, &learned));
}

TEST(learning_reads_a_hue_that_straddles_zero) {
    Picture picture;
    picture.fillRect(100, 100, 20, 40, {255, 0, 22});    // 354 degrees
    picture.fillRect(120, 100, 20, 40, {255, 22, 0});    // 5 degrees
    ColourRange learned;
    CHECK(learnColour(picture.data(), kSide, picture.stride(), 120, 120, 40, &learned));
    const float fromZero = std::min(learned.hueDegrees, 360.0f - learned.hueDegrees);
    CHECK(fromZero < 6.0f);
}

TEST(what_was_learned_finds_the_outline_it_was_learned_from) {
    Picture picture;
    picture.outline(100, 60, 40, 110, 3, {200, 60, 230});   // a purple outline
    ColourRange learned;
    CHECK(learnColour(picture.data(), kSide, picture.stride(), 120, 115, 70, &learned));
    ColourBlobFinder finder;
    ColourBlob blobs[8];
    const int count = findIn(&finder, picture, learned, blobs);
    CHECK(count == 1);
    if (count == 1) {
        CHECK(std::abs(blobs[0].left - 100) <= 2);
        CHECK(std::abs(blobs[0].bottom - 170) <= 2);
    }
}
