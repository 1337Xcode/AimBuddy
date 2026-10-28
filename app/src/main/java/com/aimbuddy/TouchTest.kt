package com.aimbuddy

import android.util.Log

private const val TAG = "TouchTest"

/** What the touch access screen shows of the test the player can run. */
sealed interface TouchTest {
    /** No test has been run, or the choice it was run for has changed. */
    data object Idle : TouchTest

    /** The helper is being started. */
    data object Running : TouchTest

    /** The helper started through this route and took the touchscreen over. */
    data class Passed(val route: TouchHelper.Route) : TouchTest

    /** The helper could not be used: why, and what it said. */
    data class Failed(val reason: Int, val detail: String) : TouchTest
}

/**
 * Starts the touch helper once, by the route the player chose, to learn whether this phone lets AimBuddy
 * move the aim, and ends it again whatever happened, so the touchscreen is never left taken. Blocks
 * while the helper starts (root may ask the player), so call it off the main thread.
 */
internal fun runTouchTest(preferred: Int, start: (Int) -> TouchHelper.Outcome, stop: () -> Unit): TouchTest {
    val outcome = try {
        start(preferred)
    } catch (e: Exception) {
        Log.w(TAG, "The touch test could not start the helper: ${e.message}", e)
        TouchHelper.Outcome.Failed(TouchHelper.Reason.UNREACHABLE, e.message ?: e.javaClass.simpleName)
    } finally {
        stop()
    }
    return when (outcome) {
        is TouchHelper.Outcome.Connected -> TouchTest.Passed(outcome.route)
        is TouchHelper.Outcome.Failed -> TouchTest.Failed(outcome.reason, outcome.detail)
    }
}
