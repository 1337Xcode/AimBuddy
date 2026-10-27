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
import kotlin.math.hypot
import kotlin.math.min

/**
 * The floating button: a round bubble full of TV static, like the app's icon. The static is made
 * once when the button is created, so each time the overlay is shown the button has a pattern of
 * its own.
 */
class StaticBubbleView(context: Context) : View(context) {

    private val density = context.resources.displayMetrics.density
    private val noise = makeNoise()

    // The static is a small picture stretched over the bubble without smoothing, so each grain stays square.
    private val grain = Paint(Paint.ANTI_ALIAS_FLAG).apply { isFilterBitmap = false }
    private val rim = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = RIM_DP * density
        color = RIM_COLOR
    }
    private val edge = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = EDGE_DP * density
        color = EDGE_COLOR
    }

    override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
        val shader = BitmapShader(noise, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP)
        shader.setLocalMatrix(Matrix().apply { setScale(width / GRAINS.toFloat(), height / GRAINS.toFloat()) })
        grain.shader = shader
    }

    override fun onDraw(canvas: Canvas) {
        val centerX = width / 2f
        val centerY = height / 2f
        val edgeWidth = EDGE_DP * density
        val rimWidth = RIM_DP * density
        val radius = min(centerX, centerY) - edgeWidth

        canvas.drawCircle(centerX, centerY, radius, grain)
        canvas.drawCircle(centerX, centerY, radius - rimWidth / 2f, rim)
        canvas.drawCircle(centerX, centerY, radius + edgeWidth / 2f, edge)
    }

    /** A small picture of static: mostly mid gray, with bright and dark grains, darker towards the edge like the glass of a screen. */
    private fun makeNoise(): Bitmap {
        val random = Random(System.nanoTime())
        val pixels = IntArray(GRAINS * GRAINS)
        val middle = (GRAINS - 1) / 2f
        for (row in 0 until GRAINS) {
            for (column in 0 until GRAINS) {
                val level = (128 + random.nextGaussian() * 62).toInt().coerceIn(0, 255)
                val shade = 1f - VIGNETTE * (hypot(column - middle, row - middle) / middle).coerceIn(0f, 1f)
                val gray = (level * shade).toInt()
                pixels[row * GRAINS + column] = (0xFF shl 24) or (gray shl 16) or (gray shl 8) or gray
            }
        }
        return Bitmap.createBitmap(pixels, GRAINS, GRAINS, Bitmap.Config.ARGB_8888)
    }

    private companion object {
        /** Grains across the bubble. */
        const val GRAINS = 18

        /** How much darker the grains at the edge are than the ones in the middle, 0 to 1. */
        const val VIGNETTE = 0.45f

        /** A light ring just inside the edge and a thin dark one just outside it, so the bubble shows on any picture. */
        const val RIM_DP = 2f
        const val EDGE_DP = 1.5f
        const val RIM_COLOR = 0xD9FFFFFF.toInt()
        const val EDGE_COLOR = 0x99000000.toInt()
    }
}
