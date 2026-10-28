package com.aimbuddy.playground

import android.content.Intent

/**
 * How one run of the playground behaves, read from the extras of the intent that starts it, for
 * example `adb shell am start -n com.aimbuddy.debug/com.aimbuddy.playground.PlaygroundActivity --ef gain 6 --ei targets 5`.
 *
 * The playground stands in for a real game when the aim assist is tested: a first-person scene in
 * which a dragged finger turns the camera by a known amount after a known delay.
 */
internal data class PlaygroundConfig(
    /** Screen pixels the scene slides per pixel the finger moves. */
    val gain: Float = 3.0f,
    /** Time from a finger move to the camera turning. */
    val delayMs: Int = 60,
    /** Extra gain for fast drags, as a share of the gain at 3000 pixels per second (0 is a plain linear response). */
    val curve: Float = 0.0f,
    /** The game looks up when the finger moves down. */
    val invertY: Boolean = false,
    /** Only the first finger in the look area turns the camera, as in many games. */
    val singleLook: Boolean = false,
    /** Characters in the scene. */
    val targets: Int = 3,
    /** How they move: still, strafe or circle. */
    val pattern: String = "strafe",
    /** How far away the characters stand, in metres: nearer ones are bigger and easier for the detector. */
    val distance: Float = 11.0f,
    /** Seeds the scene, so two runs with the same extras look the same. */
    val seed: Int = 1,
    /** Hands the assist the true character boxes in place of the model's, so the model cannot cloud the result. */
    val synthetic: Boolean = false,
    /** Switches the aim assist on while the scene is open. */
    val assist: Boolean = false,
    /** Records the assist's steps and the measured slides of the picture while the scene is open (also done when [assist] is on). */
    val trace: Boolean = false,
    /** Has the assist drag its finger along a fixed path, to check what the app sees of it. Needs [assist]. */
    val fingerTest: Boolean = false,
    /** The scene takes no notice of this many looking touches, from the first, as a game may while the screen turns. */
    val ignoreTouches: Int = 0,
    /** Writes the picture the detector sees to `frame.ppm` this many milliseconds into the run; 0 writes none. */
    val dumpMs: Int = 0,
    /** Draws an outline of this colour (six hex digits such as `A0FF00`) round every character, as some games highlight enemies; 0 draws none. */
    val highlight: Int = 0,
) {
    companion object {
        fun from(intent: Intent) = PlaygroundConfig(
            gain = intent.getFloatExtra("gain", 3.0f),
            delayMs = intent.getIntExtra("delayMs", 60),
            curve = intent.getFloatExtra("curve", 0.0f),
            invertY = intent.getBooleanExtra("invertY", false),
            singleLook = intent.getBooleanExtra("singleLook", false),
            targets = intent.getIntExtra("targets", 3).coerceIn(0, 12),
            pattern = intent.getStringExtra("pattern") ?: "strafe",
            distance = intent.getFloatExtra("distance", 11.0f),
            seed = intent.getIntExtra("seed", 1),
            synthetic = intent.getBooleanExtra("synthetic", false),
            assist = intent.getBooleanExtra("assist", false),
            trace = intent.getBooleanExtra("trace", false),
            fingerTest = intent.getBooleanExtra("fingerTest", false),
            ignoreTouches = intent.getIntExtra("ignoreTouches", 0),
            dumpMs = intent.getIntExtra("dumpMs", 0),
            highlight = intent.getStringExtra("highlight")?.let { android.graphics.Color.parseColor("#" + it.removePrefix("#")) } ?: 0,
        )
    }
}
