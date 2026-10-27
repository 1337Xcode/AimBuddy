package com.aimbuddy

import android.os.PowerManager
import org.junit.Assert.assertEquals
import org.junit.Test

/** A phone that is not hot must not be slowed down, and one that is must be eased off more the hotter it gets. */
class ThermalGovernorTest {

    @Test
    fun aCoolPhoneIsNotLimited() {
        assertEquals(0f, detectionLimitFor(PowerManager.THERMAL_STATUS_NONE), 0f)
        assertEquals(0f, detectionLimitFor(PowerManager.THERMAL_STATUS_LIGHT), 0f)
    }

    @Test
    fun aWarmPhoneRunsFewerDetections() {
        assertEquals(30f, detectionLimitFor(PowerManager.THERMAL_STATUS_MODERATE), 0f)
    }

    @Test
    fun aHotPhoneRunsFewerStill() {
        for (status in listOf(
            PowerManager.THERMAL_STATUS_SEVERE,
            PowerManager.THERMAL_STATUS_CRITICAL,
            PowerManager.THERMAL_STATUS_EMERGENCY,
            PowerManager.THERMAL_STATUS_SHUTDOWN,
        )) {
            assertEquals("status $status", 15f, detectionLimitFor(status), 0f)
        }
    }
}
