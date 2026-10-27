#ifndef AIMBUDDY_CORE_CAMERA_RESPONSE_H
#define AIMBUDDY_CORE_CAMERA_RESPONSE_H

namespace aimbuddy {

/**
 * How a game's camera answers a finger dragged across its look area.
 *
 * Every game and every sensitivity setting differs, so the numbers are not known in advance and
 * have to be measured (see ResponseEstimator). Until then they are a guess, and a wrong guess is
 * what makes the aim overshoot and swing.
 */
struct CameraResponse {
    float gainX = 1.5f;     ///< Screen pixels the scene slides sideways per pixel the finger moves.
    float gainY = 1.5f;     ///< The same for up and down.
    float delayS = 0.05f;   ///< Time from a finger move to its effect showing on the captured screen.
    bool invertY = false;   ///< The game looks up when the finger moves down.

    /**
     * How far the scene slides on screen for a finger move of (fingerDx, fingerDy) pixels.
     * Dragging the finger right turns the view right, so the scene slides left (negative x).
     */
    void sceneShift(float fingerDx, float fingerDy, float* shiftX, float* shiftY) const {
        *shiftX = -gainX * fingerDx;
        *shiftY = (invertY ? gainY : -gainY) * fingerDy;
    }
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_CORE_CAMERA_RESPONSE_H
