package com.aimbuddy.update

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import com.aimbuddy.BuildConfig
import com.aimbuddy.Http
import java.io.File
import java.io.IOException
import java.security.MessageDigest

private const val TAG = "AppUpdater"

/**
 * Finds a newer AimBuddy on GitHub, downloads it, and hands it to the system's installer.
 *
 * An update is only ever installed after three checks: the file has the SHA-256 GitHub lists for it,
 * it is an AimBuddy newer than this one, and it is signed by the same developer. The system's
 * installer makes the last check again, so a file from anyone else cannot replace this app.
 */
class AppUpdater(
    private val context: Context,
    private val releasesUrl: String,
    private val allowInsecure: Boolean,
) {

    /** Asks GitHub for the project's releases and returns the update, or null when this is the newest. */
    fun findUpdate(): UpdateInfo? {
        val current = AppVersion.parse(BuildConfig.AIMBUDDY_VERSION) ?: return null
        val json = Http.getText(releasesUrl, accept = "application/vnd.github+json", allowInsecure = allowInsecure)
        return ReleaseFeed.newest(json, current, BuildConfig.DEBUG)
    }

    /**
     * Downloads the update into the app's cache and checks it. [onProgress] gets 0 to 1, a percent at
     * a time. Throws [IOException] when the download fails and [SecurityException] when the file is
     * not an update this app may install.
     */
    fun download(info: UpdateInfo, onProgress: (Float) -> Unit): File {
        val folder = File(context.cacheDir, UPDATE_FOLDER)
        folder.deleteRecursively()   // an earlier download is of no use now
        if (!folder.mkdirs()) {
            throw IOException("Could not create ${folder.path}")
        }
        val file = File(folder, "AimBuddy-${info.version}.apk")
        var finished = false
        try {
            val digest = MessageDigest.getInstance("SHA-256")
            val connection = Http.open(info.apkUrl, allowInsecure = allowInsecure)
            try {
                val total = connection.contentLengthLong.takeIf { it > 0 } ?: info.sizeBytes.coerceAtLeast(1L)
                var received = 0L
                var reported = -1
                connection.inputStream.use { input ->
                    file.outputStream().use { output ->
                        val buffer = ByteArray(BUFFER_BYTES)
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) {
                                break
                            }
                            received += count
                            if (received > MAX_UPDATE_BYTES) {
                                throw IOException("The download is larger than any update")
                            }
                            digest.update(buffer, 0, count)
                            output.write(buffer, 0, count)
                            val percent = (received * 100 / total).toInt().coerceAtMost(100)
                            if (percent != reported) {
                                reported = percent
                                onProgress(percent / 100f)
                            }
                        }
                    }
                }
                if (info.sizeBytes > 0 && received != info.sizeBytes) {
                    throw IOException("The download is not the size GitHub lists")
                }
            } finally {
                connection.disconnect()
            }
            verify(info, file, digest.digest())
            finished = true
            return file
        } finally {
            if (!finished) {
                folder.deleteRecursively()
            }
        }
    }

    /** Refuses a file that is not the one GitHub lists, or not a newer AimBuddy from the same developer. */
    private fun verify(info: UpdateInfo, file: File, sha256: ByteArray) {
        val expected = info.sha256
        if (expected != null && toHex(sha256) != expected) {
            throw SecurityException("The download does not match the checksum GitHub lists")
        }
        val manager = context.packageManager
        val archive = manager.getPackageArchiveInfo(file.path, PackageManager.GET_SIGNING_CERTIFICATES)
            ?: throw SecurityException("The download is not an app")
        if (archive.packageName != context.packageName) {
            throw SecurityException("The download is another app: ${archive.packageName}")
        }
        val installed = manager.getPackageInfo(context.packageName, PackageManager.GET_SIGNING_CERTIFICATES)
        if (archive.longVersionCode <= installed.longVersionCode) {
            throw SecurityException("The download is not newer than this version")
        }
        val archiveSigners = signersOf(archive)
        if (archiveSigners.isEmpty() || archiveSigners != signersOf(installed)) {
            throw SecurityException("The download is not signed by the same developer")
        }
    }

    /** The SHA-256 of each certificate an app is signed with. */
    private fun signersOf(info: PackageInfo): Set<String> =
        info.signingInfo?.apkContentsSigners.orEmpty().map { toHex(MessageDigest.getInstance("SHA-256").digest(it.toByteArray())) }.toSet()

    /**
     * Gives the downloaded APK to the system's installer. The installer asks the player to confirm
     * and tells [InstallResultReceiver] how it went; when it succeeds the system replaces this app.
     */
    fun install(file: File) {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        params.setAppPackageName(context.packageName)
        params.setSize(file.length())
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            // An update of an app this app installed itself can go ahead without a question; otherwise the system asks.
            params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED)
        }
        val sessionId = installer.createSession(params)
        try {
            installer.openSession(sessionId).use { session ->
                file.inputStream().use { input ->
                    session.openWrite(file.name, 0, file.length()).use { output ->
                        input.copyTo(output)
                        session.fsync(output)
                    }
                }
                session.commit(resultIntentSender(sessionId))
            }
        } catch (e: Exception) {
            installer.abandonSession(sessionId)
            throw e
        }
        Log.i(TAG, "Handed ${file.name} to the installer")
    }

    /** What the installer sends its answer to. The system adds the details to it, so it has to be mutable. */
    private fun resultIntentSender(sessionId: Int) = PendingIntent.getBroadcast(
        context,
        sessionId,
        Intent(context, InstallResultReceiver::class.java),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
    ).intentSender

    private fun toHex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it) }

    private companion object {
        const val UPDATE_FOLDER = "updates"
        const val BUFFER_BYTES = 32 * 1024
        const val MAX_UPDATE_BYTES = 250L * 1024 * 1024
    }
}
