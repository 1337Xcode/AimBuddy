package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Test

/** The touch test on the touch access screen: the route it asks for, the answer it gives, and that the touchscreen is always given back. */
class TouchTestOutcomeTest {

    @Test
    fun aHelperThatConnectsPassesTheTestAndIsStopped() {
        var stops = 0
        val result = runTouchTest(
            preferred = 1,
            start = { TouchHelper.Outcome.Connected(TouchHelper.Route.SHIZUKU) },
            stop = { stops++ },
        )
        assertEquals(TouchTest.Passed(TouchHelper.Route.SHIZUKU), result)
        assertEquals(1, stops)
    }

    @Test
    fun aHelperThatCannotBeUsedFailsTheTestWithItsReasonAndIsStopped() {
        var stops = 0
        val result = runTouchTest(
            preferred = 0,
            start = { TouchHelper.Outcome.Failed(TouchHelper.Reason.GRAB_FAILED, "Another program holds the touchscreen.") },
            stop = { stops++ },
        )
        assertEquals(TouchTest.Failed(TouchHelper.Reason.GRAB_FAILED, "Another program holds the touchscreen."), result)
        assertEquals(1, stops)
    }

    @Test
    fun aStartThatThrowsFailsTheTestAndTheHelperIsStillStopped() {
        var stops = 0
        val result = runTouchTest(
            preferred = 2,
            start = { throw IllegalStateException("the binder went away") },
            stop = { stops++ },
        )
        assertEquals(TouchTest.Failed(TouchHelper.Reason.UNREACHABLE, "the binder went away"), result)
        assertEquals(1, stops)
    }

    @Test
    fun aLibraryThatIsNotTheOneThisBuildNeedsFailsTheTestToo() {
        var stops = 0
        val result = runTouchTest(
            preferred = 1,
            start = { throw NoSuchMethodError("rikka.shizuku.Shizuku.bindUserService") },
            stop = { stops++ },
        )
        assertEquals(TouchTest.Failed(TouchHelper.Reason.UNREACHABLE, "rikka.shizuku.Shizuku.bindUserService"), result)
        assertEquals(1, stops)
    }

    @Test
    fun theTestStartsTheHelperByTheRouteThePlayerChose() {
        val asked = mutableListOf<Int>()
        for (preferred in 0..2) {
            runTouchTest(
                preferred = preferred,
                start = {
                    asked += it
                    TouchHelper.Outcome.Connected(TouchHelper.Route.ROOT)
                },
                stop = { },
            )
        }
        assertEquals(listOf(0, 1, 2), asked)
    }
}
