package com.aimbuddy

import android.content.Context
import android.graphics.PixelFormat
import android.util.Log
import android.view.Choreographer
import android.view.Gravity
import android.view.View
import android.view.WindowManager

/** A rectangle in display pixels. */
data class Bounds(val left: Int, val top: Int, val right: Int, val bottom: Int) {
    val width: Int get() = right - left
    val height: Int get() = bottom - top
    val isEmpty: Boolean get() = right <= left || bottom <= top
}

/**
 * Where the secure window goes so that the menu never reaches a recording: the menu's column of the display, from
 * the top edge to the bottom edge.
 *
 * It is the column and not the menu's own rectangle so that a list that opens past the menu's edge (the language list,
 * the colour picker) is covered from its first frame, and the window never has to follow the menu around. The rest of
 * the display, where the game is, stays in the recording.
 *
 * `menu` is in the overlay window's pixels and `windowLeft` is where that window starts on the display. When nothing
 * is known about the menu yet, the whole display is covered, which hides too much but never too little.
 */
internal fun shieldBoundsFor(menu: Bounds, windowLeft: Int, displayWidth: Int, displayHeight: Int, marginPx: Int): Bounds {
    if (menu.isEmpty) {
        return Bounds(0, 0, displayWidth, displayHeight)
    }
    val left = (windowLeft + menu.left - marginPx).coerceIn(0, displayWidth)
    val right = (windowLeft + menu.right + marginPx).coerceIn(0, displayWidth)
    if (right <= left) {
        return Bounds(0, 0, displayWidth, displayHeight)
    }
    return Bounds(left, 0, right, displayHeight)
}

/**
 * A see-through window marked secure, put over the menu while streamer mode is on.
 *
 * The system draws a secure window as solid black in every screenshot and recording, however transparent its pixels
 * are, and that includes AimBuddy's own picture of the screen. Putting the mark on the full screen overlay would
 * black out the whole recording whenever the menu is open, so the mark goes on this window instead, which is only as
 * big as the menu's column. The player sees through it to the menu; a recording sees black where the menu is and the
 * game everywhere else.
 *
 * The window takes no touches and no focus. Use it from the main thread only.
 */
class SecureShield(private val context: Context) {
    private val windowManager = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
    private var view: View? = null
    private var params: WindowManager.LayoutParams? = null

    val isShown: Boolean get() = view != null

    /**
     * Puts the window over `bounds`, and calls `onCovered` once the system has had a few frames to show it, so that
     * what goes behind it is never on the screen before it is. Returns false, and never calls `onCovered`, when the
     * system would not add the window.
     */
    fun show(bounds: Bounds, onCovered: () -> Unit): Boolean {
        if (!cover(bounds)) {
            return false
        }
        afterFrames(FRAMES_TO_SHOW, onCovered)
        return true
    }

    /** Puts the window over `bounds` if it is not there yet, or moves it there. Returns false when the system would not add it. */
    fun cover(bounds: Bounds): Boolean {
        if (view == null) {
            return add(bounds)
        }
        move(bounds)
        return true
    }

    /** Moves the window to `bounds`, if it is somewhere else. */
    fun move(bounds: Bounds) {
        val current = params ?: return
        if (current.x == bounds.left && current.y == bounds.top && current.width == bounds.width && current.height == bounds.height) {
            return
        }
        current.x = bounds.left
        current.y = bounds.top
        current.width = bounds.width
        current.height = bounds.height
        try {
            windowManager.updateViewLayout(view, current)
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "The shield window is already gone: ${e.message}")
        }
    }

    /** Takes the window away. Safe to call at any time, any number of times. */
    fun hide() {
        val shield = view ?: return
        view = null
        params = null
        try {
            windowManager.removeViewImmediate(shield)
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "The shield window was already removed")
        } catch (e: RuntimeException) {
            Log.e(TAG, "Could not remove the shield window: ${e.message}", e)
        }
    }

    private fun add(bounds: Bounds): Boolean {
        // A pixel that is almost, not quite, clear: a window with nothing in it at all may be left out of the picture the
        // system takes, and then it would hide nothing.
        val shield = View(context).apply { setBackgroundColor(ALMOST_CLEAR) }
        val layout = WindowManager.LayoutParams(
            bounds.width,
            bounds.height,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
                WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS or
                WindowManager.LayoutParams.FLAG_SECURE,
            PixelFormat.TRANSLUCENT,
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = bounds.left
            y = bounds.top
            layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
        }
        try {
            windowManager.addView(shield, layout)
        } catch (e: RuntimeException) {
            // A phone that refuses a secure overlay window cannot hide the menu, so the caller keeps the menu shut.
            Log.e(TAG, "The system would not add the secure window: ${e.message}", e)
            return false
        }
        view = shield
        params = layout
        return true
    }

    /** Runs `action` on the main thread after the display has drawn this many more frames. */
    private fun afterFrames(count: Int, action: () -> Unit) {
        if (count <= 0) {
            action()
            return
        }
        Choreographer.getInstance().postFrameCallback { afterFrames(count - 1, action) }
    }

    private companion object {
        const val TAG = "SecureShield"
        const val ALMOST_CLEAR = 0x01000000   // black at one part in 256

        /** A new window is laid out, drawn and composed over a few frames; this many is enough on every refresh rate. */
        const val FRAMES_TO_SHOW = 4
    }
}
