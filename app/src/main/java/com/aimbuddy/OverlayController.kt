package com.aimbuddy

import android.content.Context
import android.graphics.PixelFormat
import android.graphics.drawable.Drawable
import android.graphics.drawable.PictureDrawable
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.ImageView
import com.caverock.androidsvg.SVG
import kotlin.math.abs

/**
 * The two windows AimBuddy puts over the game: a full screen one that draws the targets and the
 * menu, and a small floating icon that opens the menu.
 *
 * The rules that keep the overlay from ever getting in the player's way:
 *  - The full screen window lets every touch through to the game. It only takes touches while the
 *    menu is open, and a tap outside the menu closes it again (see the native menu code).
 *  - While touches pass through, the window is kept below the opacity limit of Android 12 and later
 *    (see [passThroughAlpha]). Above it the system silently drops every touch meant for the app
 *    underneath, and the phone looks frozen.
 *  - If the menu is open but nothing is being drawn, the window stops taking touches after a short
 *    while, so a broken renderer cannot leave an invisible layer over the screen.
 *  - Holding the icon for a moment stops AimBuddy, whatever state the menu is in.
 *  - Every window is removed in [hide], which cannot fail halfway: each step is guarded.
 *
 * Use it from the main thread only.
 */
class OverlayController(
    private val context: Context,
    private val onStopRequested: () -> Unit,
) {
    private val windowManager = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
    private val handler = Handler(Looper.getMainLooper())

    private var surface: OverlayView? = null
    private var surfaceParams: WindowManager.LayoutParams? = null
    private var icon: ImageView? = null
    private var iconParams: WindowManager.LayoutParams? = null

    private var secure = false
    private var touchable = false
    private var lastFrameCount = -1L
    private var lastFrameChangeMs = 0L

    // Dragging and holding the icon.
    private var downRawX = 0f
    private var downRawY = 0f
    private var startX = 0
    private var startY = 0
    private var moved = false
    private var held = false
    private val holdToStop = Runnable {
        held = true
        Log.i(TAG, "Icon held: stopping")
        onStopRequested()
    }

    private val monitor = object : Runnable {
        override fun run() {
            syncTouchability()
            handler.postDelayed(this, MONITOR_INTERVAL_MS)
        }
    }

    val isShown: Boolean get() = surface != null

    /** Adds both windows. Needs the overlay permission. */
    fun show(secure: Boolean) {
        if (isShown) {
            return
        }
        this.secure = secure
        touchable = false
        lastFrameCount = -1L
        lastFrameChangeMs = SystemClock.uptimeMillis()

        val view = OverlayView(context)
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT,
            overlayType(),
            surfaceFlags(touchable = false),
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            alpha = passThroughAlpha()
            // Draw under a notch or pill on every edge too, so the overlay lines up with the display.
            layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
        }
        windowManager.addView(view, params)
        surface = view
        surfaceParams = params

        try {
            addIcon()
        } catch (e: Exception) {
            hide()
            throw e
        }
        handler.postDelayed(monitor, MONITOR_INTERVAL_MS)
        Log.i(TAG, "Overlay shown")
    }

    /** Removes both windows. Safe to call at any time, any number of times. */
    fun hide() {
        handler.removeCallbacks(monitor)
        handler.removeCallbacks(holdToStop)
        icon?.let { removeQuietly(it, "icon") }
        icon = null
        iconParams = null
        surface?.let {
            runCatching { OverlayNative.showMenu(false) }
            removeQuietly(it, "overlay")
        }
        surface = null
        surfaceParams = null
        touchable = false
        Log.i(TAG, "Overlay hidden")
    }

    /** Hides the overlay from screenshots and screen recordings, or shows it to them again. */
    fun setSecure(enabled: Boolean) {
        secure = enabled
        surfaceParams?.let { params ->
            params.flags = if (enabled) params.flags or FLAG_SECURE else params.flags and FLAG_SECURE.inv()
            update(surface, params)
        }
        iconParams?.let { params ->
            params.flags = if (enabled) params.flags or FLAG_SECURE else params.flags and FLAG_SECURE.inv()
            update(icon, params)
        }
    }

    private fun overlayType(): Int = WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY

    private fun surfaceFlags(touchable: Boolean): Int {
        var flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
            WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
            WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS or
            WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED
        if (!touchable) {
            flags = flags or WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
        }
        if (secure) {
            flags = flags or FLAG_SECURE
        }
        return flags
    }

    /**
     * How opaque the full screen window may be while touches pass through it to the game.
     *
     * From Android 12 the system drops touches that go through another app's overlay when the
     * overlay is more opaque than a limit (0.8 unless the phone's maker changed it). The limit is
     * about the window's alpha, not its pixels, so even an empty window is blocked at alpha 1. The
     * window therefore stays just below the limit while it is see-through, and only takes full
     * opacity while it is the one receiving the touches (the menu).
     */
    private fun passThroughAlpha(): Float {
        val limit = Settings.Global.getFloat(
            context.contentResolver, OBSCURING_OPACITY_SETTING, DEFAULT_OBSCURING_OPACITY
        )
        return (limit - OPACITY_MARGIN).coerceIn(MIN_PASS_THROUGH_ALPHA, DEFAULT_OBSCURING_OPACITY - OPACITY_MARGIN)
    }

    /** Makes the full screen window take touches (menu open) or pass them all through. */
    private fun setTouchable(wanted: Boolean) {
        if (touchable == wanted) {
            return
        }
        val params = surfaceParams ?: return
        touchable = wanted
        params.flags = surfaceFlags(wanted)
        params.alpha = if (wanted) 1f else passThroughAlpha()
        update(surface, params)
    }

    /** Brings the window's touchability in line with what the menu needs, and stops it if the renderer has hung. */
    private fun syncTouchability() {
        val now = SystemClock.uptimeMillis()
        val frames = OverlayNative.framesDrawn()
        if (frames != lastFrameCount) {
            lastFrameCount = frames
            lastFrameChangeMs = now
        }
        val wanted = OverlayNative.wantsTouches()
        if (wanted && now - lastFrameChangeMs > STALL_MS) {
            Log.w(TAG, "The menu is open but nothing is being drawn: giving the screen back")
            OverlayNative.showMenu(false)
            setTouchable(false)
            return
        }
        setTouchable(wanted)
    }

    private fun toggleMenu() {
        val show = !OverlayNative.wantsTouches()
        lastFrameChangeMs = SystemClock.uptimeMillis()
        OverlayNative.showMenu(show)
        setTouchable(show)
    }

    private fun addIcon() {
        val sizePx = (ICON_SIZE_DP * context.resources.displayMetrics.density).toInt()
        val view = ImageView(context).apply {
            setImageDrawable(loadIcon())
            setLayerType(View.LAYER_TYPE_SOFTWARE, null)
            scaleType = ImageView.ScaleType.CENTER_INSIDE
            contentDescription = "AimBuddy menu. Hold to stop."
        }
        val params = WindowManager.LayoutParams(
            sizePx, sizePx, overlayType(),
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
                WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS or
                (if (secure) FLAG_SECURE else 0),
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 40
            y = 120
        }
        view.setOnTouchListener { _, event -> onIconTouch(event, params) }
        windowManager.addView(view, params)
        icon = view
        iconParams = params
        OverlayNative.setIconPosition(params.x.toFloat(), params.y.toFloat())
    }

    private fun onIconTouch(event: MotionEvent, params: WindowManager.LayoutParams): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downRawX = event.rawX
                downRawY = event.rawY
                startX = params.x
                startY = params.y
                moved = false
                held = false
                handler.postDelayed(holdToStop, HOLD_TO_STOP_MS)
            }
            MotionEvent.ACTION_MOVE -> {
                val dx = (event.rawX - downRawX).toInt()
                val dy = (event.rawY - downRawY).toInt()
                if (abs(dx) > MOVE_SLOP_PX || abs(dy) > MOVE_SLOP_PX) {
                    moved = true
                    handler.removeCallbacks(holdToStop)
                }
                if (moved) {
                    params.x = startX + dx
                    params.y = startY + dy
                    update(icon, params)
                    OverlayNative.setIconPosition(params.x.toFloat(), params.y.toFloat())
                }
            }
            MotionEvent.ACTION_UP -> {
                handler.removeCallbacks(holdToStop)
                if (!moved && !held) {
                    toggleMenu()
                }
            }
            MotionEvent.ACTION_CANCEL -> handler.removeCallbacks(holdToStop)
        }
        return true
    }

    private fun loadIcon(): Drawable? = try {
        val svg = context.assets.open("icons/settings.svg").use { SVG.getFromInputStream(it) }
        PictureDrawable(svg.renderToPicture())
    } catch (e: Exception) {
        Log.w(TAG, "Failed to load the icon: ${e.message}")
        context.getDrawable(android.R.drawable.ic_menu_manage)
    }

    private fun update(view: View?, params: WindowManager.LayoutParams) {
        if (view == null) {
            return
        }
        try {
            windowManager.updateViewLayout(view, params)
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "Window already gone: ${e.message}")
        }
    }

    private fun removeQuietly(view: View, what: String) {
        try {
            windowManager.removeViewImmediate(view)
        } catch (e: IllegalArgumentException) {
            Log.w(TAG, "The $what window was already removed")
        } catch (e: Exception) {
            Log.e(TAG, "Could not remove the $what window: ${e.message}", e)
        }
    }

    private companion object {
        const val TAG = "OverlayController"
        const val FLAG_SECURE = WindowManager.LayoutParams.FLAG_SECURE

        // The system setting behind the opacity limit for touches that pass through an overlay.
        const val OBSCURING_OPACITY_SETTING = "maximum_obscuring_opacity_for_touch"
        const val DEFAULT_OBSCURING_OPACITY = 0.8f
        const val OPACITY_MARGIN = 0.01f
        const val MIN_PASS_THROUGH_ALPHA = 0.05f
        const val ICON_SIZE_DP = 44
        const val MONITOR_INTERVAL_MS = 100L
        const val STALL_MS = 1500L
        const val HOLD_TO_STOP_MS = 1500L
        const val MOVE_SLOP_PX = 10
    }
}
