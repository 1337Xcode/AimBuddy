package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Knowing that the touch helper dropped out, and not starting it again for ever. */
class TouchLinkWatchTest {

    // The native side packs its answer as (connected << 24) | (reason << 8) | state.
    private fun pack(connected: Boolean, reason: Int, state: Int) = (if (connected) 1 shl 24 else 0) or (reason shl 8) or state

    @Test
    fun theThreeNumbersComeBackOutOfOnePackedInt() {
        assertEquals(LinkStatus(connected = true, state = 3, reason = 0), decodeLinkStatus(pack(true, 0, 3)))
        assertEquals(LinkStatus(connected = false, state = 4, reason = 7), decodeLinkStatus(pack(false, 7, 4)))
        assertEquals(LinkStatus(connected = false, state = 1, reason = 65535), decodeLinkStatus(pack(false, 65535, 1)))
    }

    @Test
    fun aPlayerTakingTheScreenBackIsNotUndone() {
        assertFalse(isWorthRestarting(TouchHelper.Reason.ESCAPE_KEYS))
        for (reason in listOf(TouchHelper.Reason.NONE, TouchHelper.Reason.HEARTBEAT_LOST, TouchHelper.Reason.DEVICE_LOST, TouchHelper.Reason.UNREACHABLE)) {
            assertTrue("reason $reason", isWorthRestarting(reason))
        }
    }

    @Test
    fun aHelperIsStartedAgainAFewTimesAndThenNoMore() {
        val budget = RestartBudget(maxRestarts = 3, windowMs = 60_000)

        assertTrue(budget.tryUse(0))
        assertTrue(budget.tryUse(10_000))
        assertTrue(budget.tryUse(20_000))
        assertFalse(budget.tryUse(30_000))
    }

    @Test
    fun anOldRestartStopsCountingOnceItsWindowIsOver() {
        val budget = RestartBudget(maxRestarts = 2, windowMs = 60_000)
        assertTrue(budget.tryUse(0))
        assertTrue(budget.tryUse(30_000))
        assertFalse(budget.tryUse(59_000))

        assertTrue(budget.tryUse(61_000))   // the one at 0 is over a minute old now
        assertFalse(budget.tryUse(62_000))
    }
}
