package com.aimbuddy

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import android.util.Log
import rikka.shizuku.Shizuku
import java.io.File
import java.security.SecureRandom
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/**
 * Starts the touch helper, the small program that reads the touchscreen and adds the assist's
 * finger to it, with whatever privilege the phone offers, and connects the app to it.
 *
 * Two routes lead to the privilege needed: Shizuku (the shell user, no root) and root. Automatic
 * prefers Shizuku. The helper stops by itself when the app disconnects, whatever the reason, and
 * [stop] also ends its process.
 *
 * The helper picks its own port and prints it, so the app never guesses a port that another program
 * might take in the meantime, and never sends its secret to a program that is not the helper.
 */
class TouchHelper(private val context: Context) {

    /** Why the helper could not be used. The numbers match touch::protocol::Reason in the native code. */
    object Reason {
        const val NONE = 0
        const val NO_TOUCHSCREEN = 1
        const val TOUCHSCREEN_DENIED = 2
        const val VIRTUAL_DEVICE_DENIED = 3
        const val GRAB_FAILED = 4
        const val DEVICE_LOST = 5
        const val HEARTBEAT_LOST = 6
        const val ESCAPE_KEYS = 7
        const val BAD_CLIENT = 8
        const val REQUESTED = 9
        const val UNREACHABLE = 10
        /** No Shizuku and no root: nothing to start the helper with. */
        const val NO_PRIVILEGE = 100

        fun describe(reason: Int): String = when (reason) {
            NONE -> "ready"
            NO_TOUCHSCREEN -> "No touchscreen was found."
            TOUCHSCREEN_DENIED -> "The touchscreen could not be opened. Shizuku or root may lack permission on this phone."
            VIRTUAL_DEVICE_DENIED -> "A virtual touchscreen could not be created."
            GRAB_FAILED -> "Another program holds the touchscreen."
            DEVICE_LOST -> "The touchscreen went away."
            HEARTBEAT_LOST -> "The touch helper stopped answering."
            ESCAPE_KEYS -> "Stopped with the volume keys."
            NO_PRIVILEGE -> "Aim assist needs Shizuku or root. Start Shizuku, or grant root, and try again."
            else -> "The touch helper could not be reached."
        }
    }

    /** Which route started the helper. */
    enum class Route { SHIZUKU, ROOT }

    /** How starting the helper went. */
    sealed interface Outcome {
        data class Connected(val route: Route) : Outcome
        data class Failed(val reason: Int, val detail: String) : Outcome
    }

    private val random = SecureRandom()
    private var hostArgs: Shizuku.UserServiceArgs? = null
    private var hostConnection: ServiceConnection? = null
    private var host: IHelperHost? = null
    private var rootHelper: HelperProcess? = null

    /** True when Shizuku is running and has been allowed for this app. */
    fun isShizukuReady(): Boolean = try {
        Shizuku.pingBinder() && Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
    } catch (_: Throwable) {
        false
    }

    /**
     * Starts the helper and connects to it. `preferred` is the player's choice: 0 automatic, 1
     * Shizuku, 2 root. Blocks for up to about half a minute (root may ask the player), so call it
     * off the main thread.
     */
    fun start(preferred: Int): Outcome {
        stop()
        val routes = when (preferred) {
            1 -> listOf(Route.SHIZUKU)
            2 -> listOf(Route.ROOT)
            else -> listOf(Route.SHIZUKU, Route.ROOT)
        }
        var failure: Outcome.Failed = Outcome.Failed(Reason.NO_PRIVILEGE, Reason.describe(Reason.NO_PRIVILEGE))
        for (route in routes) {
            // The service interrupts this thread when the assist is stopped: no other route is worth trying then.
            if (Thread.currentThread().isInterrupted) {
                Log.i(TAG, "Starting the touch helper was cancelled")
                return Outcome.Failed(Reason.UNREACHABLE, "Starting the touch helper was cancelled.")
            }
            val available = when (route) {
                Route.SHIZUKU -> isShizukuReady()
                Route.ROOT -> RootShell.requestRoot(ROOT_PROMPT_SECONDS)
            }
            if (!available) {
                continue
            }
            val outcome = tryRoute(route)
            if (outcome is Outcome.Connected) {
                return outcome
            }
            failure = outcome as Outcome.Failed
        }
        return failure
    }

    /** Ends the helper: the app disconnects (the helper then gives the touchscreen back) and its process is stopped. */
    fun stop() {
        NativeBridge.touchDisconnect()
        try {
            host?.stop()
        } catch (e: Exception) {
            Log.w(TAG, "Could not stop the helper through Shizuku: ${e.message}")
        }
        val args = hostArgs
        val connection = hostConnection
        if (args != null && connection != null) {
            try {
                Shizuku.unbindUserService(args, connection, true)
            } catch (e: Throwable) {
                Log.w(TAG, "Could not unbind the Shizuku service: ${e.message}")
            }
        }
        host = null
        hostArgs = null
        hostConnection = null
        rootHelper?.stop()
        rootHelper = null
    }

