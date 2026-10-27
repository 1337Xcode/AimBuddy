package com.aimbuddy.playground

import android.app.Activity
import android.content.pm.ActivityInfo
import android.os.Bundle
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

    private lateinit var scene: PlaygroundView
    private lateinit var config: PlaygroundConfig

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        config = PlaygroundConfig.from(intent)
        val logFile = File(getExternalFilesDir(null), "playground.csv")
        logFile.delete()   // each run starts a fresh record
        scene = PlaygroundView(this, config, logFile)
        setContentView(scene)
        DebugHooks.useSyntheticDetections(config.synthetic)
        DebugHooks.enableAssist(config.assist)
        if (config.assist || config.trace) {
            DebugHooks.startTrace()
        }
        if (config.fingerTest) {
            // Give the display time to turn to landscape and the assist to learn the new size before the finger moves.
            scene.postDelayed({ DebugHooks.runFingerScript(File(getExternalFilesDir(null), "fingerscript.csv").path) }, 4000)
        }
        @Suppress("DEPRECATION")
        scene.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
    }

    override fun onStop() {
        super.onStop()
        scene.flushLog()
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
