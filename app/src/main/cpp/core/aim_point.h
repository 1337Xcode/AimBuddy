#ifndef AIMBUDDY_CORE_AIM_POINT_H
#define AIMBUDDY_CORE_AIM_POINT_H

#include <algorithm>

#include "model_profile.h"
#include "tracker.h"

namespace aimbuddy {

/// Which part of a target the assist steers towards.
enum class AimZone : int {
    kHead = 0,    ///< As high on the body as can be trusted: the head when it is visible.
    kChest = 1,   ///< The upper body: a larger, more forgiving target.
    kCenter = 2,  ///< The middle of the visible box.
};

struct AimPoint {
    float x = 0.0f;
    float y = 0.0f;
};

/**
 * Where on a target's visible box to aim, as a fraction of the box height from its top edge.
 *
 * The detector boxes only what is visible. A bot standing in the open shows head to feet (tall and
 * thin), so the head is near the top. A bot behind a wall or car shows less (head and shoulders, or
 * just a head or a hand), and the box gets wider relative to its height; there the head is no
 * longer a fixed distance from the top, so the point moves towards the centre of what can be seen.
 *
 * `heightToWidth` is the box height divided by its width.
 */
inline float aimFractionFromTop(float heightToWidth, AimZone zone) {
    if (zone == AimZone::kCenter) {
        return 0.5f;
    }
    // Values at a full standing figure, a head-and-torso view and a squat or sideways view.
    const float standing = zone == AimZone::kHead ? 0.10f : 0.27f;
    const float partial = zone == AimZone::kHead ? 0.30f : 0.40f;
    constexpr float kStandingRatio = 1.8f;   // tall and thin: a person seen head to knees or feet
    constexpr float kPartialRatio = 1.0f;    // as tall as wide: head and shoulders, a crouch
    constexpr float kSquatRatio = 0.8f;      // wider than tall: lying down, a hand or a vehicle window

    if (heightToWidth >= kStandingRatio) {
        return standing;
    }
    if (heightToWidth >= kPartialRatio) {
        const float t = (heightToWidth - kPartialRatio) / (kStandingRatio - kPartialRatio);
        return partial + (standing - partial) * t;
    }
    if (heightToWidth >= kSquatRatio) {
        const float t = (heightToWidth - kSquatRatio) / (kPartialRatio - kSquatRatio);
        return 0.5f + (partial - 0.5f) * t;
    }
    return 0.5f;
}

/**
 * The most certain head target that sits inside the upper part of `body`, or nullptr.
 * A head belongs to a body when its centre is within the body's width and no lower than the middle of it.
 */
inline const TrackView* findHeadFor(const TrackView& body, const TrackView* views, int count,
                                    const ModelProfile& profile) {
    const TrackView* best = nullptr;
    const float left = body.centerX - body.width * 0.5f;
    const float right = body.centerX + body.width * 0.5f;
    const float top = body.centerY - body.height * 0.5f;
    for (int i = 0; i < count; ++i) {
        const TrackView& candidate = views[i];
        if (profile.roleOf(candidate.classId) != ClassRole::kHead) {
            continue;
        }
        const bool inside = candidate.centerX >= left && candidate.centerX <= right &&
                            candidate.centerY >= top - candidate.height &&
                            candidate.centerY <= top + body.height * 0.5f;
        if (inside && (best == nullptr || candidate.score > best->score)) {
            best = &candidate;
        }
    }
    return best;
}

/**
 * Picks the point to aim at on a body target.
 *
 * When the model also detects heads and a head box sits inside the upper part of this body, the
 * head's centre is used because it is measured, not guessed. Otherwise the point is estimated from
 * the shape of the visible box with aimFractionFromTop().
 */
inline AimPoint chooseAimPoint(const TrackView& body, const TrackView* head, AimZone zone) {
    if (head != nullptr && zone != AimZone::kCenter) {
        const float halfW = body.width * 0.5f;
        const float top = body.centerY - body.height * 0.5f;
        const bool insideColumn = head->centerX >= body.centerX - halfW && head->centerX <= body.centerX + halfW;
        const bool inUpperHalf = head->centerY >= top - head->height && head->centerY <= top + body.height * 0.5f;
        if (insideColumn && inUpperHalf) {
            if (zone == AimZone::kHead) {
                return {head->centerX, head->centerY};
            }
            // Chest: just below the head, as far down as a head is tall.
            return {head->centerX, head->centerY + head->height * 1.5f};
        }
    }
    const float ratio = body.height / std::max(body.width, 1.0f);
    const float top = body.centerY - body.height * 0.5f;
    return {body.centerX, top + body.height * aimFractionFromTop(ratio, zone)};
}

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_AIM_POINT_H
