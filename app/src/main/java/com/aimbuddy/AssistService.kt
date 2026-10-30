package com.aimbuddy

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.hardware.display.DisplayManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import android.view.Display
import android.view.WindowManager
import android.widget.Toast
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import java.io.File
import java.io.IOException
import java.util.concurrent.Executors

/**
 * Runs the assist: screen capture, detection, the overlay, and the touch helper.
 *
 * Everything that has to be stopped together lives here, so there is exactly one place that does
 * it, and it does it however the assist ends: the Stop button, the notification, the overlay's
 * menu, the player holding the overlay icon, the system ending the screen capture, or the service
 * being destroyed. [shutdown] is written so that no step can prevent the ones after it.
 *
 * Because the service owns the overlay, the app's screen can be closed at any time while the
 * assist keeps running over the game, and reopening the app shows its state again (AssistState).
 */
class AssistService : Service(), NativeBridge.StreamerModeListener, NativeBridge.StopListener {

    private val mainHandler = Handler(Looper.getMainLooper())
    private val worker = Executors.newSingleThreadExecutor { runnable -> Thread(runnable, "aimbuddy-assist-start") }

    private var capture: CaptureSession? = null
    private var overlay: OverlayController? = null
    private var touchHelper: TouchHelper? = null
    private var thermalGovernor: ThermalGovernor? = null
    private var displayManager: DisplayManager? = null
    private var shown = DisplayShape(0, 0, 0)

    /** Between the start and the end of a run; false once [shutdown] has begun. */
    private var running = false

    /** Whether streamer mode is on, which decides what the notification's icon shows. */
    private var streamerMode = false

    /** Asks now and then whether the touch helper is still there once it has connected. */
    private val touchWatch = object : Runnable {
        override fun run() {
            if (!running) {
                return
            }
            checkTouchHelper()
            mainHandler.postDelayed(this, TOUCH_WATCH_MS)
        }
    }
    private val helperRestarts = RestartBudget(MAX_HELPER_RESTARTS, HELPER_RESTART_WINDOW_MS)

