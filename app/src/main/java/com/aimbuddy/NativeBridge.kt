package com.aimbuddy

import android.hardware.HardwareBuffer

/**
 * The native side of the app: the detection pipeline, the aim loop and the connection to the
 * touch helper. Everything here is a thin call into C++ (see esp_jni.cpp); the decisions are made
 * there, and the Android parts (service, overlay, helper launcher) only start and stop them.
 */
object NativeBridge {

    init {
        System.loadLibrary("esp_native")
    }

    /** Called when the player changes streamer mode in the overlay menu. */
    interface StreamerModeListener {
        fun onStreamerModeChanged(enabled: Boolean)
    }

    /** Called when the player asks, from the overlay menu, to stop the assist. */
    interface StopListener {
        fun onStopRequested()
    }

    @Volatile
    private var streamerListener: StreamerModeListener? = null

    @Volatile
    private var stopListener: StopListener? = null

    fun setStreamerModeListener(listener: StreamerModeListener?) {
        streamerListener = listener
    }

    fun setStopListener(listener: StopListener?) {
        stopListener = listener
    }

    /** Called from native code on the overlay's render thread. */
    @JvmStatic
    fun onStreamerModeChanged(enabled: Boolean) {
        streamerListener?.onStreamerModeChanged(enabled)
    }

    /** Called from native code on the overlay's render thread. */
    @JvmStatic
    fun onStopRequested() {
        stopListener?.onStopRequested()
    }

    /** Loads the player's settings from `settingsPath` (a file in the app's private storage). */
    @JvmStatic
    external fun loadSettings(settingsPath: String)

    /** All settings as `name=value` lines (the names are the ones in the settings file). */
    @JvmStatic
    external fun settingsSnapshot(): String

    /** Changes one setting by name; unknown names and unreadable numbers change nothing. */
    @JvmStatic
    external fun setSetting(name: String, value: String)

    /** Puts every setting back to how a fresh install starts, keeping the language. */
    @JvmStatic
    external fun resetSettings()

    /** Writes the settings to their file now, if anything changed. */
    @JvmStatic
    external fun saveSettings()

    /** The most detections a second to run, for when the phone is getting hot. Zero or less lifts the limit. */
    @JvmStatic
    external fun setDetectionRateLimit(perSecond: Float)

    /**
     * Loads the detection model from its two files. `roles` is a comma separated list such as
     * "body,head" saying what each class of the model means.
     */
    @JvmStatic
    external fun loadModel(
        paramPath: String,
        binPath: String,
        roles: String?,
        endToEnd: Boolean,
        inputSize: Int,
    ): Boolean

    /** True when the player has chosen to find targets by a colour, which needs no model. */
    @JvmStatic
    external fun usesColourDetection(): Boolean

    /** Tells native code the display's size and rotation and the size of the capture image. */
    @JvmStatic
    external fun configureDisplay(
        displayWidth: Int,
        displayHeight: Int,
        captureWidth: Int,
        captureHeight: Int,
        rotation: Int,
    )

    @JvmStatic
    external fun start(): Boolean

    /** Stops the aim loop first (the assist's finger comes off, the touchscreen goes back), then detection. */
    @JvmStatic
    external fun stop()

    @JvmStatic
    external fun isRunning(): Boolean

    /** Stops everything, closes the touch connection and saves the settings. */
    @JvmStatic
    external fun shutdown()

    /** Hands native code one captured frame; the buffer is only valid during the call. */
    @JvmStatic
    external fun onFrame(hardwareBuffer: HardwareBuffer, timestamp: Long)

    /** Connects to the touch helper. Returns 0 on success, otherwise a reason code (see TouchHelper.Reason). */
    @JvmStatic
    external fun touchConnect(port: Int, tokenHex: String): Int

    @JvmStatic
    external fun touchDisconnect()

    /** The player's choice of route to the touchscreen: 0 automatic, 1 Shizuku, 2 root. */
    @JvmStatic
    external fun touchBackend(): Int

    /**
     * What the touch helper last reported: bit 24 is set while connected, bits 8 to 23 hold the
     * reason (0 when nothing went wrong) and the low byte the state.
     */
    @JvmStatic
    external fun touchStatus(): Int
}
