package com.aimbuddy.update

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.fail
import org.junit.Test

/** The update must come from the newest suitable release in GitHub's list, and from nowhere else. */
class ReleaseFeedTest {

    private fun version(text: String): AppVersion = requireNotNull(AppVersion.parse(text))

    /** One entry of GitHub's list of releases, with the fields the app reads. */
    private fun release(
        tag: String,
        prerelease: Boolean = tag.contains('-'),
        draft: Boolean = false,
        assetNames: List<String> = listOf("AimBuddy-$tag.apk"),
        digest: String? = "sha256:ABCDEF0123456789",
    ): JSONObject {
        val assets = JSONArray()
        for (assetName in assetNames) {
            val asset = JSONObject()
                .put("name", assetName)
                .put("size", 13271663L)
                .put("browser_download_url", "https://github.com/1337Xcode/AimBuddy/releases/download/$tag/$assetName")
            if (digest != null) {
                asset.put("digest", digest)
            }
            assets.put(asset)
        }
        return JSONObject().put("tag_name", tag).put("prerelease", prerelease).put("draft", draft).put("assets", assets)
    }

    private fun feed(vararg releases: JSONObject): String = JSONArray(releases.toList()).toString()

    /** What the app (not the debug build) is offered from [json]. */
    private fun forTheApp(json: String, current: String): UpdateInfo? = ReleaseFeed.newest(json, version(current), debugBuild = false)

    @Test
    fun offersNothingWhenThisIsTheNewestRelease() {
        val json = feed(release("v0.3.0-beta.3"), release("v0.3.0-beta.2"))
        assertNull(forTheApp(json, "0.3.0-beta.3"))
    }

    @Test
    fun offersTheNewestReleaseWhateverTheOrderOfTheList() {
        val json = feed(release("v0.3.0-beta.2"), release("v0.3.0-beta.4"), release("v0.3.0-beta.3"))
        val update = forTheApp(json, "0.3.0-beta.1")
        assertNotNull(update)
        assertEquals("0.3.0-beta.4", update!!.version)
        assertEquals("https://github.com/1337Xcode/AimBuddy/releases/download/v0.3.0-beta.4/AimBuddy-v0.3.0-beta.4.apk", update.apkUrl)
        assertEquals(13271663L, update.sizeBytes)
    }

    @Test
    fun offersAnEarlyBetaOfALaterPatchOverTheLastBetaOfAnEarlierOne() {
        val json = feed(release("v0.3.5-beta.1"), release("v0.3.0-beta.3"))
        assertEquals("0.3.5-beta.1", forTheApp(json, "0.3.0-beta.3")!!.version)
        assertNull(forTheApp(json, "0.3.5-beta.1"))
    }

    @Test
    fun readsTheChecksumGitHubListsInLowerCase() {
        val update = forTheApp(feed(release("v0.4.0")), "0.3.0")
        assertEquals("abcdef0123456789", update!!.sha256)
    }

    @Test
    fun hasNoChecksumWhenGitHubListsNone() {
        val update = forTheApp(feed(release("v0.4.0", digest = null)), "0.3.0")
        assertNull(update!!.sha256)
    }

    @Test
    fun skipsDraftsAndReleasesWithoutAnApk() {
        val json = feed(
            release("v0.5.0", draft = true),
            release("v0.4.5", assetNames = emptyList()),
            release("v0.4.2", assetNames = listOf("source.zip")),
            release("v0.4.0"),
        )
        assertEquals("0.4.0", forTheApp(json, "0.3.0")!!.version)
    }

    @Test
    fun leavesPreReleasesToThoseWhoRunOne() {
        val json = feed(release("v0.5.0-beta.1"), release("v0.4.0", prerelease = false))
        assertEquals("0.4.0", forTheApp(json, "0.3.0")!!.version)
        assertEquals("0.5.0-beta.1", forTheApp(json, "0.3.0-beta.2")!!.version)
    }

    @Test
    fun ignoresTagsThatAreNotVersions() {
        val json = feed(release("nightly"), release("v0.4.0"))
        assertEquals("0.4.0", forTheApp(json, "0.3.0")!!.version)
    }

    @Test
    fun offersTheAppItsOwnFileAndTheDebugBuildItsOwn() {
        // The debug file comes first in the list, as it may: the choice is made by the kind of build, not by position.
        val both = listOf("AimBuddy-v0.4.0-debug.apk", "AimBuddy-v0.4.0.apk")
        val json = feed(release("v0.4.0", assetNames = both))
        val app = ReleaseFeed.newest(json, version("0.3.0"), debugBuild = false)
        val debug = ReleaseFeed.newest(json, version("0.3.0"), debugBuild = true)
        assertEquals("https://github.com/1337Xcode/AimBuddy/releases/download/v0.4.0/AimBuddy-v0.4.0.apk", app!!.apkUrl)
        assertEquals("https://github.com/1337Xcode/AimBuddy/releases/download/v0.4.0/AimBuddy-v0.4.0-debug.apk", debug!!.apkUrl)
    }

    @Test
    fun neverOffersTheAppADebugFileOrTheDebugBuildTheAppsFile() {
        val onlyDebug = feed(release("v0.4.0", assetNames = listOf("AimBuddy-v0.4.0-debug.apk")))
        val onlyApp = feed(release("v0.4.0", assetNames = listOf("AimBuddy-v0.4.0.apk")))
        assertNull(ReleaseFeed.newest(onlyDebug, version("0.3.0"), debugBuild = false))
        assertNull(ReleaseFeed.newest(onlyApp, version("0.3.0"), debugBuild = true))
    }

    @Test
    fun fallsBackToAnOlderReleaseThatHasTheRightFile() {
        // The newest release has no debug file, so the debug build is offered the newest one that does.
        val json = feed(
            release("v0.5.0", assetNames = listOf("AimBuddy-v0.5.0.apk")),
            release("v0.4.0", assetNames = listOf("AimBuddy-v0.4.0.apk", "AimBuddy-v0.4.0-debug.apk")),
        )
        assertEquals("0.4.0", ReleaseFeed.newest(json, version("0.3.0"), debugBuild = true)!!.version)
        assertEquals("0.5.0", ReleaseFeed.newest(json, version("0.3.0"), debugBuild = false)!!.version)
    }

    @Test
    fun refusesAnswersThatAreNotAListOfReleases() {
        for (text in listOf("", "{}", "Not Found", "{\"message\": \"API rate limit exceeded\"}")) {
            try {
                forTheApp(text, "0.3.0")
                fail("accepted \"$text\"")
            } catch (expected: JSONException) {
                // the caller reports that GitHub could not be read
            }
        }
    }
}
