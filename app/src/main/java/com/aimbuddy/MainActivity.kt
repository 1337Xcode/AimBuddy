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
import android.media.projection.MediaProjection.Callback
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
import android.content.ActivityNotFoundException
import androidx.activity.result.contract.ActivityResultContracts
import java.io.File
import java.io.FileOutputStream
import com.caverock.androidsvg.SVG
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.clickable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
        private const val PREF_MODEL_PARAM_PATH = "model_param_path"
        private const val PREF_MODEL_BIN_PATH = "model_bin_path"
        private const val ASSET_MODEL_PARAM = "models/yolo26n-opt.param"
        private const val ASSET_MODEL_BIN = "models/yolo26n-opt.bin"

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
    private var pendingStartAfterRoot = false

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
    private var projectionCallbackRegistered = false
    private val mediaProjectionCallback = object : Callback() {
        override fun onStop() {
            Log.w(TAG, "MediaProjection stopped by system/user")
            runOnUiThread {
                if (isRunningState || isStarting.get()) {
                    showAppToast("Screen capture ended. ESP stopped.", true)
                    stopESP()
                }
            }
        }
    }
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
    private external fun nativeSetModelPaths(paramPath: String?, binPath: String?)

    private val importParamLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) {
            return@registerForActivityResult
        }
        val importedParam = copyModelFromUri(uri, "custom-yolo.param")
        if (importedParam == null) {
            showAppToast("Failed to import .param model file", true)
            return@registerForActivityResult
        }

        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            .edit()
            .putString(PREF_MODEL_PARAM_PATH, importedParam.absolutePath)
            .apply()

        showAppToast(".param imported. Now select the matching .bin file.", false)
        importBinLauncher.launch(arrayOf("application/octet-stream", "*/*"))
    }

    private val importBinLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) {
            return@registerForActivityResult
        }
        val importedBin = copyModelFromUri(uri, "custom-yolo.bin")
        if (importedBin == null) {
            showAppToast("Failed to import .bin model file", true)
            return@registerForActivityResult
        }

        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val paramPath = prefs.getString(PREF_MODEL_PARAM_PATH, null)
            ?: File(filesDir, "models/custom-yolo.param").takeIf { it.exists() }?.absolutePath

        if (paramPath == null) {
            showAppToast("Select .param first, then .bin.", true)
            return@registerForActivityResult
        }

        persistAndApplyModelPaths(paramPath, importedBin.absolutePath)
        showAppToast("Model imported from storage", false)
        reinitializeNativeIfIdle()
    }

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

        val hasAssetParam = assetExists(ASSET_MODEL_PARAM)
        val hasAssetBin = assetExists(ASSET_MODEL_BIN)
        if (!hasAssetParam || !hasAssetBin) {
            val missing = buildList {
                if (!hasAssetParam) add(ASSET_MODEL_PARAM)
                if (!hasAssetBin) add(ASSET_MODEL_BIN)
            }.joinToString(", ")
            showAppToast("Missing model in assets: $missing", true)
        }

        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val persistedParamPath = prefs.getString(PREF_MODEL_PARAM_PATH, null)
        val persistedBinPath = prefs.getString(PREF_MODEL_BIN_PATH, null)
        val localParamExists = persistedParamPath != null && File(persistedParamPath).exists()
        val localBinExists = persistedBinPath != null && File(persistedBinPath).exists()

        if (localParamExists && localBinExists) {
            nativeSetModelPaths(persistedParamPath, persistedBinPath)
            Log.i(TAG, "Using imported local model files")
        } else {
            nativeSetModelPaths(null, null)
        }

        // Get MediaProjectionManager
        mediaProjectionManager = getSystemService(Context.MEDIA_PROJECTION_SERVICE)
                as MediaProjectionManager

        // Initialize native components FIRST
        if (!nativeInit(assets, screenWidth, screenHeight)) {
            Log.e(TAG, "Failed to initialize native components")
            if (!hasAssetParam || !hasAssetBin) {
                showAppToast("Initialization failed. Import model manually or add assets models.", true)
            } else {
                showAppToast("Failed to initialize ESP. Check model files.", true)
            }
            setStatus("Status: Init Failed")
        } else {
            setStatus("Status: Ready")
            ImGuiGLSurface.nativeSetRootAvailable(false)
            maybeShowOpenSourceDialogOnce()
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

        if (statusTextState == "Status: Init Failed") {
            showAppToast("Initialization failed. Check model files.", true)
            return
        }

        // Step 1: overlay permission
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

        requestRootThenMediaProjection()
    }

    private fun requestRootThenMediaProjection() {
        // Root is optional for ESP runtime. If already granted, continue immediately.
        if (rootAvailable.get()) {
            requestMediaProjectionPermission()
            return
        }

        pendingStartAfterRoot = true
        setStatus("Status: Waiting for Root Permission")
        showAppToast("Approve root request if you want assisted input.", false)
        beginAsyncRootCheck(showDialogOnFailure = false) { hasRoot ->
            if (!pendingStartAfterRoot) return@beginAsyncRootCheck

            if (hasRoot) {
                setStatus("Status: Root Granted")
                requestMediaProjectionPermission()
            } else {
                setStatus("Status: Ready")
                showRootMissingDialog(
                    onRetry = {
                        requestRootThenMediaProjection()
                    },
                    onContinue = {
                        requestMediaProjectionPermission()
                    }
                )
            }
        }
    }

    private fun requestMediaProjectionPermission() {
        pendingStartAfterRoot = false
        // Step 2 (or 3 when root prompt is shown): media projection permission
        Log.i(TAG, "Requesting MediaProjection")
        val captureIntent = mediaProjectionManager?.createScreenCaptureIntent()
            ?: run {
                Log.e(TAG, "MediaProjectionManager is null")
                showAppToast("Unable to start screen capture", true)
                setStatus("Status: Ready")
                return
            }

        setStatus("Status: Waiting for Screen Capture Permission")

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
                    requestRootThenMediaProjection()
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
                            registerProjectionCallbackIfNeeded()
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

            if (projectionCallbackRegistered) {
                try {
                    mediaProjection?.unregisterCallback(mediaProjectionCallback)
                } catch (_: Exception) {
                }
            }
            mediaProjection?.stop()
            mediaProjection = null
            projectionCallbackRegistered = false

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

        registerProjectionCallbackIfNeeded()

        // Create ImageReader with HardwareBuffer support
        imageReader = ImageReader.newInstance(
            CAPTURE_WIDTH, CAPTURE_HEIGHT,
            PixelFormat.RGBA_8888,
            3  // Triple buffering to prevent producer stalls (matches native IMAGE_READER_MAX_IMAGES)
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

    private fun registerProjectionCallbackIfNeeded() {
        val projection = mediaProjection ?: return
        if (projectionCallbackRegistered) {
            return
        }
        projection.registerCallback(mediaProjectionCallback, Handler(Looper.getMainLooper()))
        projectionCallbackRegistered = true
    }

    private fun assetExists(assetPath: String): Boolean {
        return try {
            assets.open(assetPath).use { }
            true
        } catch (_: Exception) {
            false
        }
    }

    private fun copyModelFromUri(uri: Uri, outputName: String): File? {
        return try {
            val modelDir = File(filesDir, "models")
            if (!modelDir.exists() && !modelDir.mkdirs()) {
                Log.e(TAG, "Failed to create local model directory")
                return null
            }
            val outFile = File(modelDir, outputName)
            contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(outFile).use { output ->
                    input.copyTo(output)
                }
            } ?: return null
            outFile
        } catch (e: Exception) {
            Log.e(TAG, "Failed to copy model from uri: ${e.message}", e)
            null
        }
    }

    private fun persistAndApplyModelPaths(paramPath: String, binPath: String) {
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            .edit()
            .putString(PREF_MODEL_PARAM_PATH, paramPath)
            .putString(PREF_MODEL_BIN_PATH, binPath)
            .apply()

        nativeSetModelPaths(paramPath, binPath)
    }

    private fun reinitializeNativeIfIdle() {
        if (isRunningState || isStarting.get() || isStopping.get()) {
            showAppToast("New model will be used on next start.", false)
            return
        }
        nativeShutdown()
        if (nativeInit(assets, screenWidth, screenHeight)) {
            if (rootAvailable.get()) {
                nativeInitAimbot()
            }
            setStatus("Status: Ready")
            showAppToast("Model applied", false)
        } else {
            setStatus("Status: Init Failed")
            showAppToast("Imported model failed to initialize", true)
        }
    }

    private fun onImportModelClicked() {
        try {
            importParamLauncher.launch(arrayOf("*/*"))
        } catch (e: ActivityNotFoundException) {
            showAppToast("No file picker found on this device", true)
        }
    }

    private fun onStoreClicked() {
        showAppToast("Model Store: Coming soon", false)
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
            .setMessage("AimBuddy is completely free and open source. If you paid for this app, you were scammed.\n\nRepository: $OSS_GITHUB_URL")
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
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(20.dp)
        ) {
            Row(
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(top = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(onClick = { onImportModelClicked() }) {
                    Icon(
                        imageVector = Icons.Filled.FolderOpen,
                        contentDescription = "Import model files"
                    )
                }
                IconButton(onClick = { onStoreClicked() }) {
                    Icon(
                        imageVector = Icons.Filled.Download,
                        contentDescription = "Model store (coming soon)"
                    )
                }
            }

            Column(
                modifier = Modifier
                    .align(Alignment.Center)
                    .fillMaxWidth(),
                verticalArrangement = Arrangement.Center,
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = "AimBuddy",
                    style = MaterialTheme.typography.headlineMedium,
                    color = MaterialTheme.colorScheme.onBackground
                )
                Text(
                    text = "Real-time object detection and tracking",
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
            }

            Row(
                modifier = Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth(),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "GitHub: $OSS_GITHUB_URL",
                    modifier = Modifier.clickable { openGithubUrl() },
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center
                )
                Spacer(modifier = Modifier.width(16.dp))
                Text(
                    text = "Created by $CREATOR_NAME",
                    modifier = Modifier.clickable { openCreatorUrl() },
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary
                )
            }
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
    private fun showRootMissingDialog(onRetry: () -> Unit, onContinue: () -> Unit) {
        AlertDialog.Builder(this)
            .setTitle("Root Access Required")
            .setMessage("Assisted input needs root access.\n\nIf your root app uses fingerprint or MFA, approve it first and wait for confirmation.\n\nYou can retry root now, or continue in Visual Assist mode.")
            .setPositiveButton("Retry") { _, _ ->
                onRetry()
            }
            .setNegativeButton("Continue Visual Only") { _, _ ->
                onContinue()
            }
            .setCancelable(false)
            .show()
    }

    private fun beginAsyncRootCheck(showDialogOnFailure: Boolean, onCompleted: ((Boolean) -> Unit)? = null) {
        if (!rootCheckInProgress.compareAndSet(false, true)) {
            Log.i(TAG, "Root check already in progress")
            return
        }

        Log.i(TAG, "Starting async root check")
        thread(start = true, name = "aimbuddy-root-check") {
            val hasRoot = RootUtils.ensureRoot(this, timeoutSeconds = 90)
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
                            showAppToast("Root granted! Aimbot enabled.", false)
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
                                showRootMissingDialog(
                                    onRetry = { beginAsyncRootCheck(showDialogOnFailure = false, onCompleted = onCompleted) },
                                    onContinue = { }
                                )
                            }
                        }, 400)
                    }
                }
                onCompleted?.invoke(hasRoot)
            }
        }
    }
}
