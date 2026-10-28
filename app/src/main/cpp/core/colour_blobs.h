#ifndef AIMBUDDY_CORE_COLOUR_BLOBS_H
#define AIMBUDDY_CORE_COLOUR_BLOBS_H

#include <array>
#include <cstdint>
#include <vector>

namespace aimbuddy {

/// A colour to look for: a hue, and how vivid and bright a pixel has to be to count as it.
struct ColourRange {
    float hueDegrees = 0.0f;            ///< 0 to 360 (0 red, 120 green, 240 blue).
    float hueToleranceDegrees = 14.0f;  ///< How far either side of the hue still counts.
    float minSaturation = 0.5f;         ///< 0 to 1: greys and washed out colours fall below it.
    float minValue = 0.4f;              ///< 0 to 1: dark colours fall below it.

    bool operator==(const ColourRange& other) const {
        return hueDegrees == other.hueDegrees && hueToleranceDegrees == other.hueToleranceDegrees &&
               minSaturation == other.minSaturation && minValue == other.minValue;
    }
};

/// A patch of the colour that may be a person, in the pixels of the frame it was found in. The edges are
/// pixel boundaries: the patch covers columns left to right - 1 and rows top to bottom - 1.
struct ColourBlob {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int pixels = 0;   ///< How many pixels of the colour it holds (after small gaps were closed).

    int width() const { return right - left; }
    int height() const { return bottom - top; }
};

/// What a patch must look like to be taken for a person.
struct BlobFilter {
    int minHeightPx = 16;           ///< Shorter patches are specks.
    float maxHeightShare = 0.95f;   ///< Taller than this share of the frame is not a person but the frame's own colour.
    float minAspect = 0.5f;         ///< Height over width: a person lying down is wide, a person standing is tall.
    float maxAspect = 6.0f;
    int minPixels = 20;             ///< Fewer pixels of the colour than this is noise.
};

/**
 * Finds patches of one colour in a picture, for telling players apart by an outline or a team colour
 * when no model is wanted.
 *
 * The picture is cut to a mask of the pixels that have the colour (a table, filled once for each
 * range, turns a pixel into yes or no); small gaps in it are closed, so that a thin outline broken
 * by the scenery stays one piece; its pieces are labelled; pieces that touch or lie close together
 * (the head and the body of one person, with a gap between) are joined; and what is left is kept
 * when its size and shape could be a person.
 *
 * It owns its working buffers and reuses them, so a call allocates nothing once the picture size
 * has been seen. Not thread safe: one finder for one stream of frames.
 */
class ColourBlobFinder {
public:
    /// The most patches reported from one picture.
    static constexpr int kMaxBlobs = 16;

    ColourBlobFinder();

    /**
     * Finds the patches of `range` in a square RGBA picture of `side` by `side` pixels whose rows are
     * `strideBytes` apart. Writes up to `capacity` patches, the biggest first, and returns how many.
     */
    int find(const uint8_t* rgba, int side, int strideBytes, const ColourRange& range, const BlobFilter& filter,
             ColourBlob* out, int capacity);

private:
    /// The table has an entry for every colour of 5, 6 and 5 bits: 1 when it is the colour looked for.
    static constexpr int kTableSize = 1 << 16;

    void fillTable(const ColourRange& range);
    void classify(const uint8_t* rgba, int side, int strideBytes);
    void closeGaps(int side, int radius);
    void slideWindow(int side, int radius, bool horizontal, bool wantSet);
    int labelPatches(int side, ColourBlob* patches, int capacity);
    static int joinNearbyPatches(ColourBlob* patches, int count);

    std::array<uint8_t, kTableSize> table_{};
    ColourRange tableRange_;
    bool tableFilled_ = false;
    std::vector<uint8_t> mask_;
    std::vector<uint8_t> scratch_;
    std::vector<int32_t> stack_;
};

/**
 * Looks at the square of 2 * half + 1 pixels around (centerX, centerY) and takes the most vivid colour
 * in it as the one to look for. The player aims the crosshair at an enemy's outline or colour and asks
 * for this; the outline is vivid and what is behind it mostly is not.
 *
 * Fills `range` with that hue and how vivid and bright it was (the tolerance is left as it was). Returns
 * false when nothing near the point is vivid enough to be a colour worth looking for.
 */
bool learnColour(const uint8_t* rgba, int side, int strideBytes, int centerX, int centerY, int half, ColourRange* range);

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_COLOUR_BLOBS_H
