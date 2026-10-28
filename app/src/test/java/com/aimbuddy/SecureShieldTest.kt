package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Test

/** The secure window must cover all of the menu and as little else as it can: the game is what a recording is for. */
class SecureShieldTest {

    @Test
    fun coversTheMenusColumnFromTheTopToTheBottomOfTheDisplay() {
        val menu = Bounds(200, 100, 1100, 900)

        val shield = shieldBoundsFor(menu, windowLeft = 0, displayWidth = 2400, displayHeight = 1080, marginPx = 40)

        assertEquals(Bounds(160, 0, 1140, 1080), shield)
    }

    @Test
    fun countsWhereTheOverlayWindowStartsOnTheDisplay() {
        val menu = Bounds(200, 100, 1100, 900)

        val shield = shieldBoundsFor(menu, windowLeft = 72, displayWidth = 2400, displayHeight = 1080, marginPx = 40)

        assertEquals(Bounds(232, 0, 1212, 1080), shield)
    }

    @Test
    fun staysInsideTheDisplayWhenTheMenuIsAtAnEdge() {
        val atLeft = shieldBoundsFor(Bounds(4, 100, 904, 900), windowLeft = 0, displayWidth = 2400, displayHeight = 1080, marginPx = 40)
        val atRight = shieldBoundsFor(Bounds(1500, 100, 2396, 900), windowLeft = 0, displayWidth = 2400, displayHeight = 1080, marginPx = 40)

        assertEquals(Bounds(0, 0, 944, 1080), atLeft)
        assertEquals(Bounds(1460, 0, 2400, 1080), atRight)
    }

    @Test
    fun coversTheWholeDisplayWhenNothingIsKnownAboutTheMenu() {
        val shield = shieldBoundsFor(Bounds(0, 0, 0, 0), windowLeft = 0, displayWidth = 1080, displayHeight = 2400, marginPx = 40)

        assertEquals(Bounds(0, 0, 1080, 2400), shield)
    }

    @Test
    fun coversTheWholeDisplayWhenTheMenuLiesOutsideIt() {
        val shield = shieldBoundsFor(Bounds(3000, 0, 3900, 900), windowLeft = 0, displayWidth = 2400, displayHeight = 1080, marginPx = 40)

        assertEquals(Bounds(0, 0, 2400, 1080), shield)
    }
}
