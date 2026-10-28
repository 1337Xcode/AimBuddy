package com.aimbuddy

import android.content.Context
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
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
 *  - A secure window shows as solid black in a recording, and AimBuddy's own picture of the screen is
 *    a recording too. So no window that covers the game is ever secure. In streamer mode the full screen
 *    window draws nothing but the menu and the icon is made invisible, but it stays where it is and works,
 *    so the player is never locked out of the menu or of stopping. While the menu is open a secure window
 *    covers its column of the display (see [SecureShield]): a recording sees black there, and the game
 *    everywhere else.
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
    private var icon: StaticBubbleView? = null
    private var iconParams: WindowManager.LayoutParams? = null
    private val shield = SecureShield(context)

    private var streamerMode = false
    private var touchable = false

    // In streamer mode the menu waits for the secure window that covers it; a second tap on the button while it waits cancels it.
    private var opening = false
    private var openTicket = 0
    private var lastFrameCount = -1L
    private var lastFrameChangeMs = 0L

    // Where the icon sits, kept so it comes back to the same place after streamer mode.
    private var iconX = ICON_START_X
    private var iconY = ICON_START_Y

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

    // Takes the secure window away once the menu has had time to be cleared from the screen (see releaseTouches).
    private val removeShield = Runnable {
        if (!OverlayNative.wantsTouches()) {
            shield.hide()
        }
    }

    private val monitor = object : Runnable {
        override fun run() {
            syncTouchability()
            handler.postDelayed(this, MONITOR_INTERVAL_MS)
        }
    }

    val isShown: Boolean get() = surface != null

    /** Adds both windows. Needs the overlay permission. */
    fun show(streamerMode: Boolean) {
        if (isShown) {
            return
        }
        this.streamerMode = streamerMode
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
        handler.removeCallbacks(removeShield)
        opening = false
        shield.hide()
        removeIcon()
        surface?.let {
            runCatching { OverlayNative.showMenu(false) }
            removeQuietly(it, "overlay")
        }
        surface = null
        surfaceParams = null
        touchable = false
        Log.i(TAG, "Overlay hidden")
    }

    /**
     * Turns streamer mode on or off: nothing of AimBuddy may show in a screenshot or a recording.
     *
     * While it is on, the icon is invisible (it still opens the menu and still stops AimBuddy when held) and the full
     * screen window draws nothing but the menu (see the native overlay). The menu is covered by a secure window of
     * its own (see [SecureShield]), which exists only while the menu is open, so a recording is never black all over.
     */
    fun setStreamerMode(enabled: Boolean) {
        streamerMode = enabled
        icon?.alpha = if (enabled) 0f else 1f
        handler.removeCallbacks(removeShield)
        if (!enabled) {
            shield.hide()
        } else if (touchable && !shield.cover(shieldBounds())) {   // the menu is already open: cover it now
            closeMenu()
        }
    }

    /** Opens the menu, or closes it if it is open. */
    fun toggleMenu() {
        if (!isShown) {
            return
        }
        if (OverlayNative.wantsTouches() || opening) {
            closeMenu()
        } else {
            openMenu()
        }
    }

    private fun openMenu() {
        lastFrameChangeMs = SystemClock.uptimeMillis()
        handler.removeCallbacks(removeShield)
        if (!streamerMode) {
            showMenuNow()
            return
        }
        // In streamer mode the menu appears only once the secure window that covers it is on the screen, never before...
        opening = true
        val ticket = ++openTicket
        val covering = shield.show(shieldBounds()) {
            if (opening && ticket == openTicket) {
                opening = false
                showMenuNow()
            }
        }
        if (!covering) {
            opening = false
            Log.w(TAG, "The menu stays shut: it could not be hidden from recordings")
            Toast.makeText(context, R.string.streamer_menu_unavailable, Toast.LENGTH_LONG).show()
        }
    }

    private fun showMenuNow() {
        setTouchable(true)
        OverlayNative.showMenu(true)
    }

    /** ...and the menu stops being drawn before that window goes (see releaseTouches). */
    private fun closeMenu() {
        opening = false
        lastFrameChangeMs = SystemClock.uptimeMillis()
        OverlayNative.showMenu(false)
        releaseTouches()
    }

    /**
     * Gives the touches back to the game. In streamer mode the secure window stays for a few frames more: a recording
     * must not catch a last frame of the menu, so the renderer gets time to draw the cleared picture and the system to show it.
     */
    private fun releaseTouches() {
        if (touchable) {
            setTouchable(false)
        }
        if (shield.isShown && !opening) {
            scheduleShieldRemoval()
        }
    }

    private fun scheduleShieldRemoval() {
        if (!handler.hasCallbacks(removeShield)) {
            handler.postDelayed(removeShield, SHIELD_RELEASE_DELAY_MS)
        }
    }

    /** Where the secure window goes: the menu's column of the display, found from where the menu is (or would open) now. */
    private fun shieldBounds(): Bounds {
        val menu = OverlayNative.menuBounds()
        val windowOrigin = IntArray(2)
        surface?.getLocationOnScreen(windowOrigin)
        val display = windowManager.maximumWindowMetrics.bounds
        val margin = (SHIELD_MARGIN_DP * context.resources.displayMetrics.density).toInt()
        return shieldBoundsFor(Bounds(menu[0], menu[1], menu[2], menu[3]), windowOrigin[0], display.width(), display.height(), margin)
    }

    /**
     * Brings the button back inside the display after the screen has turned or changed size. In streamer mode an open
     * menu is closed: the secure window that covers it would be a moment behind the menu's new place.
     */
    fun keepIconOnScreen() {
        if (streamerMode && (touchable || opening)) {
            closeMenu()
        }
        val params = iconParams ?: return
        val bounds = windowManager.maximumWindowMetrics.bounds
        val size = params.width
        iconX = iconX.coerceIn(0, maxOf(0, bounds.width() - size))
        iconY = iconY.coerceIn(0, maxOf(0, bounds.height() - size))
        if (iconX != params.x || iconY != params.y) {
            params.x = iconX
            params.y = iconY
            update(icon, params)
            OverlayNative.setIconPosition(params.x.toFloat(), params.y.toFloat())
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
            releaseTouches()
            return
        }
        if (wanted) {
            setTouchable(true)
            if (streamerMode) {
                handler.removeCallbacks(removeShield)
                if (!shield.cover(shieldBounds())) {   // follows the menu, and covers it however it came to be open
                    closeMenu()
                    return
                }
            }
        } else {
            releaseTouches()
        }
    }

    private fun addIcon() {
        val sizePx = (ICON_SIZE_DP * context.resources.displayMetrics.density).toInt()
        val view = StaticBubbleView(context).apply {
            contentDescription = context.getString(R.string.overlay_button_description)
            // Invisible, not gone, in streamer mode: it still takes touches.
            alpha = if (streamerMode) 0f else 1f
            // A tap on a stream must not click out loud.
            isSoundEffectsEnabled = false
        }
        val params = WindowManager.LayoutParams(
            sizePx, sizePx, overlayType(),
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
                WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = iconX
            y = iconY
        }
        view.setOnTouchListener { touched, event -> onIconTouch(touched, event, params) }
        // Touch exploration, switch access and voice control press the button without touches: they come through these two.
        view.setOnClickListener { toggleMenu() }
        view.setOnLongClickListener {
            onStopRequested()
            true
        }
        windowManager.addView(view, params)
        icon = view
        iconParams = params
        OverlayNative.setIconPosition(params.x.toFloat(), params.y.toFloat())
    }

    /** Takes the icon off the screen, keeping where it was. */
    private fun removeIcon() {
        handler.removeCallbacks(holdToStop)
        icon?.let { removeQuietly(it, "icon") }
        icon = null
        iconParams = null
    }

    private fun onIconTouch(view: View, event: MotionEvent, params: WindowManager.LayoutParams): Boolean {
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
                    iconX = params.x
                    iconY = params.y
                    update(icon, params)
                    OverlayNative.setIconPosition(params.x.toFloat(), params.y.toFloat())
                }
            }
            MotionEvent.ACTION_UP -> {
                handler.removeCallbacks(holdToStop)
                if (!moved && !held) {
                    view.performClick()
                }
            }
            MotionEvent.ACTION_CANCEL -> handler.removeCallbacks(holdToStop)
        }
        return true
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

        // The system setting behind the opacity limit for touches that pass through an overlay.
        const val OBSCURING_OPACITY_SETTING = "maximum_obscuring_opacity_for_touch"
        const val DEFAULT_OBSCURING_OPACITY = 0.8f
        const val OPACITY_MARGIN = 0.01f
        const val MIN_PASS_THROUGH_ALPHA = 0.05f
        const val ICON_SIZE_DP = 44
        const val ICON_START_X = 40
        const val ICON_START_Y = 120
        const val MONITOR_INTERVAL_MS = 100L
        const val STALL_MS = 1500L
        const val SHIELD_RELEASE_DELAY_MS = 60L   // a few frames
        const val SHIELD_MARGIN_DP = 16
        const val HOLD_TO_STOP_MS = 1500L
        const val MOVE_SLOP_PX = 10
    }
}
