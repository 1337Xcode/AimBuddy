package com.aimbuddy

import android.content.Context
import android.util.Log
import java.io.BufferedReader
import java.io.DataOutputStream
import java.io.InputStreamReader
import java.util.concurrent.TimeUnit

/**
 * RootUtils - Root permission management
 * 
 * Provides helper methods to check and request root access.
 * Required for uinput device creation and input device permission setup.
 */
object RootUtils {
    private const val TAG = "RootUtils"

    /**
     * Check if root access is available and grant it if needed
     */
    fun ensureRoot(context: Context? = null, timeoutSeconds: Long = 90): Boolean {
        return try {
            Log.i(TAG, "Checking root access...")
            
            // Execute 'su -c id' to verify root
            val process = ProcessBuilder("su", "-c", "id")
                .redirectErrorStream(true)
                .start()

            val finished = process.waitFor(timeoutSeconds, TimeUnit.SECONDS)
            if (!finished) {
                process.destroy()
                process.destroyForcibly()
                Log.w(TAG, "Root check timed out after ${timeoutSeconds}s (MFA approval may still be pending)")
                return false
            }

            val output = process.inputStream.bufferedReader().use { it.readText() }
            val exitCode = process.exitValue()
            
            Log.i(TAG, "su exit=$exitCode output=$output")
            
            if (exitCode == 0 && output.contains("uid=0")) {
                Log.i(TAG, "Root access granted")
                // Set permissions on uinput device
                fixUinputPermissions()
                return true
            }
            
            Log.e(TAG, "Root check failed: exitCode=$exitCode")
            false
        } catch (e: Exception) {
            Log.e(TAG, "Root check error: ${e.message}", e)
            false
        }
    }

    /**
     * Check if root is available without prompting
     */
    fun isRootAvailable(): Boolean {
        return try {
            val process = Runtime.getRuntime().exec("which su")
            val exitCode = process.waitFor()
            exitCode == 0
        } catch (e: Exception) {
            false
        }
    }

    /**
     * Fix /dev/uinput and /dev/input permissions for touch injection
     */
    private fun fixUinputPermissions(): Boolean {
        return try {
            Log.i(TAG, "Setting device permissions for touch injection...")
            
            val process = Runtime.getRuntime().exec("su")
            val os = DataOutputStream(process.outputStream)
            
            // Set read/write permissions on uinput
            os.writeBytes("chmod 666 /dev/uinput\n")
            
            // CRITICAL: Fix permissions on ALL input devices for device grab
            // Use a for loop since glob (*) may not work on all Android shells
            os.writeBytes("for f in /dev/input/event*; do chmod 666 \$f; done\n")
            
            // Also try direct chmod in case the loop fails
            os.writeBytes("chmod 666 /dev/input/event0 2>/dev/null\n")
            os.writeBytes("chmod 666 /dev/input/event1 2>/dev/null\n")
            os.writeBytes("chmod 666 /dev/input/event2 2>/dev/null\n")
            os.writeBytes("chmod 666 /dev/input/event3 2>/dev/null\n")
            os.writeBytes("chmod 666 /dev/input/event4 2>/dev/null\n")
            os.writeBytes("chmod 666 /dev/input/event5 2>/dev/null\n")
            os.writeBytes("chmod 666 /dev/input/event6 2>/dev/null\n")
            os.writeBytes("chmod 666 /dev/input/event7 2>/dev/null\n")
            os.writeBytes("chmod 666 /dev/input/event8 2>/dev/null\n")
            os.writeBytes("chmod 666 /dev/input/event9 2>/dev/null\n")
            
            // Disable SELinux (often needed for input device access)
            os.writeBytes("setenforce 0 2>/dev/null\n")
            
            os.writeBytes("exit\n")
            os.flush()
            
            val exitCode = process.waitFor()
            Log.i(TAG, "chmod devices: exitCode=$exitCode")
            
            exitCode == 0
        } catch (e: Exception) {
            Log.e(TAG, "Failed to fix device permissions: ${e.message}", e)
            false
        }
    }

    /**
     * Execute a root command
     */
    fun executeRootCommand(command: String): Pair<Boolean, String> {
        return try {
            val process = Runtime.getRuntime().exec("su")
            val os = DataOutputStream(process.outputStream)
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            
            os.writeBytes("$command\n")
            os.writeBytes("exit\n")
            os.flush()
            
            val output = StringBuilder()
            var line: String?
            while (reader.readLine().also { line = it } != null) {
                output.append(line).append("\n")
            }
            
            val exitCode = process.waitFor()
            Pair(exitCode == 0, output.toString())
        } catch (e: Exception) {
            Pair(false, e.message ?: "Unknown error")
        }
    }
}
