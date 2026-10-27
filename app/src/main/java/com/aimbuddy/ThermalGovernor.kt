package com.aimbuddy

import android.content.Context
import android.os.PowerManager
import android.util.Log

/**
 * The most detections a second to run at a given thermal status of the phone, as the system reports
 * it: all of them while the phone is cool, fewer as it heats up. Zero means no limit.
 */
internal fun detectionLimitFor(thermalStatus: Int): Float = when {
    thermalStatus >= PowerManager.THERMAL_STATUS_SEVERE -> SEVERE_LIMIT_PER_SECOND
    thermalStatus >= PowerManager.THERMAL_STATUS_MODERATE -> MODERATE_LIMIT_PER_SECOND
    else -> 0f
}

private const val MODERATE_LIMIT_PER_SECOND = 30f
private const val SEVERE_LIMIT_PER_SECOND = 15f
private const val TAG = "ThermalGovernor"

/**
 * Eases off detection when the phone says it is getting hot. The detector is the biggest thing
 * AimBuddy asks of the phone, so asking it for fewer detections a second is how AimBuddy helps the
 * phone cool down. While the phone is cool nothing changes.
 *
 * Use it from the main thread only.
 */
class ThermalGovernor(private val context: Context) {

    private val powerManager = context.getSystemService(PowerManager::class.java)
    private val listener = PowerManager.OnThermalStatusChangedListener { status -> apply(status) }
    private var watching = false

    /** Starts watching the phone's temperature, and applies what it says now. */
    fun start() {
        if (watching) {
            return
        }
        watching = true
        apply(powerManager.currentThermalStatus)
        powerManager.addThermalStatusListener(context.mainExecutor, listener)
    }

    /** Stops watching and lifts the limit. */
    fun stop() {
        if (!watching) {
            return
        }
        watching = false
        powerManager.removeThermalStatusListener(listener)
        NativeBridge.setDetectionRateLimit(0f)
    }

    private fun apply(status: Int) {
        val limit = detectionLimitFor(status)
        Log.i(TAG, "Thermal status $status: " + if (limit > 0f) "at most $limit detections a second" else "no limit")
        NativeBridge.setDetectionRateLimit(limit)
    }
}
