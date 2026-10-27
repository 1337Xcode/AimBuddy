package com.aimbuddy.update

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/** A newer version of the app that the project has published. */
data class UpdateInfo(
    val version: String,
    val apkUrl: String,
    val sizeBytes: Long,
    /** The SHA-256 GitHub reports for the file, when it reports one. */
    val sha256: String?,
)

/** Reads GitHub's list of the project's releases and picks the update, if there is one. */
object ReleaseFeed {

    /** The release file is named `AimBuddy-v<version>.apk` (see the release workflow). */
    private val APK_NAME = Regex("AimBuddy.*\\.apk")

    private const val DIGEST_PREFIX = "sha256:"

    /**
     * Returns the newest release that is newer than [current] and has an APK, or null when the app is
     * up to date. Pre-releases are offered only to someone who already runs one, so a stable
     * install is never moved onto a beta. Drafts are never offered.
     *
     * @throws JSONException when [json] is not the list GitHub's releases endpoint returns.
     */
    fun newest(json: String, current: AppVersion): UpdateInfo? {
        val releases = JSONArray(json)
        var bestVersion: AppVersion = current
        var best: UpdateInfo? = null
        for (index in 0 until releases.length()) {
            val release = releases.optJSONObject(index) ?: continue
            if (release.optBoolean("draft") || (release.optBoolean("prerelease") && !current.isPreRelease)) {
                continue
            }
            val version = AppVersion.parse(release.optString("tag_name")) ?: continue
            if (version <= bestVersion) {
                continue
            }
            val info = infoFor(release, version) ?: continue
            bestVersion = version
            best = info
        }
        return best
    }

    /** The APK of a release, or null when it has none. */
    private fun infoFor(release: JSONObject, version: AppVersion): UpdateInfo? {
        val assets = release.optJSONArray("assets") ?: return null
        for (index in 0 until assets.length()) {
            val asset = assets.optJSONObject(index) ?: continue
            if (!APK_NAME.matches(asset.optString("name"))) {
                continue
            }
            val url = asset.optString("browser_download_url")
            if (url.isEmpty()) {
                continue
            }
            val digest = asset.optString("digest")
            return UpdateInfo(
                version = version.toString(),
                apkUrl = url,
                sizeBytes = asset.optLong("size"),
                sha256 = digest.takeIf { it.startsWith(DIGEST_PREFIX) }?.removePrefix(DIGEST_PREFIX)?.lowercase(),
            )
        }
        return null
    }
}
