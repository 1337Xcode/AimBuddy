package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** What the models screen says about a download: how far it is, how fast, and how long it will take. */
class TransferProgressTest {

    @Test
    fun theFractionIsHowMuchHasArrivedOfTheTotal() {
        assertEquals(0.25f, TransferProgress(bytesDone = 250, bytesTotal = 1000).fraction, 1e-6f)
        assertEquals(0f, TransferProgress().fraction, 0f)   // nothing is known yet
        assertEquals(1f, TransferProgress(bytesDone = 1200, bytesTotal = 1000).fraction, 0f)   // never past the end
    }

    @Test
    fun theTimeLeftIsWhatRemainsOverTheSpeedRoundedUp() {
        assertEquals(8L, TransferProgress(bytesDone = 2_000_000, bytesTotal = 10_000_000, bytesPerSecond = 1_000_000).secondsLeft)
        assertEquals(1L, TransferProgress(bytesDone = 999_999, bytesTotal = 1_000_000, bytesPerSecond = 1_000_000).secondsLeft)
    }

    @Test
    fun noTimeIsGivenWhileTheSpeedIsNotKnownOrWhenNothingIsLeft() {
        assertNull(TransferProgress(bytesDone = 5, bytesTotal = 10, bytesPerSecond = 0).secondsLeft)
        assertNull(TransferProgress(bytesDone = 10, bytesTotal = 10, bytesPerSecond = 500).secondsLeft)
    }

    @Test
    fun theMeterReadsASteadySpeedAfterAMoment() {
        val meter = TransferMeter()
        assertEquals(0L, meter.add(0, 0))                 // nothing to go on at the first sample
        assertEquals(0L, meter.add(200, 200_000))         // and too little time after it
        assertEquals(1_000_000L, meter.add(1_000, 1_000_000))
        assertEquals(1_000_000L, meter.add(3_000, 3_000_000))
    }

    @Test
    fun theMeterFollowsAConnectionThatSlowsDown() {
        val meter = TransferMeter()
        var bytes = 0L
        var speed = 0L
        for (second in 0..4) {           // 2 MB a second at first
            speed = meter.add(second * 1000L, bytes)
            bytes += 2_000_000
        }
        assertTrue(speed in 1_900_000L..2_100_000L)
        for (second in 5..14) {          // then 100 kB a second: ten seconds on, the old speed is forgotten
            speed = meter.add(second * 1000L, bytes)
            bytes += 100_000
        }
        assertTrue("speed was $speed", speed in 90_000L..110_000L)
    }
}
