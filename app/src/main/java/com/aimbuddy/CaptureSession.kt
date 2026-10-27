package com.aimbuddy

import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.util.Log
import kotlin.math.roundToInt

/**
 * Screen capture: a mirror of the display that renders into an image reader, whose frames are
 * handed to the native detector as they arrive.
 *
 * It owns the MediaProjection it is given and ends it in [stop]. If the system or the player ends
 * the projection (the "stop sharing" chip, a lock screen), `onEnded` is called so the rest of the
 * app can shut down with it.
 */
class CaptureSession(
    private val projection: MediaProjection,
    private val densityDpi: Int,
    private val onEnded: () -> Unit,
) {
    private val thread = HandlerThread("aimbuddy-capture").also { it.start() }
    private val handler = Handler(thread.looper)
    private var reader: ImageReader? = null
    private var display: VirtualDisplay? = null
    private var stopped = false

    /** Size of the images produced now. */
    var imageSize: Pair<Int, Int> = 0 to 0
        private set

    private val callback = object : MediaProjection.Callback() {
        override fun onStop() {
            Log.w(TAG, "The screen capture was ended by the system or the player")
            if (!stopped) {
                onEnded()
            }
        }
    }

    init {
        // Android 14 and later require the callback to be registered before the mirror is created.
        projection.registerCallback(callback, Handler(Looper.getMainLooper()))
    }

    /**
     * Starts mirroring a display of the given size. Returns the size of the images produced: the
     * display's shape, shrunk so its long edge is at most [MAX_LONG_EDGE].
     */
    fun start(displayWidth: Int, displayHeight: Int): Pair<Int, Int> {
        val (width, height) = captureSize(displayWidth, displayHeight)
        val newReader = createReader(width, height)
        reader = newReader
        display = projection.createVirtualDisplay(
            "AimBuddyCapture", width, height, densityDpi,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            newReader.surface, null, null
        )
        imageSize = width to height
        Log.i(TAG, "Capturing at ${width}x$height")
        return imageSize
    }

    /** Changes the size of the mirror after the display changed shape. Returns the new image size. */
    fun resize(displayWidth: Int, displayHeight: Int): Pair<Int, Int> {
        val (width, height) = captureSize(displayWidth, displayHeight)
        val mirror = display ?: return imageSize
        val newReader = createReader(width, height)
        val oldReader = reader
        mirror.resize(width, height, densityDpi)
        mirror.surface = newReader.surface
        reader = newReader
        oldReader?.setOnImageAvailableListener(null, null)
        oldReader?.close()
        imageSize = width to height
        Log.i(TAG, "Capture resized to ${width}x$height")
        return imageSize
    }

    /** Ends the capture and releases everything. Safe to call more than once. */
    fun stop() {
        if (stopped) {
            return
        }
        stopped = true
        reader?.setOnImageAvailableListener(null, null)
        runCatching { display?.release() }
        display = null
        runCatching { reader?.close() }
        reader = null
        runCatching { projection.unregisterCallback(callback) }
        runCatching { projection.stop() }
        thread.quitSafely()
        Log.i(TAG, "Capture stopped")
    }

    private fun createReader(width: Int, height: Int): ImageReader =
        ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, MAX_IMAGES).apply {
            setOnImageAvailableListener({ source ->
                val image = source.acquireLatestImage() ?: return@setOnImageAvailableListener
                try {
                    // Native code copies the detector's region before returning, so the buffer
                    // can be handed straight back to the compositor afterwards.
                    val buffer = image.hardwareBuffer
                    if (buffer != null) {
                        try {
                            NativeBridge.onFrame(buffer, image.timestamp)
                        } finally {
                            buffer.close()
                        }
                    }
                } finally {
                    image.close()
                }
            }, handler)
        }

    /** Image size with the display's shape and a long edge of at most [MAX_LONG_EDGE]; both sides even. */
    private fun captureSize(displayWidth: Int, displayHeight: Int): Pair<Int, Int> {
        val scale = minOf(1f, MAX_LONG_EDGE.toFloat() / maxOf(displayWidth, displayHeight))
        val width = (displayWidth * scale).roundToInt() and 1.inv()
        val height = (displayHeight * scale).roundToInt() and 1.inv()
        return width to height
    }

    private companion object {
        const val TAG = "CaptureSession"

        // The detector looks at a region of at most 448 px, so a wider capture only costs the
        // phone GPU time without helping detection.
        const val MAX_LONG_EDGE = 1600

        // Two images are enough: each one is copied and closed inside the callback.
        const val MAX_IMAGES = 2
    }
}
