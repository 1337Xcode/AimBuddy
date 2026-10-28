package com.aimbuddy.ui.components

import androidx.compose.animation.core.animate
import androidx.compose.animation.core.tween
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.Velocity
import kotlin.math.roundToInt

/** How long the bar takes to finish coming or going when a swipe ends with it halfway. */
private const val SETTLE_MS = 150

/**
 * How far a screen's top bar has slid up out of view. The bar goes while the content is scrolled
 * forward and comes back at the first scroll the other way, wherever the content is, so a short window
 * keeps its height for the content and the way back is one swipe off. [canHide] says whether the
 * content has more to show: when it has not, nothing is gained by hiding the bar and it stays.
 */
@Stable
internal class TopBarSlide(private val canHide: () -> Boolean) {
    /** The bar's own height in pixels, known once it has been laid out. */
    internal var barHeightPx = 0f

    /** 0 while the bar is shown, down to minus its height once it has gone. */
    var offsetPx by mutableFloatStateOf(0f)
        private set

    /** Lets the bar take its share of a scroll before the content scrolls, and finishes a swipe that ended halfway. */
    val connection = object : NestedScrollConnection {
        override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset {
            if (available.y < 0f && !canHide()) {
                return Offset.Zero
            }
            val before = offsetPx
            offsetPx = (before + available.y).coerceIn(-barHeightPx, 0f)
            return Offset(0f, offsetPx - before)
        }

        override suspend fun onPostFling(consumed: Velocity, available: Velocity): Velocity {
            settle()
            return Velocity.Zero
        }
    }

    /** Draws what it modifies [offsetPx] up out of view and hands the room it has left to what lies below. */
    fun slide(modifier: Modifier): Modifier = modifier.clipToBounds().layout { measurable, constraints ->
        val placeable = measurable.measure(constraints)
        barHeightPx = placeable.height.toFloat()
        val offset = offsetPx.roundToInt()
        layout(placeable.width, (placeable.height + offset).coerceAtLeast(0)) {
            placeable.place(0, offset)
        }
    }

    /** Moves the bar to whichever end it is nearer, so it is never left half in view. */
    private suspend fun settle() {
        val target = if (offsetPx < -barHeightPx / 2f) -barHeightPx else 0f
        if (offsetPx == target) {
            return
        }
        animate(initialValue = offsetPx, targetValue = target, animationSpec = tween(SETTLE_MS)) { value, _ ->
            offsetPx = value
        }
    }
}
