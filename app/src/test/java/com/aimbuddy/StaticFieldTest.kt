package com.aimbuddy

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Random

/** The logo is static and nothing else: every grey as likely as any other, and a different field every time. */
class StaticFieldTest {

    private val bubbleGrains = 22 * 22

    @Test
    fun everyLevelIsAGreyAByteCanHoldAndEveryGreyTurnsUp() {
        val levels = staticLevels(Random(1), 100_000)
        assertTrue(levels.all { it in 0..255 })
        assertEquals(256, levels.toSet().size)
    }

    @Test
    fun anyLevelIsAsLikelyAsAnyOtherSoNothingIsDarkerOrLighter() {
        val levels = staticLevels(Random(2), 100_000)
        // Eight equal bands of grey from black to white each hold an eighth of the grains, give or take about five standard deviations.
        val bands = IntArray(8)
        levels.forEach { bands[it / 32]++ }
        bands.forEach { assertEquals(12_500.0, it.toDouble(), 600.0) }
        assertEquals(127.5, levels.average(), 1.5)
    }

    @Test
    fun fieldsMadeApartDifferAndOneSeedMakesTheSameFieldAgain() {
        val first = staticLevels(Random(4), bubbleGrains)
        val second = staticLevels(Random(5), bubbleGrains)
        val grainsThatAgree = first.indices.count { first[it] == second[it] }
        // Chance alone would make about one grain in 256 agree.
        assertTrue("$grainsThatAgree of $bubbleGrains grains agree", grainsThatAgree < bubbleGrains / 20)
        assertArrayEquals(first, staticLevels(Random(4), bubbleGrains))
    }
}