    private fun tryRoute(route: Route): Outcome {
        val helper = File(context.applicationInfo.nativeLibraryDir, HELPER_FILE)
        if (!helper.exists()) {
            return Outcome.Failed(Reason.UNREACHABLE, "The touch helper is missing from this install (${helper.path}).")
        }
        val token = newToken()
        val command = buildList {
            add(helper.path)
            add("--port")
            add("0")   // let the helper pick: it says which port it got
            add("--token")
            add(token)
            if (BuildConfig.DEBUG) add("--verbose")
        }

        val port = when (route) {
            Route.SHIZUKU -> launchWithShizuku(command)
            Route.ROOT -> launchWithRoot(command)
        }
        if (port <= 0) {
            val log = helperLog()
            Log.w(TAG, "Touch helper through $route did not start: $log")
            stop()
            return Outcome.Failed(Reason.UNREACHABLE, Reason.describe(Reason.UNREACHABLE) + if (log.isNotBlank()) "\n$log" else "")
        }

        val reason = NativeBridge.touchConnect(port, token)
        if (reason == Reason.NONE) {
            Log.i(TAG, "Touch helper connected through $route")
            return Outcome.Connected(route)
        }
        val log = helperLog()
        Log.w(TAG, "Touch helper through $route failed with reason $reason: $log")
        stop()
        return Outcome.Failed(reason, Reason.describe(reason) + if (log.isNotBlank()) "\n$log" else "")
    }

    /** Starts the helper through Shizuku. Returns the port it listens on, or a value of 0 or less. */
    private fun launchWithShizuku(command: List<String>): Int {
        val latch = CountDownLatch(1)
        var connected: IHelperHost? = null
        val connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName, binder: IBinder) {
                connected = IHelperHost.Stub.asInterface(binder)
                latch.countDown()
            }

            override fun onServiceDisconnected(name: ComponentName) {
                connected = null
            }
        }
        // A tag of its own for every start: Shizuku keeps one record for each tag, and binding to the record of a
        // service that is still being taken down (the one this start follows on a stop, or on a helper that died)
        // hangs until the bind times out.
        val args = Shizuku.UserServiceArgs(ComponentName(context.packageName, HelperHost::class.java.name))
            .daemon(false)
            .tag("touch-${serviceStarts.incrementAndGet()}")
            .processNameSuffix("touch")
            .debuggable(BuildConfig.DEBUG)
            .version(BuildConfig.AIMBUDDY_VERSION.hashCode())
        try {
            Shizuku.bindUserService(args, connection)
        } catch (e: Throwable) {
            Log.w(TAG, "Shizuku refused to start the helper service: ${e.message}")
            return 0
        }
        hostArgs = args
        hostConnection = connection
        if (!awaitUnlessInterrupted(latch, SHIZUKU_BIND_SECONDS)) {
            val why = if (Thread.currentThread().isInterrupted) "the assist was stopped first" else "it took too long"
            Log.w(TAG, "Shizuku did not start the helper service: $why")
            return 0
        }
        val service = connected ?: return 0
        host = service
        return try {
            service.start(command.toTypedArray(), HELPER_START_MS)
        } catch (e: Exception) {
            Log.w(TAG, "Shizuku call failed: ${e.message}")
            0
        }
    }

    /** Starts the helper as root. Returns the port it listens on, or a value of 0 or less. */
    private fun launchWithRoot(command: List<String>): Int {
        return try {
            val started = HelperProcess(RootShell.start(command))
            rootHelper = started
            started.awaitPort(HELPER_START_MS.toLong())
        } catch (e: Exception) {
            Log.w(TAG, "Could not start the helper as root: ${e.message}")
            0
        }
    }

    /** What the helper has printed, for the player to read when something fails. */
    private fun helperLog(): String {
        val fromShizuku = try {
            host?.log().orEmpty()
        } catch (_: Exception) {
            ""
        }
        return fromShizuku.ifBlank { rootHelper?.log().orEmpty() }
    }

    /** A secret only this app and the helper it started know: 64 random bits as 16 hexadecimal digits. */
    private fun newToken(): String {
        val bytes = ByteArray(8)
        random.nextBytes(bytes)
        return bytes.joinToString("") { "%02x".format(it) }
    }

    private companion object {
        const val TAG = "TouchHelper"

        /** Counts the services started through Shizuku in this process, to give each its own tag. */
        val serviceStarts = AtomicInteger()

        const val HELPER_FILE = "libaimtouch.so"
        const val ROOT_PROMPT_SECONDS = 30L
        const val SHIZUKU_BIND_SECONDS = 10L

        // Starting the helper takes a few milliseconds; this only matters when something is wrong.
        const val HELPER_START_MS = 5000
    }
}

/**
 * Waits up to [timeoutSeconds] for [latch] to open and says whether it did. A thread that is
 * interrupted while it waits is being told to stop (the service does that when the assist ends), so
 * the answer is false and the interrupt stays set for the caller to see: an exception thrown here
 * would end a background thread, and with it the whole app.
 */
internal fun awaitUnlessInterrupted(latch: CountDownLatch, timeoutSeconds: Long): Boolean =
    try {
        latch.await(timeoutSeconds, TimeUnit.SECONDS)
    } catch (_: InterruptedException) {
        // Throwing the exception cleared the interrupt; set it again so the caller can see it.
        Thread.currentThread().interrupt()
        false
    }
