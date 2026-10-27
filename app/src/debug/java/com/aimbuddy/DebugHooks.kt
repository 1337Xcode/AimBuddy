package com.aimbuddy

/**
 * Hooks that exist only in debug builds, for testing the aim assist against the playground scene
 * without a game and without the model. The native side is compiled in only when AIMBUDDY_DEBUG_HOOKS is on.
 */
object DebugHooks {
    init {
        System.loadLibrary("esp_native")
    }

    /** Takes the targets from [feedDetections] instead of from the detection model. */
    @JvmStatic
    external fun useSyntheticDetections(on: Boolean)

    /** The scene's true targets, five numbers each (x, y, width, height, score) in screen pixels, as of `sceneNs` (the steady clock). */
    @JvmStatic
    external fun feedDetections(boxes: FloatArray, sceneNs: Long)

    /** Switches the aim assist on or off, as the overlay menu does. */
    @JvmStatic
    external fun enableAssist(on: Boolean)
}
