package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Test

/** What the app says about Shizuku before anything is started: running or not, and allowed or not. */
class ShizukuStatusTest {

    @Test
    fun shizukuIsReadyWhenItRunsAndHasAllowedAimBuddy() {
        assertEquals(ShizukuStatus.READY, shizukuStatus(installed = true, running = true, allowed = true))
    }

    @Test
    fun aRunningShizukuStillNeedsAimBuddyToBeAllowed() {
        assertEquals(ShizukuStatus.NEEDS_PERMISSION, shizukuStatus(installed = true, running = true, allowed = false))
    }

    @Test
    fun aRunningServiceCountsAsInstalledWhateverTheLauncherSays() {
        assertEquals(ShizukuStatus.NEEDS_PERMISSION, shizukuStatus(installed = false, running = true, allowed = false))
    }

    @Test
    fun anInstalledShizukuThatIsNotRunningHasToBeStarted() {
        assertEquals(ShizukuStatus.NOT_RUNNING, shizukuStatus(installed = true, running = false, allowed = false))
    }

    @Test
    fun noShizukuAtAllIsNotInstalled() {
        assertEquals(ShizukuStatus.NOT_INSTALLED, shizukuStatus(installed = false, running = false, allowed = false))
    }
}
