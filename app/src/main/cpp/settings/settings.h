#ifndef AIMBUDDY_SETTINGS_SETTINGS_H
#define AIMBUDDY_SETTINGS_SETTINGS_H

namespace aimbuddy {

/// Which route the app takes to the touchscreen. Automatic prefers Shizuku, then root.
enum class TouchBackend : int {
    kAuto = 0,
    kShizuku = 1,
    kRoot = 2,
};

/// The circle the assist's finger works in on one display, in display pixels.
struct TouchArea {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float radiusPx = 0.0f;
};

/**
 * Everything the player can change.
 *
 * It is plain data: take a copy from the SettingsStore, change it, hand it back. The values
 * are always kept inside their allowed ranges by clamp(), so no other code has to check them.
 */
struct Settings {
    // --- What is drawn ---
    float boxColor[3] = {1.0f, 0.2f, 0.2f};
    int boxThickness = 2;                 ///< Pixels, 1 to 6.
    bool showLabels = true;               ///< Confidence next to each box.
    bool showAimPoint = true;             ///< A marker where the assist aims.
    bool showSnapLine = false;            ///< A line to the nearest target...
    int snapLineFrom = 0;                 ///< ...from 0 the crosshair, 1 the middle of the top of the screen, 2 of its bottom.
    bool showCount = false;               ///< How many targets are in view.
    bool showPerformance = false;         ///< Detection rate and delay.
    bool showDetectionArea = true;        ///< A square around the area the detector looks at.
    bool showAimReach = true;             ///< A circle around the crosshair, as far as the assist reaches.

    // --- Detection ---
    float confidence = 0.5f;              ///< Lowest detector confidence that starts a target.
    float detectionZoneRadius = 200.0f;   ///< Pixels around the crosshair the detector looks at.
    int detector = 0;                     ///< What finds the targets: 0 the detection model, 1 a colour (experimental; needs no model). Read when the assist starts.
    bool colourLearned = false;           ///< A colour to look for has been learned, so the colour detector has something to find.
    float colourHue = 0.0f;               ///< The colour's hue in degrees, 0 to 360.
    float colourSaturation = 0.5f;        ///< The least saturated a pixel may be and still count, 0 to 1.
    float colourValue = 0.4f;             ///< The darkest a pixel may be and still count, 0 to 1.
    float colourTolerance = 14.0f;        ///< How far either side of the hue still counts, in degrees.
    float colourMinHeight = 40.0f;        ///< Patches of the colour shorter than this, in screen pixels, are not targets.

    // --- Aim assist ---
    bool assistEnabled = false;
    int aimZone = 0;                      ///< Part of a target to steer towards: 0 head, 1 chest, 2 centre.
    float aimStrength = 0.6f;             ///< 0 to 1: how quickly the aim closes on a target.
    float aimFovRadius = 220.0f;          ///< Targets further than this from the crosshair are left alone.
    int aimPriority = 0;                  ///< Which target to prefer: 0 closest to the crosshair, 1 biggest, 2 most certain.
    int aimAfterLanding = 0;              ///< Once on a target: 0 keep following it, 1 one correction and then leave it to the player.
    int aimSwitching = 1;                 ///< How readily the lock moves to another target: 0 sticky, 1 balanced, 2 quick.
    bool autoCalibrate = true;            ///< Measure how the game answers the assist finger instead of trusting the two numbers below.
    float sceneGain = 1.5f;               ///< How far the scene slides per pixel the assist finger moves (the guess used until measured).
    float actuationDelayMs = 50.0f;       ///< How long the game takes to react to the finger (the guess used until measured).
    bool invertY = false;                 ///< The game looks up when the finger moves down.
    float learnedGainX = 0.0f;            ///< What was measured last time; 0 means nothing was. Keeps the next start close.
    float learnedGainY = 0.0f;
    float learnedDelayMs = 0.0f;
    float learnedConfidence = 0.0f;       ///< 0 to 1: how well that measurement was backed.
    float anchorX = 0.78f;                ///< Where the assist finger rests, as a fraction of the display width...
    float anchorY = 0.45f;                ///< ...and height.
    float anchorRadius = 250.0f;          ///< The finger never travels further than this from its resting place.
    bool showAnchor = true;               ///< Draw that resting area on screen.
    float anchorOpacity = 0.3f;

    // --- App ---
    int touchBackend = static_cast<int>(TouchBackend::kAuto);
    bool streamerMode = false;            ///< Hide the overlay from screenshots and recordings.
    int language = 0;                     ///< 0 English, 1 Chinese, 2 Russian.

    /// Brings every value into its allowed range.
    void clamp();

    /// The settings a fresh install starts with. The language is kept so the screen stays readable.
    Settings resetKeepingLanguage() const;

    /**
     * Where the assist's finger really works on a display of this size: the resting place and radius set,
     * with the radius brought down to the nearest edge of the display. A finger sent past an edge stays on it
     * while the controller counts the whole move, and the camera is credited with a turn the game never saw.
     * A display of no size gives the settings as they are.
     */
    TouchArea touchAreaOn(float displayWidth, float displayHeight) const;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_SETTINGS_SETTINGS_H
