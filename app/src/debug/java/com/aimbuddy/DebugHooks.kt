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

    /** Starts keeping a row for every aim step (what the controller saw, decided and believed) and for every measured slide of the picture. */
    @JvmStatic
    external fun startTrace()

    /** Writes what [startTrace] kept as CSV: the aim steps to `aimPath` and the measured slides to `motionPath`. */
    @JvmStatic
    external fun writeTrace(aimPath: String, motionPath: String): Boolean

    /** Drags the assist's finger along a fixed path and writes where it was told to be to `path` as CSV. Needs the assist switched on. */
    @JvmStatic
    external fun runFingerScript(path: String)

    /** True once the assist knows the display is this size, which it learns a moment after the screen turns. */
    @JvmStatic
    external fun knowsDisplaySize(width: Int, height: Int): Boolean

    /** Switches the aim assist on or off, as the overlay menu does. */
    @JvmStatic
    external fun enableAssist(on: Boolean)
}
