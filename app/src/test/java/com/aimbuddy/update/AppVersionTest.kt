package com.aimbuddy.update

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Versions must order the way the release tags do, or the app offers the wrong update or none. */
class AppVersionTest {

    private fun version(text: String): AppVersion = requireNotNull(AppVersion.parse(text)) { "not a version: $text" }

    @Test
    fun ordersTheProjectsOwnTagsOldestFirst() {
        val tags = listOf("0.3.0-beta.1", "0.3.0-beta.2", "0.3.0-beta.3", "0.3.0-beta.10", "0.3.0", "0.3.1-beta.1", "0.3.1", "0.4.0", "1.0.0")
        val sorted = tags.shuffled().sortedBy { version(it) }
        assertEquals(tags, sorted.map { version(it).toString() })
    }

    @Test
    fun comparesPreReleaseNumbersAsNumbers() {
        assertTrue(version("1.0.0-beta.10") > version("1.0.0-beta.9"))
        assertTrue(version("1.0.0-beta") < version("1.0.0-beta.1"))
        assertTrue(version("1.0.0-alpha") < version("1.0.0-beta"))
        assertTrue(version("1.0.0-1") < version("1.0.0-alpha"))
    }

    @Test
    fun aReleaseIsNewerThanItsPreReleases() {
        assertTrue(version("2.1.0") > version("2.1.0-rc.1"))
        assertTrue(version("2.1.0-rc.1") < version("2.1.0"))
    }

    @Test
    fun readsTheLeadingVAndIgnoresABuildTag() {
        assertEquals(version("0.3.0"), version("v0.3.0"))
        assertEquals(version("0.3.0-beta.3"), version("v0.3.0-beta.3+abc123"))
    }

    @Test
    fun missingTrailingNumbersCountAsZero() {
        assertEquals(version("0.3"), version("0.3.0"))
        assertEquals(version("0.3").hashCode(), version("0.3.0").hashCode())
        assertTrue(version("0.3") < version("0.3.1"))
    }

    @Test
    fun knowsWhichVersionsArePreReleases() {
        assertTrue(version("0.3.0-beta.3").isPreRelease)
        assertFalse(version("0.3.0").isPreRelease)
    }

    @Test
    fun refusesTextThatIsNotAVersion() {
        for (text in listOf("", "latest", "v", "0.x.1", "1..2", "1.2.3-", "1.2.3-beta..1", "99999999999.0.0")) {
            assertNull("accepted \"$text\"", AppVersion.parse(text))
        }
        assertNotNull(AppVersion.parse("  0.3.0  "))
    }

    @Test
    fun printsAVersionWithoutTheLeadingV() {
        assertEquals("0.3.0-beta.3", version("v0.3.0-beta.3").toString())
    }
}
