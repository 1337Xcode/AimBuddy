package com.aimbuddy.playground

import android.app.Activity
import android.content.pm.ActivityInfo
import android.graphics.Point
import android.os.Bundle
import android.util.Log
import android.view.View
import android.view.WindowManager
import com.aimbuddy.DebugHooks
import java.io.File

/**
 * A first-person scene for testing the aim assist without a game.
 *
 * It is only part of debug builds. A finger dragged across it turns the camera by a known amount
 * after a known delay (see [PlaygroundConfig]), and characters stand or move in it. Every frame
 * the true distance from the crosshair to the nearest character's head is written to
 * `playground.csv`, so a run can be judged without trusting anything the assist reports.
 */
class PlaygroundActivity : Activity() {

    private companion object {
        const val TAG = "Playground"
        const val POLL_MS = 100
        const val DISPLAY_WAIT_MS = 10_000

        /** Counts the scenes opened, so a scene that is closing late leaves a newer one's test alone. */
        var generation = 0
    }

    private var myGeneration = 0

    private lateinit var scene: PlaygroundView
    private lateinit var config: PlaygroundConfig

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        config = PlaygroundConfig.from(intent)
        myGeneration = ++generation
        val logFile = File(getExternalFilesDir(null), "playground.csv")
        logFile.delete()   // each run starts a fresh record
        scene = PlaygroundView(this, config, logFile)
        setContentView(scene)
        // The assist learns the new screen size a moment after the display turns to landscape. Until
        // it has, it would read the scene against the wrong screen, so the test waits for it.
        scene.post(object : Runnable {
            private var waitedMs = 0
            private val size = Point()

            override fun run() {
                @Suppress("DEPRECATION")
                windowManager.defaultDisplay.getRealSize(size)
                val ready = DebugHooks.knowsDisplaySize(size.x, size.y) || waitedMs >= DISPLAY_WAIT_MS
                if (!ready) {
                    waitedMs += POLL_MS
                    scene.postDelayed(this, POLL_MS.toLong())
                    return
                }
                startTest()
            }
        })
        @Suppress("DEPRECATION")
        scene.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
    }

    /** Switches the assist and the test hooks on, once the scene is on a screen the assist knows. */
    private fun startTest() {
        Log.i(TAG, "Test $myGeneration starts: synthetic=${config.synthetic} assist=${config.assist} trace=${config.trace}")
        DebugHooks.useSyntheticDetections(config.synthetic)
        DebugHooks.enableAssist(config.assist)
        if (config.assist || config.trace) {
            DebugHooks.startTrace()
        }
        if (config.fingerTest) {
            DebugHooks.runFingerScript(File(getExternalFilesDir(null), "fingerscript.csv").path)
        }
    }

    override fun onStop() {
        super.onStop()
        scene.flushLog()
        if (myGeneration != generation) {
            Log.i(TAG, "Test $myGeneration stops after a newer scene opened; leaving its test alone")
            return
        }
        Log.i(TAG, "Test $myGeneration stops")
        if (config.assist || config.trace) {
            DebugHooks.writeTrace(
                File(getExternalFilesDir(null), "aimtrace.csv").path,
                File(getExternalFilesDir(null), "motiontrace.csv").path,
            )
        }
        // Leave the assist as the scene found it: off, and reading the real model.
        DebugHooks.enableAssist(false)
        DebugHooks.useSyntheticDetections(false)
    }
}
