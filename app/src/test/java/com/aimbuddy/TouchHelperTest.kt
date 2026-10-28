package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.FutureTask
import java.util.concurrent.TimeUnit

/**
 * Stopping the assist while the touch helper is still starting interrupts the thread that waits for
 * it. That wait has to give up quietly: an exception there ended the whole app in 0.3.5-beta.1.
 */
class TouchHelperTest {

    private data class WaitResult(val opened: Boolean, val interruptKept: Boolean)

    @Test
    fun aLatchThatOpensInTimeEndsTheWaitWithTrue() {
        val latch = CountDownLatch(1)
        latch.countDown()

        assertTrue(awaitUnlessInterrupted(latch, 5))
    }

    @Test
    fun aLatchThatStaysClosedEndsTheWaitWithFalseAndLeavesNoInterrupt() {
        val latch = CountDownLatch(1)

        assertFalse(awaitUnlessInterrupted(latch, 0))
        assertFalse(Thread.currentThread().isInterrupted)
    }

    @Test
    fun aThreadInterruptedWhileWaitingGivesUpAndKeepsTheInterrupt() {
        val latch = CountDownLatch(1)
        val wait = FutureTask {
            val opened = awaitUnlessInterrupted(latch, 60)
            WaitResult(opened, interruptKept = Thread.currentThread().isInterrupted)
        }
        val waiter = Thread(wait)
        waiter.start()
        waitUntilWaiting(waiter)

        waiter.interrupt()

        // An exception thrown by the wait comes out of get() as an ExecutionException and fails the test.
        assertEquals(WaitResult(opened = false, interruptKept = true), wait.get(10, TimeUnit.SECONDS))
    }

    /** Returns once [thread] sits inside its timed wait, so the interrupt lands during the wait and not before it. */
    private fun waitUntilWaiting(thread: Thread) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
        while (thread.state != Thread.State.TIMED_WAITING) {
            check(System.nanoTime() < deadline) { "the thread never started waiting (it is ${thread.state})" }
            Thread.sleep(1)
        }
    }
}
