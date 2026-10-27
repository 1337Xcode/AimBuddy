package com.aimbuddy

import android.content.res.AssetManager

/**
 * The native half of the overlay: draws the targets and the settings menu with ImGui (see
 * imgui_menu.cpp). The window and the OpenGL context belong to [OverlayView].
 *
 * Calls marked "GL thread" must come from the overlay's render thread, with its OpenGL context
 * current. The others may be called from any thread.
 */
object OverlayNative {

    init {
        System.loadLibrary("esp_native")
    }

    /**
     * Starts drawing into the current OpenGL context, for a surface of this size. `assets` holds the
     * fonts and `density` is the screen's pixels per dp, which the menu's sizes follow. GL thread.
     */
    @JvmStatic
    external fun attach(assets: AssetManager, width: Int, height: Int, density: Float)

    /** The surface changed size. GL thread. */
    @JvmStatic
    external fun resize(width: Int, height: Int)

    /** Draws one frame. GL thread. */
    @JvmStatic
    external fun drawFrame()

    /** Stops drawing and frees what was made for the OpenGL context, which must still be current. GL thread. */
    @JvmStatic
    external fun detach()

    /** Hands a touch (0 down, 1 up or cancelled, 2 moved) to the menu. GL thread. */
    @JvmStatic
    external fun touch(action: Int, x: Float, y: Float): Boolean

    /** Whether the overlay window should be touchable: true only while the menu is open. */
    @JvmStatic
    external fun wantsTouches(): Boolean

    @JvmStatic
    external fun showMenu(visible: Boolean)

    /** How many frames have been drawn, to tell a stuck renderer from a working one. */
    @JvmStatic
    external fun framesDrawn(): Long

    /** Reports where the overlay window's top-left corner is on the display, in pixels. */
    @JvmStatic
    external fun setOrigin(x: Int, y: Int)

    /** Reports where the floating icon is, so the menu opens beside it. */
    @JvmStatic
    external fun setIconPosition(x: Float, y: Float)
}
