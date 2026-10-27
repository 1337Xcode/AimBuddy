#ifndef AIMBUDDY_OVERLAY_TARGET_OVERLAY_H
#define AIMBUDDY_OVERLAY_TARGET_OVERLAY_H

#include "../core/aim_point.h"
#include "../core/model_profile.h"
#include "../core/tracker.h"
#include "../imgui/imgui.h"

namespace aimbuddy {

/// How targets are outlined. Every field is a player-facing choice.
struct TargetStyle {
    ImU32 color = IM_COL32(255, 200, 0, 255);  ///< Outline colour; its alpha is the base opacity.
    float thickness = 2.0f;                    ///< Outline width in pixels.
    float gap = 2.0f;                          ///< Space between the target and its outline, so the outline never covers the target.
    bool cornerBrackets = false;               ///< Draw only the corners: less clutter on small targets.
    bool showScore = false;                    ///< Print the detector's confidence above the box.
    bool showAimPoint = true;                  ///< Mark the spot the assist steers towards.
    AimZone aimZone = AimZone::kHead;
    int lockedTrackId = -1;                    ///< The target the assist is following; drawn stronger.
    ImVec2 origin = ImVec2(0.0f, 0.0f);        ///< Where the overlay window's corner sits on the display; targets are in display pixels.
};

/**
 * Draws tracked targets on the overlay.
 *
 * Highlights are drawn with a thin dark halo under the coloured line, so they stay visible on any
 * background and for any kind of colour vision. They sit a small gap outside the target rather than
 * on its edge, which keeps the game's own pixels intact for the detector, because the overlay is
 * part of the screen the detector captures.
 */
class TargetOverlay {
public:
    /**
     * Draws every body target in `views`, fading each by its own opacity.
     *
     * `profile` says which classes are bodies and which are heads. A head, when the model detects
     * one, is used to place the aim marker.
     */
    static void draw(ImDrawList* list, const TrackView* views, int count, const ModelProfile& profile,
                     const TargetStyle& style);
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_OVERLAY_TARGET_OVERLAY_H
