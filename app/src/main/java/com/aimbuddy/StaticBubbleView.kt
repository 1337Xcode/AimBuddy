package com.aimbuddy

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.Canvas
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Shader
import android.view.View
import java.util.Random
import kotlin.math.min

/**
 * The floating button: a bubble of TV static, the same kind of static as the app's icon. It is static
 * to its edge, with no rim, no outline and no shading, so nothing in it is anything but static. The
 * static is made when the button is created, so each time the overlay is shown the button has a
 * pattern of its own.
 */
class StaticBubbleView(context: Context) : View(context) {

    private val noise = makeNoise()

    // The static is a small picture stretched over the bubble without smoothing, so each grain stays square.
    private val grain = Paint(Paint.ANTI_ALIAS_FLAG).apply { isFilterBitmap = false }

    // A tap makes OverlayController call this, and so does an accessibility service pressing the button.
    override fun performClick(): Boolean = super.performClick()

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        val shader = BitmapShader(noise, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP)
        shader.setLocalMatrix(Matrix().apply { setScale(width / GRAINS.toFloat(), height / GRAINS.toFloat()) })
        grain.shader = shader
    }

    override fun onDraw(canvas: Canvas) {
        val centerX = width / 2f
        val centerY = height / 2f
        canvas.drawCircle(centerX, centerY, min(centerX, centerY), grain)
    }

    /** A small picture of static, one pixel to a grain. */
    private fun makeNoise(): Bitmap {
        val levels = staticLevels(Random(System.nanoTime()), GRAINS * GRAINS)
        val pixels = IntArray(levels.size) { index ->
            val grey = levels[index]
            (0xFF shl 24) or (grey shl 16) or (grey shl 8) or grey
        }
        return Bitmap.createBitmap(pixels, GRAINS, GRAINS, Bitmap.Config.ARGB_8888)
    }

    private companion object {
        /** Grains across the bubble: its size (44 dp) over the size of a grain (2 dp), as in the launcher icon. */
        const val GRAINS = 22
    }
}
