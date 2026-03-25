package com.aimbuddy

import android.content.Context
import android.graphics.PixelFormat
import android.opengl.GLSurfaceView
import android.util.AttributeSet
import android.util.Log
import android.view.MotionEvent
import android.view.Surface
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * ImGuiGLSurface - GLSurfaceView for ImGui menu overlay
 *
 * Provides a proper OpenGL context for ImGui rendering separate from
 * the main ESP overlay.
 */
class ImGuiGLSurface @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : GLSurfaceView(context, attrs), GLSurfaceView.Renderer {

    companion object {
        private const val TAG = "ImGuiGLSurface"

        init {
            System.loadLibrary("esp_native")
        }

        // Native methods (GLSurfaceView renderer)
        @JvmStatic
        external fun nativeInit(assetManager: android.content.res.AssetManager, surface: Surface)

        @JvmStatic
        external fun nativeSurfaceChanged(width: Int, height: Int)

        @JvmStatic
        external fun nativeTick()

        @JvmStatic
        external fun nativeShutdown()

        @JvmStatic
        external fun nativeMotionEvent(action: Int, x: Float, y: Float): Boolean

        @JvmStatic
        external fun nativeWantsCapture(): Boolean

        @JvmStatic
        external fun nativeSetMenuVisible(visible: Boolean)
        
        @JvmStatic
        external fun nativeSetIconPosition(x: Float, y: Float)
        
        @JvmStatic
        external fun nativeSetRootAvailable(available: Boolean)
    }

    private var screenWidth = 0
    private var screenHeight = 0

    init {
        // Setup OpenGL ES 3.0 context
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 8, 16, 0)
        holder.setFormat(PixelFormat.TRANSLUCENT)
        setZOrderOnTop(true) // Draw on top
        setRenderer(this)
        renderMode = RENDERMODE_CONTINUOUSLY

        Log.i(TAG, "ImGuiGLSurface created")
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        Log.i(TAG, "Surface created")
        nativeInit(context.assets, holder.surface)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        Log.i(TAG, "Surface changed: ${width}x${height}")
        screenWidth = width
        screenHeight = height
        nativeSurfaceChanged(width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        nativeTick()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val action = when (event.action) {
            MotionEvent.ACTION_DOWN -> 0
            MotionEvent.ACTION_UP -> 1
            MotionEvent.ACTION_MOVE -> 2
            else -> return super.onTouchEvent(event)
        }

        return nativeMotionEvent(action, event.x, event.y)
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        Log.i(TAG, "Surface detached, shutting down")
        nativeShutdown()
    }
}
