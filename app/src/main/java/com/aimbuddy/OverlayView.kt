package com.aimbuddy

import android.content.Context
import android.content.res.AssetManager
import android.graphics.SurfaceTexture
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.util.Log
import android.view.MotionEvent
import android.view.TextureView
import java.util.concurrent.ConcurrentLinkedQueue

/**
 * The overlay's drawing surface: a view in which native code draws the targets and the settings
 * menu with ImGui, using OpenGL on a thread of its own.
 *
 * It is a TextureView and not a SurfaceView on purpose. From Android 12 the system drops every
 * touch that passes through another app's overlay when the overlay is more opaque than 0.8, and it
 * judges a SurfaceView by a rule that ignores how transparent its pixels are: a SurfaceView is a
 * layer of its own, and any such layer above an app blocks that app's touches. A TextureView is
 * drawn into the window's own layer, so the window's alpha is what counts (see OverlayController),
 * and the touches reach the game.
 *
 * The window that holds it lets touches through, except while the menu is open. Touches that arrive
 * then are handed to ImGui on the render thread, the only thread ImGui may be used from.
 */
class OverlayView(context: Context) : TextureView(context), TextureView.SurfaceTextureListener {

    private var renderer: RenderThread? = null
    private val location = IntArray(2)   // where the view sits on the screen, read again on every layout

    init {
        isOpaque = false
        surfaceTextureListener = this
    }

    override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
        Log.i(TAG, "Surface available: ${width}x$height")
        renderer = RenderThread(surface, width, height, context.assets, context.resources.displayMetrics.density)
            .also { it.start() }
    }

    override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
        Log.i(TAG, "Surface resized: ${width}x$height")
        renderer?.post { OverlayNative.resize(width, height) }
    }

    /**
     * The surface is going away. The render thread is told to stop and ends on its own; returning
     * true lets the view release the surface, which also frees a thread that is waiting for it.
     */
    override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
        Log.i(TAG, "Surface destroyed")
        renderer?.quit()
        renderer = null
        return true
    }

    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) = Unit

    /**
     * The overlay window can start a few pixels in from the display corner (a display cutout) or
     * end before its edge (a navigation bar), and both change at runtime. The detector and the
     * aim logic use display pixels, so the native side needs the window's offset to draw in the
     * right place.
     */
    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)
        getLocationOnScreen(location)
        OverlayNative.setOrigin(location[0], location[1])
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!OverlayNative.wantsTouches()) {
            return false
        }
        val action = when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> 0
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> 1
            MotionEvent.ACTION_MOVE -> 2
            else -> return true
        }
        val x = event.x
        val y = event.y
        renderer?.post { OverlayNative.touch(action, x, y) }
        return true
    }

    private companion object {
        const val TAG = "OverlayView"
    }
}

/**
 * Draws the overlay: makes an OpenGL context for the view's surface, then draws frames until told
 * to quit or until the surface is gone. Everything that touches ImGui runs on this thread.
 */
private class RenderThread(
    private val texture: SurfaceTexture,
    private val width: Int,
    private val height: Int,
    private val assets: AssetManager,
    private val density: Float,
) : Thread("aimbuddy-overlay-gl") {

    private val tasks = ConcurrentLinkedQueue<Runnable>()

    @Volatile
    private var quitting = false

    /** Runs `task` on the render thread before the next frame. */
    fun post(task: Runnable) {
        tasks.add(task)
    }

    fun quit() {
        quitting = true
    }

    override fun run() {
        var egl: EglSession? = null
        try {
            egl = EglSession(texture)
            OverlayNative.attach(assets, width, height, density)
            while (!quitting) {
                while (true) {
                    (tasks.poll() ?: break).run()
                }
                if (OverlayNative.drawFrame() && !egl.swap()) {
                    Log.i(TAG, "The surface is gone; the renderer stops")
                    break
                }
            }
        } catch (e: Throwable) {
            Log.e(TAG, "The overlay's renderer stopped: ${e.message}", e)
        } finally {
            // Native objects belong to the OpenGL context, so they are freed before it is.
            runCatching { OverlayNative.detach() }
            egl?.release()
            Log.i(TAG, "Renderer ended")
        }
    }

    private companion object {
        const val TAG = "OverlayRender"
    }
}

/** An OpenGL ES 3 context that draws into a SurfaceTexture. Made, used and released by one thread. */
private class EglSession(texture: SurfaceTexture) {
    private val display: EGLDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
    private val context: EGLContext
    private val surface: EGLSurface

    init {
        check(display != EGL14.EGL_NO_DISPLAY) { "No EGL display" }
        val version = IntArray(2)
        check(EGL14.eglInitialize(display, version, 0, version, 1)) { "eglInitialize failed" }

        val attributes = intArrayOf(
            EGL14.EGL_RENDERABLE_TYPE, EGLExt.EGL_OPENGL_ES3_BIT_KHR,
            EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT,
            EGL14.EGL_RED_SIZE, 8,
            EGL14.EGL_GREEN_SIZE, 8,
            EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_ALPHA_SIZE, 8,   // the overlay is see-through, so the surface needs alpha
            EGL14.EGL_NONE,
        )
        val configs = arrayOfNulls<EGLConfig>(1)
        val count = IntArray(1)
        check(EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0) && count[0] > 0) {
            "No OpenGL ES 3 configuration with alpha"
        }
        val config = checkNotNull(configs[0])

        val contextAttributes = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE)
        context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT, contextAttributes, 0)
        check(context != EGL14.EGL_NO_CONTEXT) { "eglCreateContext failed" }
        surface = EGL14.eglCreateWindowSurface(display, config, texture, intArrayOf(EGL14.EGL_NONE), 0)
        check(surface != EGL14.EGL_NO_SURFACE) { "eglCreateWindowSurface failed" }
        check(EGL14.eglMakeCurrent(display, surface, surface, context)) { "eglMakeCurrent failed" }
        // The frame rate is set by the drawing code, not by waiting on the display.
        EGL14.eglSwapInterval(display, 0)
    }

    /** Shows the frame just drawn. Returns false when the surface can no longer be drawn to. */
    fun swap(): Boolean = EGL14.eglSwapBuffers(display, surface)

    fun release() {
        EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
        EGL14.eglDestroySurface(display, surface)
        EGL14.eglDestroyContext(display, context)
        EGL14.eglTerminate(display)
    }
}
