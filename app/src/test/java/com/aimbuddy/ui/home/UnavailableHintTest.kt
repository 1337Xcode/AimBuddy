package com.aimbuddy.ui.home

import com.aimbuddy.R
import com.aimbuddy.TouchHelper.Reason
import org.junit.Assert.assertEquals
import org.junit.Test

/** A touch helper that could not be used must say why, because what the player can do about it depends on the reason. */
class UnavailableHintTest {

    @Test
    fun noShizukuAndNoRootSaysWhatIsNeeded() {
        assertEquals(R.string.hint_running_no_assist, unavailableHint(Reason.NO_PRIVILEGE))
    }

    @Test
    fun aPhoneThatRefusesTheTouchscreenSaysSo() {
        for (reason in listOf(Reason.NO_TOUCHSCREEN, Reason.TOUCHSCREEN_DENIED, Reason.VIRTUAL_DEVICE_DENIED)) {
            assertEquals("reason $reason", R.string.hint_touch_denied, unavailableHint(reason))
        }
    }

    @Test
    fun aTouchscreenHeldByAnotherProgramSaysSo() {
        assertEquals(R.string.hint_touch_busy, unavailableHint(Reason.GRAB_FAILED))
    }

    @Test
    fun anythingElseAsksForAnotherTry() {
        for (reason in listOf(Reason.UNREACHABLE, Reason.BAD_CLIENT, Reason.HEARTBEAT_LOST, 999)) {
            assertEquals("reason $reason", R.string.hint_touch_unreachable, unavailableHint(reason))
        }
    }
}
