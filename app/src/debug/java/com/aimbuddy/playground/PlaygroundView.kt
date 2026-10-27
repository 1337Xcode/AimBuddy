package com.aimbuddy.playground

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.os.SystemClock
import android.util.SparseArray
import android.view.MotionEvent
import android.view.View
import com.aimbuddy.DebugHooks
import java.io.File
import java.util.ArrayDeque
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.sin

/**
 * Draws the playground scene and turns the camera when a finger is dragged across it.
 *
 * The camera has a yaw and a pitch in degrees; everything in the world has a direction in the same
 * degrees, and a flat projection puts it on screen. A drag of the finger by some pixels turns the
 * camera by `gain` times that many screen pixels' worth of degrees, `delayMs` later. Fingers that
 * start on the stand-in buttons (the fire button and the joystick) do not turn the camera.
 */
internal class PlaygroundView(
    context: Context,
    private val config: PlaygroundConfig,
    private val logFile: File,
) : View(context) {

    private enum class Role { LOOK, BUTTON, IGNORED }

    private class Pointer(var x: Float, var y: Float, var timeMs: Long, val role: Role)

    /** A turn of the camera that has been asked for and has not happened yet. */
    private class Turn(val dueMs: Long, val yaw: Float, val pitch: Float)

    /** Something fixed in the world that gives the camera's motion something to show: a rock, a crate, a bush. */
    private class Landmark(val yaw: Float, val pitch: Float, val width: Float, val height: Float, val color: Int, val round: Boolean)

    private class Outfit(val shirt: Int, val pants: Int, val skin: Int, val hair: Int)

    /** A person standing or moving at some distance. */
    private class Character(val yaw: Float, val pitch: Float, val distance: Float, val phase: Float, val outfit: Outfit)

    private val random = java.util.Random(config.seed.toLong())
    private val landmarks = List(360) { makeLandmark() }
    private val characters = List(config.targets) { makeCharacter(it) }
    private val pointers = SparseArray<Pointer>()
    private val turns = ArrayDeque<Turn>()
    private val startMs = SystemClock.uptimeMillis()

    private var yaw = 0.0f
    private var pitch = 0.0f
    private var lookTravelX = 0.0f   // how far the looking finger has moved in all, to compare with what the assist meant to do
    private var lookTravelY = 0.0f
    private var lookPointerId = -1
    private var lookTouches = 0   // looking touches so far, to ignore the first few when asked
    private var fireHeld = false

    private val drawOrder = characters.sortedByDescending { it.distance }   // far ones first, so near ones are drawn over them
    private val headDirection = FloatArray(2)
    private val sides = intArrayOf(-1, 1)

    private val jitterSource = java.util.Random(config.seed + 1L)
    private val trueBoxes = FloatArray(config.targets * BOX_VALUES)
    private val origin = IntArray(2)

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val box = RectF()
    private val log = StringBuilder()
    private var lastFlushMs = startMs

    init {
        log.append("uptime_ms,yaw,pitch,nearest_dx,nearest_dy,nearest_dist,characters,fire,look_x,look_y\n")
    }

    // ---------------------------------------------------------------------------------------
    // The world
    // ---------------------------------------------------------------------------------------

    private fun makeLandmark(): Landmark {
        val shade = 70 + random.nextInt(110)
        val tint = random.nextInt(3)
        val color = Color.rgb(shade + if (tint == 0) 40 else 0, shade + if (tint == 1) 40 else 0, shade + if (tint == 2) 40 else 0)
        return Landmark(
            yaw = random.nextFloat() * 360f - 180f,
            pitch = random.nextFloat() * 45f - 22f,
            width = 25f + random.nextFloat() * 110f,
            height = 20f + random.nextFloat() * 90f,
            color = color,
            round = random.nextBoolean(),
        )
    }

    private fun makeCharacter(index: Int): Character {
        val shirts = intArrayOf(0xFFD7263D.toInt(), 0xFF1B9AAA.toInt(), 0xFFF4B942.toInt(), 0xFF6A4C93.toInt(), 0xFFEF476F.toInt(), 0xFF06D6A0.toInt())
        val pants = intArrayOf(0xFF22333B.toInt(), 0xFF3D405B.toInt(), 0xFF5E503F.toInt(), 0xFF1D3557.toInt())
        val skins = intArrayOf(0xFFF1C27D.toInt(), 0xFFC68642.toInt(), 0xFF8D5524.toInt(), 0xFFFFDBAC.toInt())
        val hairs = intArrayOf(0xFF2B1B17.toInt(), 0xFF6B4423.toInt(), 0xFFD9A441.toInt(), 0xFF111111.toInt())
        // The characters stand in a ring around the forward direction, all about the same distance from the crosshair.
        val angle = 2.0 * PI * index / config.targets.coerceAtLeast(1)
        val ring = if (config.targets == 1) 0f else 7f
        return Character(
            yaw = (ring * cos(angle)).toFloat(),
            pitch = (ring * 0.6 * sin(angle)).toFloat(),
            distance = config.distance * (1f + 0.2f * random.nextFloat()),
            phase = random.nextFloat() * 6.28f,
            outfit = Outfit(shirts[random.nextInt(shirts.size)], pants[random.nextInt(pants.size)], skins[random.nextInt(skins.size)], hairs[random.nextInt(hairs.size)]),
        )
    }

    /** Where a character is looking from the camera's point of view, in degrees, at `seconds` into the run. */
    private fun directionOf(character: Character, seconds: Float, out: FloatArray) {
        val turn = 2.0 * PI * 0.35 * seconds + character.phase
        when (config.pattern) {
            "still" -> {
                out[0] = character.yaw
                out[1] = character.pitch
            }
            "circle" -> {
                out[0] = character.yaw + 4f * cos(turn).toFloat()
                out[1] = character.pitch + 2f * sin(turn).toFloat()
            }
            else -> {
                out[0] = character.yaw + 6f * sin(turn).toFloat()
                out[1] = character.pitch
            }
        }
    }

    // ---------------------------------------------------------------------------------------
    // Drawing
    // ---------------------------------------------------------------------------------------

    private fun pixelsPerDegree() = width / 90f

    /** The difference between two directions in degrees, brought into -180 to 180. */
    private fun wrap(degrees: Float): Float {
        var wrapped = degrees % 360f
        if (wrapped > 180f) wrapped -= 360f
        if (wrapped < -180f) wrapped += 360f
        return wrapped
    }

    override fun onDraw(canvas: Canvas) {
        val now = SystemClock.uptimeMillis()
        applyDueTurns(now)
        val ppd = pixelsPerDegree()
        val centerX = width / 2f
        val centerY = height / 2f

        drawBackdrop(canvas, centerY + pitch * ppd)
        drawLandmarks(canvas, ppd, centerX, centerY)
        val seconds = (now - startMs) * 0.001f
        var nearestDx = 0f
        var nearestDy = 0f
        var nearest = Float.MAX_VALUE
        var boxCount = 0
        getLocationOnScreen(origin)
        for (character in drawOrder) {
            directionOf(character, seconds, headDirection)
            val x = centerX + wrap(headDirection[0] - yaw) * ppd
            val y = centerY - (headDirection[1] - pitch) * ppd
            val figure = 103f / character.distance * ppd
            drawFigure(canvas, x, y, figure, character.outfit)
            boxCount = addTrueBox(boxCount, x + origin[0], y + origin[1], figure)
            val dx = x - centerX
            val dy = y - centerY
            if (hypot(dx, dy) < nearest) {
                nearest = hypot(dx, dy)
                nearestDx = dx
                nearestDy = dy
            }
        }
        drawControls(canvas, centerX, centerY)
        if (config.synthetic) {
            DebugHooks.feedDetections(trueBoxes.copyOf(boxCount * BOX_VALUES), System.nanoTime())
        }
        record(now, nearestDx, nearestDy, nearest)
        postInvalidateOnAnimation()
    }

    /**
     * Writes the box a person detector would draw around a figure into [trueBoxes], in screen pixels
     * and with a little of the jitter a real detector has. Returns the number of boxes written so far.
     */
    private fun addTrueBox(count: Int, headX: Float, headY: Float, figure: Float): Int {
        val jitter = { (jitterSource.nextFloat() - 0.5f) * JITTER * figure }
        val left = headX - 0.2f * figure + jitter()
        val top = headY - 0.065f * figure + jitter()
        val right = headX + 0.2f * figure + jitter()
        val bottom = headY + 0.945f * figure + jitter()
        val at = count * BOX_VALUES
        trueBoxes[at] = left
        trueBoxes[at + 1] = top
        trueBoxes[at + 2] = right - left
        trueBoxes[at + 3] = bottom - top
        trueBoxes[at + 4] = TRUE_BOX_SCORE
        return count + 1
    }

    private fun drawBackdrop(canvas: Canvas, horizonY: Float) {
        paint.shader = LinearGradient(0f, 0f, 0f, horizonY.coerceIn(1f, height.toFloat()), 0xFF4A90D9.toInt(), 0xFFBFE3F5.toInt(), Shader.TileMode.CLAMP)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
        paint.shader = null
        paint.color = 0xFF55663E.toInt()
        canvas.drawRect(0f, horizonY.coerceIn(0f, height.toFloat()), width.toFloat(), height.toFloat(), paint)
    }

    private fun drawLandmarks(canvas: Canvas, ppd: Float, centerX: Float, centerY: Float) {
        for (landmark in landmarks) {
            val x = centerX + wrap(landmark.yaw - yaw) * ppd
            val y = centerY - (landmark.pitch - pitch) * ppd
            if (x < -150f || x > width + 150f || y < -150f || y > height + 150f) {
                continue
            }
            paint.color = landmark.color
            box.set(x - landmark.width / 2, y - landmark.height / 2, x + landmark.width / 2, y + landmark.height / 2)
            if (landmark.round) canvas.drawOval(box, paint) else canvas.drawRect(box, paint)
        }
    }

    /** A person from head to feet; (x, y) is the middle of the head and `height` the whole figure. */
    private fun drawFigure(canvas: Canvas, x: Float, y: Float, height: Float, outfit: Outfit) {
        val head = 0.065f * height
        val shoulder = y + head * 1.5f
        val hip = shoulder + 0.30f * height
        val feet = y + 0.93f * height
        // Legs and shoes.
        paint.color = outfit.pants
        for (side in sides) {
            box.set(x + side * 0.065f * height - 0.05f * height, hip, x + side * 0.065f * height + 0.05f * height, feet)
            canvas.drawRect(box, paint)
        }
        paint.color = 0xFF111111.toInt()
        for (side in sides) {
            box.set(x + side * 0.065f * height - 0.065f * height, feet - 0.02f * height, x + side * 0.065f * height + 0.075f * height, feet + 0.015f * height)
            canvas.drawOval(box, paint)
        }
        // Torso and arms.
        paint.color = outfit.shirt
        box.set(x - 0.12f * height, shoulder, x + 0.12f * height, hip)
        canvas.drawRect(box, paint)
        paint.strokeWidth = 0.055f * height
        paint.strokeCap = Paint.Cap.ROUND
        for (side in sides) {
            paint.color = outfit.shirt
            canvas.drawLine(x + side * 0.12f * height, shoulder + 0.03f * height, x + side * 0.17f * height, shoulder + 0.17f * height, paint)
            paint.color = outfit.skin
            canvas.drawLine(x + side * 0.17f * height, shoulder + 0.17f * height, x + side * 0.15f * height, shoulder + 0.27f * height, paint)
        }
        // A rifle held across the body.
        paint.color = 0xFF2A2A2A.toInt()
        box.set(x - 0.2f * height, shoulder + 0.2f * height, x + 0.18f * height, shoulder + 0.235f * height)
        canvas.drawRect(box, paint)
        // Neck, head and hair.
        paint.color = outfit.skin
        box.set(x - 0.03f * height, shoulder - head * 0.6f, x + 0.03f * height, shoulder)
        canvas.drawRect(box, paint)
        canvas.drawCircle(x, y, head, paint)
        paint.color = outfit.hair
        box.set(x - head, y - head, x + head, y)
        canvas.drawArc(box, 180f, 180f, true, paint)
        paint.color = Color.BLACK
        canvas.drawCircle(x - head * 0.35f, y - head * 0.1f, head * 0.1f, paint)
        canvas.drawCircle(x + head * 0.35f, y - head * 0.1f, head * 0.1f, paint)
    }

    /** The crosshair and the stand-in buttons, which stay where they are while the scene moves. */
    private fun drawControls(canvas: Canvas, centerX: Float, centerY: Float) {
        paint.color = Color.WHITE
        paint.strokeWidth = 3f
        canvas.drawLine(centerX - 12f, centerY, centerX + 12f, centerY, paint)
        canvas.drawLine(centerX, centerY - 12f, centerX, centerY + 12f, paint)
        paint.color = if (fireHeld) 0xAAFF5555.toInt() else 0x66FFFFFF
        canvas.drawCircle(width * FIRE_X, height * FIRE_Y, buttonRadius(), paint)
        paint.color = 0x44FFFFFF
        canvas.drawCircle(width * STICK_X, height * STICK_Y, buttonRadius() * 1.3f, paint)
        paint.color = 0xFF333333.toInt()
        box.set(width * 0.62f, height * 0.78f, width * 0.86f, height * 0.98f)
        canvas.drawRoundRect(box, 18f, 18f, paint)
    }

    private fun buttonRadius() = height * 0.1f

    // ---------------------------------------------------------------------------------------
    // Touch: turning the camera
    // ---------------------------------------------------------------------------------------

    private fun roleAt(x: Float, y: Float, pointerId: Int): Role {
        val onFire = hypot(x - width * FIRE_X, y - height * FIRE_Y) <= buttonRadius()
        val onStick = hypot(x - width * STICK_X, y - height * STICK_Y) <= buttonRadius() * 1.3f
        return when {
            onFire || onStick -> Role.BUTTON
            config.singleLook && lookPointerId != -1 && lookPointerId != pointerId -> Role.IGNORED
            else -> Role.LOOK
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val index = event.actionIndex
                val id = event.getPointerId(index)
                var role = roleAt(event.getX(index), event.getY(index), id)
                if (role == Role.LOOK && ++lookTouches <= config.ignoreTouches) {
                    role = Role.IGNORED
                }
                pointers.put(id, Pointer(event.getX(index), event.getY(index), event.eventTime, role))
                if (role == Role.LOOK) lookPointerId = id
                updateFire()
            }
            MotionEvent.ACTION_MOVE -> for (index in 0 until event.pointerCount) {
                pointers.get(event.getPointerId(index))?.let { moveLook(it, event.getX(index), event.getY(index), event.eventTime) }
            }
            MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                val id = event.getPointerId(event.actionIndex)
                pointers.remove(id)
                if (id == lookPointerId) lookPointerId = -1
                updateFire()
            }
        }
        return true
    }

    private fun updateFire() {
        fireHeld = (0 until pointers.size()).any {
            val pointer = pointers.valueAt(it)
            pointer.role == Role.BUTTON && hypot(pointer.x - width * FIRE_X, pointer.y - height * FIRE_Y) <= buttonRadius() * 1.5f
        }
    }

    /** Asks for the camera turn that follows a finger's move, to happen after the game's delay. */
    private fun moveLook(pointer: Pointer, x: Float, y: Float, timeMs: Long) {
        val dx = x - pointer.x
        val dy = y - pointer.y
        val elapsedMs = (timeMs - pointer.timeMs).coerceAtLeast(1L)
        pointer.x = x
        pointer.y = y
        pointer.timeMs = timeMs
        if (pointer.role != Role.LOOK || (dx == 0f && dy == 0f)) {
            return
        }
        lookTravelX += dx
        lookTravelY += dy
        val speed = hypot(dx, dy) / elapsedMs * 1000f
        val gain = config.gain * (1f + config.curve * min(speed / 3000f, 1f))
        val ppd = pixelsPerDegree()
        val up = if (config.invertY) 1f else -1f
        turns.add(Turn(timeMs + config.delayMs, gain * dx / ppd, up * gain * dy / ppd))
    }

    private fun applyDueTurns(nowMs: Long) {
        while (turns.isNotEmpty() && turns.first.dueMs <= nowMs) {
            val turn = turns.removeFirst()
            yaw += turn.yaw
            pitch = (pitch + turn.pitch).coerceIn(-60f, 60f)
        }
    }

    // ---------------------------------------------------------------------------------------
    // The record of what really happened
    // ---------------------------------------------------------------------------------------

    private fun record(nowMs: Long, dx: Float, dy: Float, distance: Float) {
        log.append(nowMs).append(',').append("%.3f".format(yaw)).append(',').append("%.3f".format(pitch)).append(',')
            .append("%.1f".format(dx)).append(',').append("%.1f".format(dy)).append(',').append("%.1f".format(distance)).append(',')
            .append(characters.size).append(',').append(if (fireHeld) 1 else 0).append(',')
            .append("%.1f".format(lookTravelX)).append(',').append("%.1f".format(lookTravelY)).append('\n')
        if (nowMs - lastFlushMs > FLUSH_EVERY_MS) {
            flushLog()
        }
    }

    /** Appends what has been recorded since the last time to the file. */
    fun flushLog() {
        if (log.isEmpty()) {
            return
        }
        lastFlushMs = SystemClock.uptimeMillis()
        runCatching { logFile.appendText(log.toString()) }
        log.setLength(0)
    }

    private companion object {
        /** Numbers per box handed to the assist: x, y, width, height, score. */
        const val BOX_VALUES = 5

        /** How much each side of a handed-over box wanders, as a share of the figure's height. */
        const val JITTER = 0.03f
        const val TRUE_BOX_SCORE = 0.9f
        const val FIRE_X = 0.9f
        const val FIRE_Y = 0.7f
        const val STICK_X = 0.12f
        const val STICK_Y = 0.72f
        const val FLUSH_EVERY_MS = 3000L
    }
}
