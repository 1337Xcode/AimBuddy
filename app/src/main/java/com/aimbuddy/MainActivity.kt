package com.aimbuddy

import android.Manifest
import android.app.Activity
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.Looper
import android.provider.Settings
import android.text.TextUtils
import android.util.DisplayMetrics
import android.util.Log
import android.view.Gravity
import android.view.MotionEvent
import android.view.Surface
import android.view.View
import android.view.WindowManager
import android.view.accessibility.AccessibilityManager
import android.widget.ImageView
import android.widget.Toast
import android.graphics.drawable.Drawable
import android.graphics.drawable.PictureDrawable
import com.caverock.androidsvg.SVG
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.clickable
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/**
 * MainActivity - ESP overlay control interface
 *
 * Handles permissions, MediaProjection setup, and overlay lifecycle.
 * Provides START/STOP buttons for ESP functionality.
 */
class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "ESP_MainActivity"
        private const val REQUEST_MEDIA_PROJECTION = 1001
        private const val REQUEST_OVERLAY_PERMISSION = 1002

        // Capture resolution (720p for optimal SD888 performance)
        private const val CAPTURE_WIDTH = 1280
        private const val CAPTURE_HEIGHT = 720
        private const val OSS_GITHUB_URL = "https://github.com/1337XCode/AimBuddy"
        private const val CREATOR_NAME = "1337XCode"
        private const val CREATOR_URL = "https://www.darshanchheda.com"
        private const val PREFS_NAME = "aimbuddy_prefs"
        private const val PREF_OSS_NOTICE_SHOWN = "oss_notice_shown"

        init {
            System.loadLibrary("esp_native")
        }
    }

    // UI state
    private var isRunningState by mutableStateOf(false)
    private var statusTextState by mutableStateOf("Status: Model Loading")

    // Overlay components
    private var imguiOverlay: ImGuiGLSurface? = null
    private var windowManager: WindowManager? = null
    private var isOverlayVisible = false
    private val touchHandler = Handler(Looper.getMainLooper())
    private var touchPolling = false
    private val isStopping = AtomicBoolean(false)
    private val isStarting = AtomicBoolean(false)
    private val rootCheckInProgress = AtomicBoolean(false)
    private val rootAvailable = AtomicBoolean(false)

    // Floating menu icon overlay
    private var floatingIconView: ImageView? = null
    private var floatingIconParams: WindowManager.LayoutParams? = null
    private var iconDownRawX = 0f
    private var iconDownRawY = 0f
    private var iconStartX = 0
    private var iconStartY = 0
    private var iconMoved = false
    private var menuVisible = false

    // MediaProjection components
    private var mediaProjectionManager: MediaProjectionManager? = null
    private var mediaProjection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private val imageThread = HandlerThread("esp-image-reader").also { it.start() }
    private val imageHandler = Handler(imageThread.looper)

    // Display metrics
    private var screenWidth = 1080
    private var screenHeight = 2400
    private var screenDensity = 1

    // Rendering
    private val renderHandler = Handler(Looper.getMainLooper())

    // Native methods
    private external fun nativeInit(assetManager: android.content.res.AssetManager,
                                    screenWidth: Int, screenHeight: Int): Boolean
    private external fun nativeStart()
    private external fun nativeStop()
    private external fun nativeShutdown()
    private external fun nativeIsRunning(): Boolean
    private external fun nativeInitAimbot(): Boolean

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Force landscape orientation
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    ControlScreen(
                        isRunning = isRunningState,
                        statusText = statusTextState,
                        onStart = { onStartClicked() },
                        onStop = { onStopClicked() }
                    )
                }
            }
        }
        
        // Enable immersive fullscreen mode (hide nav bar & status bar)
        enableImmersiveMode()

        Log.i(TAG, "onCreate")

        // Get display metrics
        val displayMetrics = DisplayMetrics()
        windowManager = getSystemService(Context.WINDOW_SERVICE) as WindowManager
        windowManager?.defaultDisplay?.getRealMetrics(displayMetrics)
        
        // Force Landscape dimensions for game overlay
        // If width < height, swap them
        if (displayMetrics.widthPixels < displayMetrics.heightPixels) {
            screenWidth = displayMetrics.heightPixels
            screenHeight = displayMetrics.widthPixels
        } else {
            screenWidth = displayMetrics.widthPixels
            screenHeight = displayMetrics.heightPixels
        }
        screenDensity = displayMetrics.densityDpi

        Log.i(TAG, "Screen: ${screenWidth}x${screenHeight}, density: $screenDensity")

        setStatus("Status: Model Loading")

        // Get MediaProjectionManager
        mediaProjectionManager = getSystemService(Context.MEDIA_PROJECTION_SERVICE)
                as MediaProjectionManager

        // Initialize native components FIRST
        if (!nativeInit(assets, screenWidth, screenHeight)) {
            Log.e(TAG, "Failed to initialize native components")
            showAppToast("Failed to initialize ESP. Check model files.", true)
            setStatus("Status: Init Failed")
        } else {
            setStatus("Status: Ready")
            ImGuiGLSurface.nativeSetRootAvailable(false)
            maybeShowOpenSourceDialogOnce()
            // Delay root check slightly so the activity is fully stable before
            // the Magisk prompt fires (avoids prompt appearing during transitions).
            Handler(Looper.getMainLooper()).postDelayed({
                if (!isFinishing && !isDestroyed) {
                    beginAsyncRootCheck(showDialogOnFailure = true)
                }
            }, 800)
        }
    }
    
    override fun onDestroy() {
        Log.i(TAG, "onDestroy")
        stopESP()
        imageThread.quitSafely()
        nativeShutdown()
        super.onDestroy()
    }
    
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            enableImmersiveMode()
        }
    }
    
    @Suppress("DEPRECATION")
    private fun enableImmersiveMode() {
        // Hide navigation bar and status bar for fullscreen
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        )
    }

    private fun onStartClicked() {
        Log.i(TAG, "Start button clicked")

        if (isRunningState || isStarting.get()) {
            Log.i(TAG, "Start ignored: already running or starting")
            return
        }

        if (isStopping.get()) {
            Log.i(TAG, "Start ignored: stop in progress")
            return
        }

        // Check overlay permission first — show a helpful explanation dialog
        if (!Settings.canDrawOverlays(this)) {
            Log.i(TAG, "Requesting overlay permission")
            AlertDialog.Builder(this)
                .setTitle("Overlay Permission Required")
                .setMessage("AimBuddy needs the 'Display over other apps' permission to draw the ESP overlay.\n\nTap 'Open Settings', find AimBuddy in the list and enable the permission, then come back and press Start.")
                .setPositiveButton("Open Settings") { _, _ ->
                    val intent = Intent(
                        Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:$packageName")
                    )
                    startActivityForResult(intent, REQUEST_OVERLAY_PERMISSION)
                }
                .setNegativeButton("Cancel", null)
                .setCancelable(true)
                .show()
            return
        }

        // Request MediaProjection permission
        Log.i(TAG, "Requesting MediaProjection")
        val captureIntent = mediaProjectionManager?.createScreenCaptureIntent()
            ?: run {
                Log.e(TAG, "MediaProjectionManager is null")
                showAppToast("Unable to start screen capture", true)
                return
            }

        startActivityForResult(
            captureIntent,
            REQUEST_MEDIA_PROJECTION
        )
    }

    private fun onStopClicked() {
        Log.i(TAG, "Stop button clicked")
        stopESP()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)

        when (requestCode) {
            REQUEST_OVERLAY_PERMISSION -> {
                if (Settings.canDrawOverlays(this)) {
                    Log.i(TAG, "Overlay permission granted")
                    onStartClicked()  // Retry start
                } else {
                    Log.w(TAG, "Overlay permission denied")
                    showAppToast("Overlay permission required", true)
                }
            }

            REQUEST_MEDIA_PROJECTION -> {
                if (resultCode == Activity.RESULT_OK && data != null) {
                    Log.i(TAG, "MediaProjection permission granted")
                    
                    // Start Foreground Service FIRST (Required for MediaProjection on Android 10+)
                    val serviceIntent = Intent(this, ScreenCaptureService::class.java)
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        startForegroundService(serviceIntent)
                    } else {
                        startService(serviceIntent)
                    }
                    
                    // Small delay to ensure service is promoted to foreground
                    // In a production app, use a bound service or callback, but a handler delay is often sufficient for this check
                    Handler(Looper.getMainLooper()).postDelayed({
                        try {
                            mediaProjection = mediaProjectionManager?.getMediaProjection(resultCode, data)
                            startESP()
                        } catch (e: Exception) {
                            Log.e(TAG, "Failed to create MediaProjection: ${e.message}")
                            showAppToast("Failed to start capture: ${e.message}", true)
                        }
                    }, 1000) // 1 second delay
                    
                } else {
                    Log.w(TAG, "MediaProjection permission denied")
                    showAppToast("Screen capture permission required", true)
                }
            }
        }
    }

    private fun startESP() {
        if (!isStarting.compareAndSet(false, true)) {
            Log.i(TAG, "startESP ignored: already starting")
            return
        }

        Log.i(TAG, "Starting ESP")
        setStatus("Status: Starting")
        try {
            if (mediaProjection == null) {
                showAppToast("Screen capture not initialized", true)
                setStatus("Status: Ready")
                return
            }

            if (!Settings.canDrawOverlays(this)) {
                showAppToast("Overlay permission required", true)
                setStatus("Status: Ready")
                return
            }

            setupScreenCapture()
            setupOverlay()
            nativeStart()

            updateButtonStates(true)
            showAppToast("ESP started", false)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start ESP: ${e.message}", e)
            showAppToast("Failed to start ESP: ${e.message}", true)
            stopESP()
        } finally {
            isStarting.set(false)
        }
    }

    private fun stopESP() {
        if (!isStopping.compareAndSet(false, true)) {
            return
        }
        Log.i(TAG, "Stopping ESP")
        setStatus("Status: Stopping")
        try {
            // Stop native processing
            if (nativeIsRunning()) {
                nativeStop()
            }

            // Cleanup screen capture
            imageReader?.setOnImageAvailableListener(null, null)
            virtualDisplay?.release()
            virtualDisplay = null

            imageReader?.close()
            imageReader = null

            mediaProjection?.stop()
            mediaProjection = null

            // Remove overlay
            removeOverlay()

            updateButtonStates(false)

            // Stop the foreground service
            stopService(Intent(this, ScreenCaptureService::class.java))
        } finally {
            isStopping.set(false)
        }
    }

    private fun setupScreenCapture() {
        Log.i(TAG, "Setting up screen capture at ${CAPTURE_WIDTH}x${CAPTURE_HEIGHT}")

        // Create ImageReader with HardwareBuffer support
        imageReader = ImageReader.newInstance(
            CAPTURE_WIDTH, CAPTURE_HEIGHT,
            PixelFormat.RGBA_8888,
            2  // 2-buffer depth for double buffering
        ).apply {
            setOnImageAvailableListener({ reader ->
                val image = reader.acquireLatestImage() ?: return@setOnImageAvailableListener
                try {
                    // Get HardwareBuffer and pass to native code
                    val hardwareBuffer = image.hardwareBuffer
                    if (hardwareBuffer != null) {
                        ScreenCaptureService.nativeOnFrame(hardwareBuffer, image.timestamp)
                        hardwareBuffer.close()
                    }
                } finally {
                    image.close()
                }
            }, imageHandler)
        }

        // Create VirtualDisplay
        virtualDisplay = mediaProjection?.createVirtualDisplay(
            "ESPCapture",
            CAPTURE_WIDTH, CAPTURE_HEIGHT, screenDensity,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            imageReader?.surface, null, null
        )

        Log.i(TAG, "Screen capture setup complete")
    }

    private fun setupOverlay() {
        Log.i(TAG, "Setting up overlay")

        if (!Settings.canDrawOverlays(this)) {
            throw IllegalStateException("Overlay permission is not granted")
        }

        // Create GLSurfaceView for ImGui rendering
        imguiOverlay = ImGuiGLSurface(this)

        // Overlay window parameters
        val layoutParams = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT,
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
            else
                WindowManager.LayoutParams.TYPE_PHONE,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL or
                    // Default to pass-through touch; enable only when menu needs input
                    WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or
                    WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
                    WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS or
                    WindowManager.LayoutParams.FLAG_FULLSCREEN or
                    WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            // Extend into display cutout areas (notch, pill, etc.)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }
        }

        windowManager?.addView(imguiOverlay, layoutParams)
        isOverlayVisible = true

        startTouchPolling()
        setupFloatingIcon()

        Log.i(TAG, "Overlay added")
    }

    private fun removeOverlay() {
        if (isOverlayVisible) {
            stopTouchPolling()
            imguiOverlay?.let {
                try {
                    windowManager?.removeView(it)
                } catch (ignored: IllegalArgumentException) {
                    Log.w(TAG, "Overlay view already removed")
                }
            }
            imguiOverlay = null
            removeFloatingIcon()
            isOverlayVisible = false
            menuVisible = false
            Log.i(TAG, "Overlay removed")
        }
    }

    private fun setupFloatingIcon() {
        if (floatingIconView != null) return
        val wm = windowManager ?: return

        val iconSizePx = (44 * resources.displayMetrics.density).toInt()
        val iconView = ImageView(this).apply {
            setImageDrawable(loadSvgDrawable("icons/settings.svg")
                ?: getDrawable(android.R.drawable.ic_menu_manage))
            setLayerType(View.LAYER_TYPE_SOFTWARE, null)
            scaleType = ImageView.ScaleType.CENTER_INSIDE
        }

        val params = WindowManager.LayoutParams(
            iconSizePx,
            iconSizePx,
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
            else
                WindowManager.LayoutParams.TYPE_PHONE,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                    WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or
                    WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 40
            y = 120
        }

        iconView.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    iconDownRawX = event.rawX
                    iconDownRawY = event.rawY
                    iconStartX = params.x
                    iconStartY = params.y
                    iconMoved = false
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = (event.rawX - iconDownRawX).toInt()
                    val dy = (event.rawY - iconDownRawY).toInt()
                    if (kotlin.math.abs(dx) > 6 || kotlin.math.abs(dy) > 6) {
                        iconMoved = true
                    }
                    params.x = iconStartX + dx
                    params.y = iconStartY + dy
                    wm.updateViewLayout(iconView, params)
                    
                    // Sync icon position to native code for menu positioning
                    ImGuiGLSurface.nativeSetIconPosition(params.x.toFloat(), params.y.toFloat())
                    true
                }
                MotionEvent.ACTION_UP -> {
                    if (!iconMoved) {
                        menuVisible = !menuVisible
                        ImGuiGLSurface.nativeSetMenuVisible(menuVisible)
                    }
                    true
                }
                else -> false
            }
        }

        wm.addView(iconView, params)
        floatingIconView = iconView
        floatingIconParams = params
        
        // Sync initial icon position to native code
        ImGuiGLSurface.nativeSetIconPosition(params.x.toFloat(), params.y.toFloat())
    }

    private fun loadSvgDrawable(assetPath: String): Drawable? {
        return try {
            val svg = assets.open(assetPath).use { SVG.getFromInputStream(it) }
            val picture = svg.renderToPicture()
            PictureDrawable(picture)
        } catch (e: Exception) {
            Log.w(TAG, "Failed to load SVG icon: ${e.message}")
            null
        }
    }

    private fun removeFloatingIcon() {
        val wm = windowManager ?: return
        floatingIconView?.let {
            try {
                wm.removeView(it)
            } catch (ignored: IllegalArgumentException) {
                Log.w(TAG, "Floating icon already removed")
            }
        }
        floatingIconView = null
        floatingIconParams = null
    }

    private fun startTouchPolling() {
        if (touchPolling) return
        touchPolling = true
        touchHandler.post(object : Runnable {
            override fun run() {
                if (!touchPolling) return
                val view = imguiOverlay ?: return
                val params = view.layoutParams as? WindowManager.LayoutParams ?: return

                val wantsCapture = ImGuiGLSurface.nativeWantsCapture()
                val isTouchable = (params.flags and WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE) == 0

                if (wantsCapture && !isTouchable) {
                    params.flags = params.flags and WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE.inv()
                    windowManager?.updateViewLayout(view, params)
                } else if (!wantsCapture && isTouchable) {
                    params.flags = params.flags or WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                    windowManager?.updateViewLayout(view, params)
                }

                touchHandler.postDelayed(this, 50)
            }
        })
    }

    private fun stopTouchPolling() {
        touchPolling = false
        touchHandler.removeCallbacksAndMessages(null)
    }

    private fun setStatus(status: String) {
        runOnUiThread {
            statusTextState = status
        }
    }

    private fun updateButtonStates(isRunning: Boolean) {
        runOnUiThread {
            if (isRunning) {
                isRunningState = true
                statusTextState = "Status: Running"
            } else {
                isRunningState = false
                statusTextState = "Status: Ready"
            }
        }
    }

    private fun maybeShowOpenSourceDialogOnce() {
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        if (prefs.getBoolean(PREF_OSS_NOTICE_SHOWN, false)) {
            return
        }

        AlertDialog.Builder(this)
            .setTitle("AimBuddy Notice")
            .setMessage("AimBuddy is free and open source. If you paid for this app, you were scammed.\n\nRepository: $OSS_GITHUB_URL")
            .setPositiveButton("Open GitHub") { _, _ ->
                prefs.edit().putBoolean(PREF_OSS_NOTICE_SHOWN, true).apply()
                openGithubUrl()
            }
            .setNegativeButton("Cancel") { _, _ ->
                prefs.edit().putBoolean(PREF_OSS_NOTICE_SHOWN, true).apply()
            }
            .setCancelable(false)
            .show()
    }

    private fun showAppToast(message: String, isError: Boolean) {
        val textView = android.widget.TextView(this).apply {
            text = message
            setPadding(26, 18, 26, 18)
            textSize = 13f
            maxLines = 4
            setTextColor(android.graphics.Color.WHITE)
            setBackgroundColor(if (isError) 0xCC8B1D1D.toInt() else 0xCC1B1F2A.toInt())
            gravity = Gravity.CENTER
        }
        Toast(this).apply {
            duration = Toast.LENGTH_LONG
            view = textView
            setGravity(Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL, 0, 120)
        }.show()
    }

    private fun openCreatorUrl() {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(CREATOR_URL))
        startActivity(intent)
    }

    private fun openGithubUrl() {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(OSS_GITHUB_URL))
        startActivity(intent)
    }

    @Composable
    private fun ControlScreen(
        isRunning: Boolean,
        statusText: String,
        onStart: () -> Unit,
        onStop: () -> Unit
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(24.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = "AimBuddy",
                style = MaterialTheme.typography.headlineMedium,
                color = MaterialTheme.colorScheme.onBackground
            )
            Text(
                text = "Real-time detection and aiming companion",
                modifier = Modifier.padding(top = 8.dp),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = statusText,
                modifier = Modifier.padding(top = 12.dp, bottom = 24.dp),
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center
            )

            Row(
                modifier = Modifier.padding(vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Button(
                    onClick = onStart,
                    enabled = !isRunning,
                    modifier = Modifier.width(170.dp)
                ) {
                    Text(if (isRunning) "Running" else "Start")
                }

                Button(
                    onClick = onStop,
                    enabled = isRunning,
                    modifier = Modifier.width(170.dp)
                ) {
                    Text("Stop")
                }
            }

            Text(
                text = "GitHub: $OSS_GITHUB_URL",
                modifier = Modifier
                    .padding(top = 16.dp)
                    .clickable { openGithubUrl() },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.tertiary
            )
            Text(
                text = "Created by $CREATOR_NAME",
                modifier = Modifier
                    .padding(top = 6.dp)
                    .clickable { openCreatorUrl() },
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.primary
            )
        }
    }
    
    /**
     * Request Root Access for Aimbot (uinput)
     * 
     * The new aimbot implementation uses the Linux kernel 'uinput' module to inject
     * touch events directly at the driver level. This bypasses Android's Accessibility
     * Service APIs, offering significantly lower latency (<3ms) and higher precision.
     * 
     * To access /dev/uinput, the application requires ROOT privileges ("su").
     * This function attempts to execute 'su' to trigger the Magisk/SuperSU prompt.
     */
    private fun requestRootAccess(): Boolean {
        return try {
            Log.i(TAG, "Requesting ROOT access for uinput...")
            val process = Runtime.getRuntime().exec("su")
            val outputStream = java.io.DataOutputStream(process.outputStream)
            
            // Fix permissions for /dev/uinput so we can access it from non-root context
            outputStream.writeBytes("chmod 666 /dev/uinput\n")
            // Disable SELinux enforcement (common fix for custom input devices on Android)
            outputStream.writeBytes("setenforce 0\n")
            
            outputStream.writeBytes("exit\n")
            outputStream.flush()
            
            process.waitFor()
            val exitValue = process.exitValue()
            
            if (exitValue == 0) {
                Log.i(TAG, "ROOT access granted and uinput permissions fixed!")
                true
            } else {
                Log.e(TAG, "ROOT access denied (exit code $exitValue)")
                false
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to execute 'su': ${e.message}")
            false
        }
    }

    /**
     * Show dialog if Root is missing
     */
    private fun showRootMissingDialog() {
        AlertDialog.Builder(this)
            .setTitle("Root Access Required")
            .setMessage("The High-Performance Aimbot requires ROOT access to function.\n\nWithout root, only the ESP (Visuals) will work.\n\nPlease grant root permissions in Magisk/KernelSU.")
            .setPositiveButton("Retry") { _, _ ->
                beginAsyncRootCheck(showDialogOnFailure = false)
            }
            .setNegativeButton("Continue without Aimbot") { _, _ ->
                // User can use ESP only
            }
            .setCancelable(false)
            .show()
    }

    private fun beginAsyncRootCheck(showDialogOnFailure: Boolean) {
        if (!rootCheckInProgress.compareAndSet(false, true)) {
            Log.i(TAG, "Root check already in progress")
            return
        }

        Log.i(TAG, "Starting async root check")
        thread(start = true, name = "aimbuddy-root-check") {
            val hasRoot = RootUtils.ensureRoot(this)
            runOnUiThread {
                rootCheckInProgress.set(false)
                rootAvailable.set(hasRoot)
                ImGuiGLSurface.nativeSetRootAvailable(hasRoot)

                if (hasRoot) {
                    Log.i(TAG, "Root available — initializing aimbot")
                    // Guard: only call nativeInitAimbot if native init already succeeded.
                    // nativeIsRunning() == false here just means inference not started yet —
                    // nativeInit success is tracked by statusTextState not being "Init Failed".
                    if (statusTextState != "Status: Init Failed") {
                        if (nativeInitAimbot()) {
                            Log.i(TAG, "Aimbot initialized successfully")
                            showAppToast("Root granted — aimbot enabled ✓", false)
                        } else {
                            Log.w(TAG, "Aimbot init failed after root grant")
                            showAppToast("Root granted but aimbot init failed. Check /dev/uinput.", true)
                        }
                    }
                } else {
                    Log.w(TAG, "Root denied or unavailable")
                    if (showDialogOnFailure && !isFinishing && !isDestroyed) {
                        Handler(Looper.getMainLooper()).postDelayed({
                            if (!isFinishing && !isDestroyed) {
                                showRootMissingDialog()
                            }
                        }, 400)
                    }
                }
            }
        }
    }
}
