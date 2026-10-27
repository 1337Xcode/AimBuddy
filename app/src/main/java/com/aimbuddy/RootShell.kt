package com.aimbuddy

import android.util.Log
import java.util.concurrent.TimeUnit

/**
 * Talking to the superuser binary, for phones that have root.
 *
 * Root is only used to start the touch helper program. Nothing here changes system settings or
 * file permissions: the helper opens the devices it needs itself.
 */
object RootShell {
    private const val TAG = "RootShell"

    @Volatile
    private var granted = false

    /** True once the player has granted root to this app in this run. */
    fun isGranted(): Boolean = granted

    /**
     * Asks the superuser binary to run `id` and checks that it answers as root. The first call may
     * show the player a permission prompt, so it waits up to `timeoutSeconds` for an answer.
     */
    fun requestRoot(timeoutSeconds: Long): Boolean {
        if (granted) {
            return true
        }
        return try {
            val process = ProcessBuilder("su", "-c", "id").redirectErrorStream(true).start()
            if (!process.waitFor(timeoutSeconds, TimeUnit.SECONDS)) {
                process.destroyForcibly()
                Log.w(TAG, "Root request timed out after ${timeoutSeconds}s")
                return false
            }
            val output = process.inputStream.bufferedReader().use { it.readText() }
            granted = process.exitValue() == 0 && output.contains("uid=0")
            Log.i(TAG, "Root ${if (granted) "granted" else "refused"}")
            granted
        } catch (e: Exception) {
            Log.i(TAG, "No superuser binary: ${e.message}")
            false
        }
    }

    /** Starts `command` as root, with its output merged into one stream. */
    fun start(command: List<String>): Process {
        val line = command.joinToString(" ") { quote(it) }
        return ProcessBuilder("su", "-c", "exec $line").redirectErrorStream(true).start()
    }

    private fun quote(word: String): String = "'" + word.replace("'", "'\\''") + "'"
}