    private val displayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) {}
        override fun onDisplayRemoved(displayId: Int) {}
        override fun onDisplayChanged(displayId: Int) {
            if (displayId == Display.DEFAULT_DISPLAY && running) {
                onDisplayChanged()
            }
        }
    }

    private data class DisplayShape(val width: Int, val height: Int, val rotation: Int)

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> startRun(intent)
            ACTION_STOP -> {
                shutdown()
                stopSelf()   // also covers a stop that reaches a service which was not running
            }
            ACTION_MENU -> overlay?.toggleMenu()
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        shutdown()
        worker.shutdownNow()
        super.onDestroy()
    }

    // ---------------------------------------------------------------------------------------
    // Starting
    // ---------------------------------------------------------------------------------------

    private fun startRun(intent: Intent) {
        if (!running) {
            // Streamer mode set in an earlier run holds from the first moment, even for the notification's icon.
            streamerMode = savedStreamerMode()
        }
        // The notification must exist before anything else: a foreground service has seconds to show one.
        showForegroundNotification()
        if (running) {
            Log.i(TAG, "Start ignored: already running")
            return
        }
        running = true
        AssistState.set(AssistState.Phase.STARTING)
        AssistState.setTouch(AssistState.Touch.Off)

        val resultCode = intent.getIntExtra(EXTRA_RESULT_CODE, 0)
        val resultData = intent.getParcelableExtraCompat<Intent>(EXTRA_RESULT_DATA)
        if (resultData == null) {
            fail(AssistState.Failure.CAPTURE_DENIED, "Screen capture was not allowed")
            return
        }
        if (!Settings.canDrawOverlays(this)) {
            fail(AssistState.Failure.OVERLAY_DENIED, "The display over other apps permission is missing")
            return
        }

        NativeBridge.setStreamerModeListener(this)
        NativeBridge.setStopListener(this)

        // Loading the model takes a moment, so it happens off the main thread.
        worker.execute {
            val loaded = try {
                loadSettingsAndModel()
            } catch (e: Exception) {
                // An unreadable file or a damaged list has to end this start with a message, not the whole app.
                Log.e(TAG, "The model could not be set up: ${e.message}", e)
                false
            }
            mainHandler.post {
                if (!running) {
                    return@post
                }
                if (!loaded) {
                    fail(AssistState.Failure.MODEL_FAILED, "The detection model could not be loaded")
                    return@post
                }
                try {
                    beginCapture(resultCode, resultData)
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to start: ${e.message}", e)
                    fail(AssistState.Failure.START_FAILED, "Could not start: ${e.message}")
                }
            }
        }
    }

    private fun loadSettingsAndModel(): Boolean {
        NativeBridge.loadSettings(File(filesDir, SETTINGS_FILE).path)
        if (NativeBridge.usesColourDetection()) {
            return true   // a colour is looked for: there is no model to load
        }
        val catalog = ModelCatalog(this)
        catalog.tidy()
        val model = catalog.getActiveModel() ?: run {
            Log.e(TAG, "No model is installed")
            return false
        }
        val spec = ModelProfileSpec.readFor(model.paramPath)
        return NativeBridge.loadModel(
            model.paramPath,
            model.binPath,
            spec.roles,
            spec.endToEnd,
            spec.inputSize,
        )
    }

    private fun beginCapture(resultCode: Int, resultData: Intent) {
        val projectionManager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        val projection = projectionManager.getMediaProjection(resultCode, resultData)
            ?: throw IllegalStateException("The system did not give out a screen capture")

        val shape = readDisplayShape()
        val session = try {
            CaptureSession(projection, resources.displayMetrics.densityDpi) {
                mainHandler.post {
                    shutdown(AssistState.Failure.CAPTURE_ENDED)
                    // The player is in the game, not on the home screen that says why: say it over the game.
                    Toast.makeText(this, R.string.capture_ended_short, Toast.LENGTH_LONG).show()
                }
            }
        } catch (e: Exception) {
            projection.stop()   // nothing owns the projection yet, so it would otherwise stay shared
            throw e
        }
        capture = session
        val (captureWidth, captureHeight) = session.start(shape.width, shape.height)
        NativeBridge.configureDisplay(shape.width, shape.height, captureWidth, captureHeight, shape.rotation)
        shown = shape

        if (!NativeBridge.start()) {
            throw IllegalStateException("The detector would not start")
        }
        thermalGovernor = ThermalGovernor(this).also { it.start() }

        val controller = OverlayController(this) { mainHandler.post { shutdown() } }
        overlay = controller
        // Streamer mode set in an earlier run holds from the start, so the button is never on the screen even for a moment.
        val streamerWas = streamerMode
        streamerMode = NativeBridge.settingsSnapshot().lineSequence().any { it.trim() == "streamerMode=1" }
        controller.show(streamerMode)
        if (streamerMode != streamerWas) {
            showForegroundNotification()   // changed since the file was written
        }

        displayManager = (getSystemService(Context.DISPLAY_SERVICE) as DisplayManager).also {
            it.registerDisplayListener(displayListener, mainHandler)
        }

        AssistState.set(AssistState.Phase.RUNNING)
        startTouchHelper()
        mainHandler.postDelayed(touchWatch, TOUCH_WATCH_MS)
    }

    /**
     * Notices that the helper went away after it had connected: the system stopped it, the touchscreen's
     * device vanished, or the player took the screen back with the volume keys. Without this the Home
     * screen would go on saying the aim is assisted. Unless it was the player, the helper is started
     * again, a few times; then the screen says why it is not there.
     */
    private fun checkTouchHelper() {
        if (AssistState.state.value.touch !is AssistState.Touch.Connected) {
            return
        }
        val status = decodeLinkStatus(NativeBridge.touchStatus())
        if (status.connected) {
            return
        }
        // A helper that died without a word leaves no reason behind: it could not be reached.
        val reason = status.reason.takeIf { it != TouchHelper.Reason.NONE } ?: TouchHelper.Reason.UNREACHABLE
        Log.w(TAG, "The touch helper went away (reason $reason)")
        if (isWorthRestarting(reason) && helperRestarts.tryUse(SystemClock.elapsedRealtime())) {
            startTouchHelper()
        } else {
            AssistState.setTouch(AssistState.Touch.Unavailable(reason, TouchHelper.Reason.describe(reason)))
        }
    }

    /** Starts the touch helper in the background; the assist runs without it, drawing targets only. */
    private fun startTouchHelper() {
        AssistState.setTouch(AssistState.Touch.Starting)
        val helper = touchHelper ?: TouchHelper(this).also { touchHelper = it }
        worker.execute {
            val outcome = try {
                helper.start(NativeBridge.touchBackend())
            } catch (e: Exception) {
                // Whatever went wrong while starting the helper, the assist goes on without it: it draws targets only.
                Log.e(TAG, "The touch helper could not be started: ${e.message}", e)
                TouchHelper.Outcome.Failed(TouchHelper.Reason.UNREACHABLE, e.message ?: e.javaClass.simpleName)
            }
            mainHandler.post {
                if (!running) {
                    helper.stop()
                    return@post
                }
                when (outcome) {
                    is TouchHelper.Outcome.Connected -> AssistState.setTouch(AssistState.Touch.Connected(outcome.route))
                    is TouchHelper.Outcome.Failed -> {
                        Log.w(TAG, "Touch helper unavailable: ${outcome.detail}")
                        AssistState.setTouch(AssistState.Touch.Unavailable(outcome.reason, outcome.detail))
                    }
                }
            }
        }
    }

    private fun fail(failure: AssistState.Failure, log: String) {
        Log.w(TAG, log)
        shutdown(failure)
    }

    // ---------------------------------------------------------------------------------------
    // Display changes
    // ---------------------------------------------------------------------------------------

    private fun onDisplayChanged() {
        val shape = readDisplayShape()
        if (shape == shown) {
            return
        }
        Log.i(TAG, "Display changed: $shown -> $shape")
        try {
            val sizeChanged = shape.width != shown.width || shape.height != shown.height
            val session = capture
            // A new size needs a new mirror; the same size with a new rotation only changes the touch mapping.
            val (captureWidth, captureHeight) =
                if (sizeChanged && session != null) session.resize(shape.width, shape.height) else session?.imageSize ?: (0 to 0)
            NativeBridge.configureDisplay(shape.width, shape.height, captureWidth, captureHeight, shape.rotation)
            shown = shape
            overlay?.keepIconOnScreen()
        } catch (e: Exception) {
            Log.e(TAG, "Could not follow the display change: ${e.message}", e)
        }
    }

    private fun realDisplay(): Display =
        (getSystemService(Context.DISPLAY_SERVICE) as DisplayManager).getDisplay(Display.DEFAULT_DISPLAY)

    /** The whole display in its current orientation: system bars and cutouts included, as the capture sees it. */
    private fun readDisplayShape(): DisplayShape {
        val bounds = (getSystemService(Context.WINDOW_SERVICE) as WindowManager).maximumWindowMetrics.bounds
        return DisplayShape(bounds.width(), bounds.height(), realDisplay().rotation)
    }

    // ---------------------------------------------------------------------------------------
    // Stopping
    // ---------------------------------------------------------------------------------------

    /**
     * Ends the run. The order matters: first the aim loop (its finger comes off and the touchscreen
     * goes back to the system), then the helper, then the overlay and the capture. Each step is
     * guarded so a failure in one cannot leave the rest undone.
     */
    private fun shutdown(failure: AssistState.Failure? = null) {
        if (!running && AssistState.state.value.phase == AssistState.Phase.IDLE) {
            return
        }
        running = false
        mainHandler.removeCallbacks(touchWatch)
        AssistState.set(AssistState.Phase.STOPPING)

        guarded("native stop") { NativeBridge.stop() }
        guarded("thermal governor") { thermalGovernor?.stop() }
        thermalGovernor = null
        guarded("touch helper") { touchHelper?.stop() }
        touchHelper = null
        guarded("overlay") { overlay?.hide() }
        overlay = null
        guarded("display listener") { displayManager?.unregisterDisplayListener(displayListener) }
        displayManager = null
        guarded("capture") { capture?.stop() }
        capture = null
        guarded("native shutdown") { NativeBridge.shutdown() }
        NativeBridge.setStreamerModeListener(null)
        NativeBridge.setStopListener(null)

        AssistState.setTouch(AssistState.Touch.Off)
        AssistState.set(AssistState.Phase.IDLE, failure)
        leaveForeground()
        Log.i(TAG, "Stopped" + (failure?.let { " ($it)" } ?: ""))
    }

    /** Whether the settings file says streamer mode is on. Read straight from the file: this runs before the settings are loaded. */
    private fun savedStreamerMode(): Boolean = try {
        File(filesDir, SETTINGS_FILE).useLines { lines -> lines.any { it.trim() == "streamerMode=1" } }
    } catch (e: IOException) {
        false   // no file yet: the defaults hold
    }

    /** Removes the notification and ends the service. */
    private fun leaveForeground() {
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private inline fun guarded(what: String, action: () -> Unit) {
        try {
            action()
        } catch (e: Throwable) {
            Log.e(TAG, "Stopping $what failed: ${e.message}", e)
        }
    }

    // ---------------------------------------------------------------------------------------
    // Calls from native code
    // ---------------------------------------------------------------------------------------

    override fun onStreamerModeChanged(enabled: Boolean) {
        mainHandler.post {
            streamerMode = enabled
            overlay?.setStreamerMode(enabled)
            if (running) {
                showForegroundNotification()
            }
        }
    }

    override fun onStopRequested() {
        mainHandler.post { shutdown() }
    }

    // ---------------------------------------------------------------------------------------
    // Notification
    // ---------------------------------------------------------------------------------------

    /**
     * Shows the notification that keeps the assist alive and has the Menu and Stop buttons. In streamer
     * mode its small icon is empty: the system never lets a foreground service's notification out of the
     * status bar, so what shows there must be nothing, or a recording that includes the status bar would show AimBuddy.
     */
    private fun showForegroundNotification() {
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, getString(R.string.notification_channel), NotificationManager.IMPORTANCE_LOW)
        )
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        val stop = PendingIntent.getService(
            this, 1, Intent(this, AssistService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE
        )
        val menu = PendingIntent.getService(
            this, 2, Intent(this, AssistService::class.java).setAction(ACTION_MENU), PendingIntent.FLAG_IMMUTABLE
        )
        val notification: Notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(getString(R.string.notification_text))
            .setSmallIcon(if (streamerMode) R.drawable.ic_stat_blank else R.drawable.ic_stat_aim)
            .setOngoing(true)
            .setContentIntent(open)
            .addAction(0, getString(R.string.notification_menu), menu)
            .addAction(0, getString(R.string.notification_stop), stop)
            .build()
        ServiceCompat.startForeground(this, NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
    }

    companion object {
        private const val TAG = "AssistService"
        private const val CHANNEL_ID = "aimbuddy_assist"
        private const val NOTIFICATION_ID = 12345
        private const val SETTINGS_FILE = "settings.cfg"

        /** How often the touch helper is asked for, and how many times it is started again before the player is told. */
        private const val TOUCH_WATCH_MS = 500L
        private const val MAX_HELPER_RESTARTS = 3
        private const val HELPER_RESTART_WINDOW_MS = 60_000L

        const val ACTION_START = "com.aimbuddy.action.START"
        const val ACTION_STOP = "com.aimbuddy.action.STOP"
        const val ACTION_MENU = "com.aimbuddy.action.MENU"
        private const val EXTRA_RESULT_CODE = "resultCode"
        private const val EXTRA_RESULT_DATA = "resultData"

        /** Starts the assist with the result of the screen capture permission dialog. */
        fun start(context: Context, resultCode: Int, resultData: Intent) {
            val intent = Intent(context, AssistService::class.java)
                .setAction(ACTION_START)
                .putExtra(EXTRA_RESULT_CODE, resultCode)
                .putExtra(EXTRA_RESULT_DATA, resultData)
            context.startForegroundService(intent)
        }

        /** Asks a running assist to stop. */
        fun stop(context: Context) {
            context.startService(Intent(context, AssistService::class.java).setAction(ACTION_STOP))
        }
    }
}

/** Reads a parcelable extra without the deprecated untyped call on newer Android versions. */
private inline fun <reified T : android.os.Parcelable> Intent.getParcelableExtraCompat(name: String): T? =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        getParcelableExtra(name, T::class.java)
    } else {
        @Suppress("DEPRECATION")
        getParcelableExtra(name)
    }
