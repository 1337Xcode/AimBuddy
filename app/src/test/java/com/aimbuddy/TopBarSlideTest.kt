package com.aimbuddy

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import com.aimbuddy.ui.components.TopBarSlide
import org.junit.Assert.assertEquals
import org.junit.Test

/** How a screen's top bar makes way for the content in a short window: it takes the scroll first, and gives it back the other way. */
class TopBarSlideTest {

    private fun barOf(heightPx: Float, canHide: Boolean = true): TopBarSlide =
        TopBarSlide(canHide = { canHide }).also { it.barHeightPx = heightPx }

    /** A drag of [dy] pixels (negative: the finger moves up and the content scrolls forward); returns what the bar took of it. */
    private fun TopBarSlide.dragBy(dy: Float): Offset =
        connection.onPreScroll(Offset(0f, dy), NestedScrollSource.UserInput)

    @Test
    fun theBarTakesTheScrollUntilItHasGone() {
        val bar = barOf(heightPx = 100f)
        assertEquals(Offset(0f, -40f), bar.dragBy(-40f))   // all of the drag moved the bar
        assertEquals(-40f, bar.offsetPx, 0f)
        assertEquals(Offset(0f, -60f), bar.dragBy(-80f))   // 60 px were left for the bar, 20 go on to the content
        assertEquals(-100f, bar.offsetPx, 0f)
        assertEquals(Offset.Zero, bar.dragBy(-30f))        // the bar has gone: the content has all of it
    }

    @Test
    fun theBarComesBackAtTheFirstDragTheOtherWay() {
        val bar = barOf(heightPx = 100f)
        bar.dragBy(-100f)
        assertEquals(Offset(0f, 25f), bar.dragBy(25f))
        assertEquals(-75f, bar.offsetPx, 0f)
    }

    @Test
    fun aBarThatIsShownLeavesADragBackToTheContent() {
        val bar = barOf(heightPx = 100f)
        assertEquals(Offset.Zero, bar.dragBy(30f))
        assertEquals(0f, bar.offsetPx, 0f)
    }

    @Test
    fun theBarStaysWhenTheContentHasNothingMoreToShow() {
        val bar = barOf(heightPx = 100f, canHide = false)
        assertEquals(Offset.Zero, bar.dragBy(-50f))
        assertEquals(0f, bar.offsetPx, 0f)
    }
}
