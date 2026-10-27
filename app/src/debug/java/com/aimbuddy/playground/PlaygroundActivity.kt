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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val config = PlaygroundConfig.from(intent)
        val logFile = File(getExternalFilesDir(null), "playground.csv")
        logFile.delete()   // each run starts a fresh record
        scene = PlaygroundView(this, config, logFile)
        setContentView(scene)
        DebugHooks.useSyntheticDetections(config.synthetic)
        DebugHooks.enableAssist(config.assist)
        @Suppress("DEPRECATION")
        scene.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
    }

    override fun onStop() {
        super.onStop()
        scene.flushLog()
        // Leave the assist as the scene found it: off, and reading the real model.
        DebugHooks.enableAssist(false)
        DebugHooks.useSyntheticDetections(false)
    }
}
