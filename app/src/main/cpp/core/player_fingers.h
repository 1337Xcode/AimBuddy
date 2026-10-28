#ifndef AIMBUDDY_CORE_PLAYER_FINGERS_H
#define AIMBUDDY_CORE_PLAYER_FINGERS_H

#include <array>
#include <cstdint>

namespace aimbuddy::touch {

/**
 * Follows the player's own fingers on the screen and says how fast the quickest one on each half of
 * the display is moving.
 *
 * The touch helper sees every real touch. From where the fingers are now and then it can tell
 * whether the player is steering the camera, which the assist needs to know so it does not fight
 * the player's own turn. Only speeds leave the helper, never positions.
 *
 * A finger that stays still sends no events, so its speed falls only when update() is called:
 * call it every few tens of milliseconds while any finger is down. Pure: it has no clock, no
 * input and no output, so it is tested on any machine.
 */
class PlayerFingers {
public:
    /// The most fingers followed at once; the touchscreen's own limit is 32.
    static constexpr int kMaxFingers = 32;

    /// One finger: the number the touchscreen gives it for as long as it stays down, and where it is, in display pixels.
    struct Finger {
        int32_t id = -1;
        float x = 0.0f;
        float y = 0.0f;
    };

    /// What the fingers are doing: how many are down, and the speed of the quickest on each half of the display, in pixels a second.
    struct Activity {
        int fingersDown = 0;
        float leftPxS = 0.0f;
        float rightPxS = 0.0f;
    };

    /// Takes the fingers that are on the screen at `nowNs` (steady clock). `displayWidth` tells which half a finger is on.
    void update(int64_t nowNs, const Finger* fingers, int count, float displayWidth);

    const Activity& activity() const { return activity_; }

    /// Forgets every finger, for a screen that was given back and taken again.
    void reset();

private:
    /// What is known of one finger: where it was when its speed was last worked out, and that speed.
    struct Track {
        int32_t id = -1;
        float x = 0.0f;
        float y = 0.0f;
        int64_t sinceNs = 0;
        float speedPxS = 0.0f;
    };

    const Track* findTrack(int32_t id) const;
    static Track advance(const Track& before, const Finger& finger, int64_t nowNs);

    std::array<Track, kMaxFingers> tracks_{};
    int trackCount_ = 0;
    Activity activity_;
};

}  // namespace aimbuddy::touch

#endif  // AIMBUDDY_CORE_PLAYER_FINGERS_H
