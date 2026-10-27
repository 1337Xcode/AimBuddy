package com.aimbuddy

import android.app.Activity
import android.content.Context
import android.content.Intent
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
import android.provider.OpenableColumns
import android.util.DisplayMetrics
import android.util.Log
import android.view.Gravity
import android.view.MotionEvent
import android.view.Surface
import android.view.View
import android.view.WindowManager
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
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.clickable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.lang.ref.WeakReference
import kotlin.math.roundToInt
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import rikka.shizuku.Shizuku
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.border
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.OutlinedButton
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.draw.shadow

// Minimal dark palette tuned for the launcher and the small set of
// state colors used on the status pill / backend chips. Kept here as
// top-level so the composables don't recompute them every recomposition.
private val StatusGreen = Color(0xFF34D399)
private val StatusAmber = Color(0xFFFBBF24)
private val StatusRed   = Color(0xFFF87171)
private val StatusGrey  = Color(0xFF6B7280)

private val AimBuddyColors = darkColorScheme(
    primary          = Color(0xFF8AB4F8),
    onPrimary        = Color(0xFF0B1220),
    secondary        = Color(0xFF93C5FD),
    background       = Color(0xFF0B0F17),
    surface          = Color(0xFF111722),
    surfaceVariant   = Color(0xFF1B2230),
    onBackground     = Color(0xFFE5E7EB),
    onSurface        = Color(0xFFE5E7EB),
    onSurfaceVariant = Color(0xFF9CA3AF),
)

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
        private const val REQUEST_SHIZUKU_PERMISSION = 2001

        // Longest edge of the capture image. The detector looks at a region of at most 448 px, so a
        // wider capture only costs GPU time on the phone without helping detection.
        private const val CAPTURE_MAX_LONG_EDGE = 1600

        // Two images are enough: each one is copied and closed inside the callback.
        private const val CAPTURE_MAX_IMAGES = 2
        private const val OSS_GITHUB_URL = "https://github.com/1337XCode/AimBuddy"
        private const val CREATOR_NAME = "1337XCode"
        private const val CREATOR_URL = "https://www.darshanchheda.com"
        private const val PREFS_NAME = "aimbuddy_prefs"
        private const val PREF_OSS_NOTICE_SHOWN = "oss_notice_shown"
        private const val PREF_MODEL_PARAM_PATH = "model_param_path"
        private const val PREF_MODEL_BIN_PATH = "model_bin_path"
        private const val ASSET_MODEL_PARAM = "models/yolo26n-opt.param"
        private const val ASSET_MODEL_BIN = "models/yolo26n-opt.bin"
        private const val STORE_OWNER = "1337Xcode"
        private const val STORE_REPO = "AimBuddy"
        private const val STORE_BRANCH = "master"

        private var activityRef: WeakReference<MainActivity>? = null

        @JvmStatic
        fun nativeInjectShizukuAimMove(screenX: Float, screenY: Float, isFirst: Boolean): Boolean {
            val activity = activityRef?.get() ?: return false
            return activity.injectShizukuAimMove(screenX, screenY, isFirst)
        }

        @JvmStatic
        fun nativeInjectShizukuAimUp(): Boolean {
            val activity = activityRef?.get() ?: return false
            return activity.injectShizukuAimUp()
        }

        /**
         * Toggle FLAG_SECURE on overlay windows. Called from native code via JNI
         * when the user enables/disables Streamer Mode in the in-game menu.
         */
        @JvmStatic
        fun nativeApplyStreamerMode(enabled: Boolean) {
            val activity = activityRef?.get() ?: return
            activity.runOnUiThread { activity.applyStreamerModeFlag(enabled) }
        }

        init {
            System.loadLibrary("esp_native")
        }
    }

    // UI state
    private var isRunningState by mutableStateOf(false)
    private var statusTextState by mutableStateOf("Status: Model Loading")
    private var activeModelTextState by mutableStateOf("Model: Auto")
    private var showStoreState by mutableStateOf(false)
    private var isFetchingStoreState by mutableStateOf(false)
    private var storeModelsState by mutableStateOf<List<StoreModelDefinition>>(emptyList())
    private var downloadingModelIdState by mutableStateOf<String?>(null)
    private var installedModelsState by mutableStateOf<List<InstalledModel>>(emptyList())

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
    private val shizukuAvailable = AtomicBoolean(false)
    private var pendingStartAfterRoot = false
    private var pendingStartAfterShizuku = false
    private var pendingShizukuPermissionRequest = false
    private var shizukuWaitStartedAt = 0L
    private var shizukuInjector: ShizukuInputInjector? = null
    private val shizukuBinderReceivedListener = Shizuku.OnBinderReceivedListener {
        runOnUiThread {
            refreshShizukuState()
            if (pendingStartAfterShizuku && !isStarting.get()) {
                requestShizukuThenMediaProjection()
            }
        }
    }
    private val shizukuBinderDeadListener = Shizuku.OnBinderDeadListener {
        runOnUiThread {
            refreshShizukuState(forceUnavailable = true)
            if (nativeGetTouchBackend() == 1) {
                showAppToast("Shizuku disconnected. Reconnect and press Start again.", true)
            }
        }
    }
    private val shizukuPermissionListener = Shizuku.OnRequestPermissionResultListener { requestCode, grantResult ->
        if (requestCode != REQUEST_SHIZUKU_PERMISSION) {
            return@OnRequestPermissionResultListener
        }
        val granted = grantResult == PackageManager.PERMISSION_GRANTED
        pendingShizukuPermissionRequest = false
        refreshShizukuState()
        if (granted) {
            showAppToast("Shizuku permission granted.", false)
        } else {
            showAppToast("Shizuku permission denied. Aim assist stays visual-only.", true)
        }
        if (pendingStartAfterShizuku) {
            pendingStartAfterShizuku = false
            requestMediaProjectionPermission()
        }
    }
    private var pendingImportParamUri: Uri? = null
    private var pendingImportName: String = "local-model"

    private lateinit var modelCatalog: ModelCatalog
    private val storeRepository = ModelStoreRepository(
        owner = STORE_OWNER,
        repo = STORE_REPO,
        branch = STORE_BRANCH
    )

    // Streamer mode (FLAG_SECURE on overlay windows)
    @Volatile
    private var streamerModeEnabled: Boolean = false

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
    private external fun nativeConfigureCapture(screenWidth: Int, screenHeight: Int,
                                                captureWidth: Int, captureHeight: Int)
    private external fun nativeStart()
    private external fun nativeStop()
    private external fun nativeShutdown()
    private external fun nativeIsRunning(): Boolean
    private external fun nativeInitAimbot(): Boolean
    private external fun nativeSetModelPaths(paramPath: String?, binPath: String?)
    private external fun nativeSetModelProfile(roles: String?, endToEnd: Boolean, inputSize: Int)
    private external fun nativeSetTouchBackend(backend: Int)
    private external fun nativeGetTouchBackend(): Int
    private external fun nativeSetShizukuBridgeAvailable(available: Boolean)

    private val importParamLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) {
            return@registerForActivityResult
        }
        pendingImportParamUri = uri
        pendingImportName = getDisplayName(uri).substringBeforeLast('.', "local-model")

        showAppToast(".param imported. Now select the matching .bin file.", false)
        importBinLauncher.launch(arrayOf("application/octet-stream", "*/*"))
    }

    private val importBinLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) {
            return@registerForActivityResult
        }
        val paramUri = pendingImportParamUri
        if (paramUri == null) {
            showAppToast("Select .param first, then .bin.", true)
            return@registerForActivityResult
        }

        val modelId = modelCatalog.sanitizeModelId("local-${pendingImportName}-${System.currentTimeMillis()}")
        val installDir = File(filesDir, "models/$modelId")
        val paramFile = File(installDir, "$modelId.param")
        val binFile = File(installDir, "$modelId.bin")

        val paramOk = copyUriToFile(paramUri, paramFile)
        val binOk = copyUriToFile(uri, binFile)
        if (!paramOk || !binOk) {
            showAppToast("Failed to import selected model files", true)
            return@registerForActivityResult
        }

        val installed = InstalledModel(
            id = modelId,
            title = pendingImportName,
            description = "Imported from local storage",
            source = ModelSource.LOCAL,
            paramPath = paramFile.absolutePath,
            binPath = binFile.absolutePath,
            totalSizeBytes = paramFile.length() + binFile.length()
        )
        modelCatalog.addOrUpdateModel(installed, makeActive = true)
        applyActiveModelSelection()
        showAppToast("Model imported from storage", false)
        reinitializeNativeIfIdle()
        pendingImportParamUri = null
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        activityRef = WeakReference(this)
        
        // Force landscape orientation
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        
        setContent {
            MaterialTheme(colorScheme = AimBuddyColors) {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    LauncherScreen(
                        isRunning = isRunningState,
                        statusText = statusTextState,
                        activeModelText = activeModelTextState,
                        rootReady = rootAvailable.get(),
                        shizukuReady = shizukuAvailable.get(),
                        onStart = { onStartClicked() },
                        onStop = { onStopClicked() },
                        onImportModel = { onImportModelClicked() },
                        onOpenStore = { onStoreClicked() },
                        onOpenGithub = { openGithubUrl() },
                        onOpenCreator = { openCreatorUrl() },
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
        modelCatalog = ModelCatalog(this)

        val hasAssetParam = assetExists(ASSET_MODEL_PARAM)
        val hasAssetBin = assetExists(ASSET_MODEL_BIN)
        if (!hasAssetParam || !hasAssetBin) {
            val missing = buildList {
                if (!hasAssetParam) add(ASSET_MODEL_PARAM)
                if (!hasAssetBin) add(ASSET_MODEL_BIN)
            }.joinToString(", ")
            showAppToast("Missing model in assets: $missing", true)
        }

        modelCatalog.ensureDefaultAssetModel(hasAssetParam, hasAssetBin)
        migrateLegacySingleImportedModel()
        applyActiveModelSelection()

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
            refreshShizukuState()
            maybeShowOpenSourceDialogOnce()
        }

        Shizuku.addRequestPermissionResultListener(shizukuPermissionListener)
        Shizuku.addBinderReceivedListenerSticky(shizukuBinderReceivedListener)
        Shizuku.addBinderDeadListener(shizukuBinderDeadListener)
    }
    
    override fun onDestroy() {
        Log.i(TAG, "onDestroy")
        stopESP()
        Shizuku.removeRequestPermissionResultListener(shizukuPermissionListener)
        Shizuku.removeBinderReceivedListener(shizukuBinderReceivedListener)
        Shizuku.removeBinderDeadListener(shizukuBinderDeadListener)
        activityRef = null
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
        val backend = resolveEffectiveTouchBackend(nativeGetTouchBackend().coerceIn(0, 1))
        nativeSetTouchBackend(backend)

        if (backend == 1) {
            requestShizukuThenMediaProjection()
            return
        }

        // Root is optional for ESP runtime. If already granted, continue immediately.
        if (rootAvailable.get()) {
            requestMediaProjectionPermission()
            return
        }

        pendingStartAfterRoot = true
        setStatus("Status: Waiting for Root Permission")
        showAppToast("Approve root request if you want assisted input.", false)
        beginAsyncRootCheck { hasRoot ->
            if (!pendingStartAfterRoot) return@beginAsyncRootCheck

            if (hasRoot) {
                setStatus("Status: Root Granted")
                requestMediaProjectionPermission()
            } else {
                pendingStartAfterRoot = false
                nativeSetTouchBackend(1)
                showAppToast("Root unavailable. Switched to Shizuku non-root input.", false)
                setStatus("Status: Using Shizuku Backend")
                requestShizukuThenMediaProjection()
            }
        }
    }

    private fun requestShizukuThenMediaProjection() {
        if (!pendingStartAfterShizuku) {
            shizukuWaitStartedAt = System.currentTimeMillis()
        }
        pendingStartAfterShizuku = true
        setStatus("Status: Waiting for Shizuku Permission")

        refreshShizukuState()
        if (!Shizuku.pingBinder()) {
            if (isRootLikelyAvailable()) {
                pendingStartAfterShizuku = false
                nativeSetTouchBackend(0)
                showAppToast("Shizuku unavailable. Switched to root uinput input.", false)
                setStatus("Status: Using Root Backend")
                requestRootThenMediaProjection()
            } else {
                showAppToast("Waiting for Shizuku connection...", false)
                val waitedMs = System.currentTimeMillis() - shizukuWaitStartedAt
                if (waitedMs > 4000) {
                    pendingStartAfterShizuku = false
                    setStatus("Status: Shizuku Not Connected")
                    showShizukuConnectionHelpDialog()
                }
            }
            return
        }

        if (Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED) {
            shizukuAvailable.set(true)
            nativeSetShizukuBridgeAvailable(true)
            ImGuiGLSurface.nativeSetShizukuAvailable(true)
            if (statusTextState != "Status: Init Failed") {
                nativeInitAimbot()
            }
            pendingStartAfterShizuku = false
            requestMediaProjectionPermission()
            return
        }

        if (!pendingShizukuPermissionRequest) {
            pendingShizukuPermissionRequest = true
            try {
                Shizuku.requestPermission(REQUEST_SHIZUKU_PERMISSION)
                showAppToast("Approve Shizuku permission request.", false)
            } catch (e: Throwable) {
                pendingShizukuPermissionRequest = false
                pendingStartAfterShizuku = false
                showAppToast("Failed to request Shizuku permission: ${e.message}", true)
                setStatus("Status: Ready")
            }
        }
    }

    private fun showShizukuConnectionHelpDialog() {
        AlertDialog.Builder(this)
            .setTitle("Shizuku Not Connected")
            .setMessage(
                "AimBuddy could not receive Shizuku binder.\n\n" +
                    "1) Open Shizuku app and make sure service is running\n" +
                    "2) Confirm AimBuddy is allowed in Shizuku app\n" +
                    "3) Return and press Start again"
            )
            .setPositiveButton("Open Shizuku") { _, _ ->
                val launch = packageManager.getLaunchIntentForPackage("moe.shizuku.privileged.api")
                if (launch != null) {
                    startActivity(launch)
                } else {
                    showAppToast("Shizuku app not found.", true)
                }
            }
            .setNegativeButton("Retry") { _, _ ->
                requestShizukuThenMediaProjection()
            }
            .setNeutralButton("Continue Visual Only") { _, _ ->
                requestMediaProjectionPermission()
            }
            .show()
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

    /**
     * Capture size with the screen's aspect ratio, so nothing is letterboxed or stretched,
     * and a long edge of at most [CAPTURE_MAX_LONG_EDGE]. Both sides are even.
     */
    private fun computeCaptureSize(): Pair<Int, Int> {
        val scale = minOf(1f, CAPTURE_MAX_LONG_EDGE.toFloat() / maxOf(screenWidth, screenHeight))
        val width = (screenWidth * scale).roundToInt() and 1.inv()
        val height = (screenHeight * scale).roundToInt() and 1.inv()
        return width to height
    }

    private fun setupScreenCapture() {
        val (captureWidth, captureHeight) = computeCaptureSize()
        Log.i(TAG, "Setting up screen capture at ${captureWidth}x${captureHeight}")

        registerProjectionCallbackIfNeeded()
        nativeConfigureCapture(screenWidth, screenHeight, captureWidth, captureHeight)

        imageReader = ImageReader.newInstance(
            captureWidth, captureHeight,
            PixelFormat.RGBA_8888,
            CAPTURE_MAX_IMAGES
        ).apply {
            setOnImageAvailableListener({ reader ->
                val image = reader.acquireLatestImage() ?: return@setOnImageAvailableListener
                try {
                    // Native code copies the detector's region before returning, so the
                    // buffer can be handed straight back to the compositor afterwards.
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

        virtualDisplay = mediaProjection?.createVirtualDisplay(
            "ESPCapture",
            captureWidth, captureHeight, screenDensity,
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

    private fun copyUriToFile(uri: Uri, outFile: File): Boolean {
        return try {
            val modelDir = outFile.parentFile
            if (modelDir == null) {
                return false
            }
            if (!modelDir.exists() && !modelDir.mkdirs()) {
                Log.e(TAG, "Failed to create local model directory")
                return false
            }
            contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(outFile).use { output ->
                    input.copyTo(output)
                }
            } ?: return false
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to copy model from uri: ${e.message}", e)
            false
        }
    }

    private fun getDisplayName(uri: Uri): String {
        var name = "model"
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) {
                name = cursor.getString(index) ?: name
            }
        }
        return name
    }

    private fun applyActiveModelSelection() {
        val active = modelCatalog.getActiveModel()
        installedModelsState = modelCatalog.getInstalledModels()
        if (active == null) {
            nativeSetModelPaths(null, null)
            applyModelProfile(ModelProfileSpec.UNKNOWN)
            activeModelTextState = "Model: Missing"
            return
        }

        if (active.source == ModelSource.ASSET) {
            nativeSetModelPaths(null, null)
            applyModelProfile(ModelProfileSpec.readFromAssets(assets))
        } else {
            nativeSetModelPaths(active.paramPath, active.binPath)
            applyModelProfile(ModelProfileSpec.readFor(active.paramPath))
        }
        activeModelTextState = "Model: ${active.title} (${active.source.name.lowercase()})"
    }

    /** Tells the native detector what the active model's classes mean. */
    private fun applyModelProfile(spec: ModelProfileSpec) {
        nativeSetModelProfile(spec.roles, spec.endToEnd, spec.inputSize)
    }

    private fun reinitializeNativeIfIdle() {
        if (isRunningState || isStarting.get() || isStopping.get()) {
            showAppToast("New model will be used on next start.", false)
            return
        }
        nativeShutdown()
        applyActiveModelSelection()
        if (nativeInit(assets, screenWidth, screenHeight)) {
            if (rootAvailable.get() || shizukuAvailable.get()) {
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
            importParamLauncher.launch(arrayOf("application/octet-stream", "*/*"))
        } catch (e: ActivityNotFoundException) {
            showAppToast("No file picker found on this device", true)
        }
    }

    private fun onStoreClicked() {
        openModelStore()
    }

    private fun migrateLegacySingleImportedModel() {
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val legacyParam = prefs.getString(PREF_MODEL_PARAM_PATH, null)
        val legacyBin = prefs.getString(PREF_MODEL_BIN_PATH, null)
        if (legacyParam.isNullOrBlank() || legacyBin.isNullOrBlank()) {
            return
        }

        val paramFile = File(legacyParam)
        val binFile = File(legacyBin)
        if (!paramFile.exists() || !binFile.exists()) {
            return
        }

        val legacyModel = InstalledModel(
            id = modelCatalog.sanitizeModelId("legacy-local-model"),
            title = "Legacy Imported Model",
            description = "Migrated from previous app version",
            source = ModelSource.LOCAL,
            paramPath = paramFile.absolutePath,
            binPath = binFile.absolutePath,
            totalSizeBytes = paramFile.length() + binFile.length()
        )
        modelCatalog.addOrUpdateModel(legacyModel, makeActive = false)
    }

    private fun openModelStore() {
        showStoreState = true
        if (storeModelsState.isEmpty()) {
            fetchStoreModelsAsync()
        }
    }

    private fun fetchStoreModelsAsync() {
        isFetchingStoreState = true
        thread(start = true, name = "aimbuddy-store-fetch") {
            try {
                val models = storeRepository.fetchAvailableModels()
                runOnUiThread {
                    storeModelsState = models
                    isFetchingStoreState = false
                }
            } catch (e: Exception) {
                runOnUiThread {
                    isFetchingStoreState = false
                    showAppToast("Store fetch failed: ${e.message}", true)
                }
            }
        }
    }

    private fun downloadStoreModelAsync(model: StoreModelDefinition) {
        downloadingModelIdState = model.id
        showAppToast("Downloading ${model.title}...", false)
        thread(start = true, name = "aimbuddy-store-download") {
            try {
                val modelId = modelCatalog.sanitizeModelId(model.id)
                val targetDir = File(filesDir, "models/$modelId")
                val downloaded = storeRepository.downloadModel(model, targetDir)

                val installed = InstalledModel(
                    id = modelId,
                    title = model.title,
                    description = model.description,
                    source = ModelSource.STORE,
                    paramPath = downloaded.first.absolutePath,
                    binPath = downloaded.second.absolutePath,
                    totalSizeBytes = downloaded.first.length() + downloaded.second.length()
                )
                modelCatalog.addOrUpdateModel(installed, makeActive = true)

                runOnUiThread {
                    downloadingModelIdState = null
                    applyActiveModelSelection()
                    reinitializeNativeIfIdle()
                    showAppToast("Downloaded and switched to ${model.title}", false)
                }
            } catch (e: Exception) {
                runOnUiThread {
                    downloadingModelIdState = null
                    showAppToast("Download failed: ${e.message}", true)
                }
            }
        }
    }

    private fun showDeleteModelConfirmationDialog(model: InstalledModel) {
        AlertDialog.Builder(this)
            .setTitle("Delete Model")
            .setMessage("Are you sure you want to permanently delete \"${model.title}\" and free up space?")
            .setPositiveButton("Delete") { _, _ ->
                if (modelCatalog.deleteModel(model.id)) {
                    applyActiveModelSelection()
                    reinitializeNativeIfIdle()
                    showAppToast("Model deleted successfully", false)
                } else {
                    showAppToast("Failed to delete model", true)
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun formatBytes(bytes: Long): String {
        if (bytes < 1024L) {
            return "${bytes} B"
        }
        val kb = bytes / 1024.0
        if (kb < 1024.0) {
            return String.format("%.1f KB", kb)
        }
        val mb = kb / 1024.0
        if (mb < 1024.0) {
            return String.format("%.2f MB", mb)
        }
        val gb = mb / 1024.0
        return String.format("%.2f GB", gb)
    }

    private fun refreshShizukuState(forceUnavailable: Boolean = false) {
        val available = if (forceUnavailable) {
            false
        } else {
            try {
                Shizuku.pingBinder() &&
                    Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
            } catch (_: Throwable) {
                false
            }
        }

        shizukuAvailable.set(available)
        if (available && shizukuInjector == null) {
            shizukuInjector = ShizukuInputInjector()
        }
        if (!available) {
            shizukuInjector = null
        }

        nativeSetShizukuBridgeAvailable(available)
        ImGuiGLSurface.nativeSetShizukuAvailable(available)
    }

    private fun isRootLikelyAvailable(): Boolean {
        return rootAvailable.get() || RootUtils.isRootAvailable()
    }

    private fun resolveEffectiveTouchBackend(preferredBackend: Int): Int {
        val rootReady = isRootLikelyAvailable()
        val shizukuReady = shizukuAvailable.get()

        return when (preferredBackend) {
            0 -> if (rootReady) 0 else if (shizukuReady) 1 else 0
            1 -> if (shizukuReady) 1 else if (rootReady) 0 else 1
            else -> preferredBackend.coerceIn(0, 1)
        }
    }

    /**
     * Apply or clear FLAG_SECURE on overlay windows.
     * When the flag is set, MediaProjection / screen recorders / screenshots
     * will not capture the overlay  -  only the game underneath.
     */
    fun applyStreamerModeFlag(enabled: Boolean) {
        streamerModeEnabled = enabled
        val wm = windowManager ?: return

        imguiOverlay?.let { view ->
            // GLSurfaceView is a SurfaceView; SurfaceView owns a separate
            // surface composited by SurfaceFlinger that does NOT inherit
            // FLAG_SECURE from the parent window on all Android versions.
            // Call setSecure() explicitly so MediaProjection blanks the GL
            // surface regardless of the host window flag.
            try { view.setSecure(enabled) } catch (_: Throwable) {}

            val params = view.layoutParams as? WindowManager.LayoutParams ?: return@let
            if (enabled) {
                params.flags = params.flags or WindowManager.LayoutParams.FLAG_SECURE
            } else {
                params.flags = params.flags and WindowManager.LayoutParams.FLAG_SECURE.inv()
            }
            try { wm.updateViewLayout(view, params) } catch (_: IllegalArgumentException) {}
        }

        floatingIconView?.let { view ->
            val params = floatingIconParams ?: return@let
            if (enabled) {
                params.flags = params.flags or WindowManager.LayoutParams.FLAG_SECURE
            } else {
                params.flags = params.flags and WindowManager.LayoutParams.FLAG_SECURE.inv()
            }
            try { wm.updateViewLayout(view, params) } catch (_: IllegalArgumentException) {}
        }
    }

    private fun injectShizukuAimMove(screenX: Float, screenY: Float, isFirst: Boolean): Boolean {
        if (!shizukuAvailable.get()) {
            return false
        }
        val injector = shizukuInjector ?: return false
        return injector.injectAimMove(screenX, screenY, isFirst)
    }

    private fun injectShizukuAimUp(): Boolean {
        val injector = shizukuInjector ?: return false
        return injector.releaseAim()
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

        if (streamerModeEnabled) {
            layoutParams.flags = layoutParams.flags or WindowManager.LayoutParams.FLAG_SECURE
            try { imguiOverlay?.setSecure(true) } catch (_: Throwable) {}
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

        if (streamerModeEnabled) {
            params.flags = params.flags or WindowManager.LayoutParams.FLAG_SECURE
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

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    private fun LauncherScreen(
        isRunning: Boolean,
        statusText: String,
        activeModelText: String,
        rootReady: Boolean,
        shizukuReady: Boolean,
        onStart: () -> Unit,
        onStop: () -> Unit,
        onImportModel: () -> Unit,
        onOpenStore: () -> Unit,
        onOpenGithub: () -> Unit,
        onOpenCreator: () -> Unit,
    ) {
        var menuOpen by remember { mutableStateOf(false) }
        val statusColor = statusAccentFor(statusText, isRunning)

        Box(modifier = Modifier.fillMaxSize()) {
            Scaffold(
                containerColor = MaterialTheme.colorScheme.background,
                topBar = {
                    CenterAlignedTopAppBar(
                        title = {
                            Text(
                                text = "AimBuddy",
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.SemiBold,
                            )
                        },
                        actions = {
                            IconButton(onClick = { menuOpen = true }) {
                                Icon(Icons.Filled.MoreVert, contentDescription = "More")
                            }
                        },
                        colors = TopAppBarDefaults.centerAlignedTopAppBarColors(
                            containerColor = MaterialTheme.colorScheme.background,
                        ),
                    )
                },
            ) { inner ->
                Row(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(inner)
                        .padding(horizontal = 24.dp, vertical = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(24.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    // Left Panel: Status & Active Model
                    Column(
                        modifier = Modifier
                            .weight(1.2f)
                            .fillMaxHeight(),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.Start
                    ) {
                        StatusCard(
                            statusText = statusText,
                            accent = statusColor,
                            isRunning = isRunning,
                        )
                        
                        Spacer(Modifier.height(12.dp))
                        
                        ActiveModelCard(
                            activeModelText = activeModelText,
                            onSelectModel = { onOpenStore() }
                        )
                    }

                    // Right Panel: Primary Toggle & Backend Status
                    Column(
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxHeight(),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        PrimaryActionButton(
                            isRunning = isRunning,
                            onClick = if (isRunning) onStop else onStart
                        )

                        Spacer(Modifier.height(14.dp))

                        Text(
                            text = "Touch Input Backends",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            fontWeight = FontWeight.SemiBold,
                            modifier = Modifier.padding(bottom = 6.dp)
                        )

                        BackendChips(rootReady = rootReady, shizukuReady = shizukuReady)

                        Spacer(Modifier.height(14.dp))

                        Text(
                            text = "Open source - created by $CREATOR_NAME",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                            modifier = Modifier
                                .clickable { onOpenCreator() }
                                .padding(vertical = 4.dp)
                        )
                    }
                }
            }

            // In-window custom Dropdown Menu Overlay
            if (menuOpen) {
                // Click catcher
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(Color.Black.copy(alpha = 0.2f))
                        .clickable(
                            interactionSource = remember { MutableInteractionSource() },
                            indication = null
                        ) { menuOpen = false }
                )
                
                // Dropdown Card positioned at Top-End
                Card(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(top = 56.dp, end = 16.dp)
                        .width(220.dp)
                        .shadow(12.dp, RoundedCornerShape(14.dp)),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant,
                    ),
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.15f))
                ) {
                    Column(modifier = Modifier.padding(vertical = 4.dp)) {
                        DropdownMenuItemContent(
                            text = "Import model files",
                            icon = Icons.Filled.FolderOpen,
                            onClick = { menuOpen = false; onImportModel() }
                        )
                        HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.08f))
                        DropdownMenuItemContent(
                            text = "Model store",
                            icon = Icons.Filled.Download,
                            onClick = { menuOpen = false; onOpenStore() }
                        )
                        HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.08f))
                        DropdownMenuItemContent(
                            text = "View on GitHub",
                            icon = Icons.Filled.Info,
                            onClick = { menuOpen = false; onOpenGithub() }
                        )
                    }
                }
            }

            // Custom Compose Model Store Screen overlay
            if (showStoreState) {
                ModelStoreScreen(
                    onClose = { showStoreState = false },
                    onImportModel = onImportModel
                )
            }
        }
    }

    @Composable
    private fun DropdownMenuItemContent(
        text: String,
        icon: ImageVector,
        onClick: () -> Unit
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onClick() }
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(20.dp)
            )
            Spacer(modifier = Modifier.width(12.dp))
            Text(
                text = text,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface,
                fontWeight = FontWeight.Medium
            )
        }
    }

    private fun mapStatusToDisplayTitle(status: String): String {
        val raw = status.removePrefix("Status: ").trim()
        return when (raw) {
            "Model Loading" -> "Initializing Model"
            "Init Failed" -> "Setup Failed"
            "Ready" -> "System Ready"
            "Waiting for Root Permission" -> "Root Request Pending"
            "Root Granted" -> "Root Access Confirmed"
            "Using Shizuku Backend" -> "Shizuku Backend Active"
            "Waiting for Shizuku Permission" -> "Shizuku Request Pending"
            "Using Root Backend" -> "Root Backend Active"
            "Shizuku Not Connected" -> "Shizuku Offline"
            "Waiting for Screen Capture Permission" -> "Capture Auth Pending"
            "Starting" -> "Starting Service"
            "Stopping" -> "Stopping Service"
            "Running" -> "Service Active"
            else -> raw
        }
    }

    private fun mapStatusToDisplayDescription(status: String): String {
        val raw = status.removePrefix("Status: ").trim()
        return when (raw) {
            "Model Loading" -> "Mapping memory buffers and initializing weights..."
            "Init Failed" -> "Engine initialization failed. Please check your model files."
            "Ready" -> "System configured. Tap START SERVICE below to begin."
            "Waiting for Root Permission" -> "Please grant Superuser authorization when prompted."
            "Root Granted" -> "Superuser access verified. Mounting touch inputs."
            "Using Shizuku Backend" -> "Synthetic touch events routed via Shizuku wrapper."
            "Waiting for Shizuku Permission" -> "Please authorize Shizuku shell manager when prompted."
            "Using Root Backend" -> "Synthetic touch events injected directly to /dev/uinput."
            "Shizuku Not Connected" -> "Start Shizuku server inside the Shizuku Manager app."
            "Waiting for Screen Capture Permission" -> "Please confirm screen projection permission in dialog."
            "Starting" -> "Instantiating virtual devices and capture threads..."
            "Stopping" -> "Disposing graphics surfaces and stopping background tasks..."
            "Running" -> "Drawing ESP overlay dynamically on top of capture buffers."
            else -> "System state: $raw"
        }
    }

    @Composable
    private fun StatusCard(statusText: String, accent: Color, isRunning: Boolean) {
        val displayTitle = mapStatusToDisplayTitle(statusText)
        val displayDescription = mapStatusToDisplayDescription(statusText)
        
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
            ),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.12f)),
            elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp, vertical = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Start
            ) {
                Box(
                    modifier = Modifier
                        .size(12.dp)
                        .clip(CircleShape)
                        .background(accent)
                        .border(
                            2.dp,
                            accent.copy(alpha = 0.4f),
                            CircleShape
                        )
                )
                Spacer(Modifier.width(16.dp))
                Column {
                    Text(
                        text = displayTitle,
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(Modifier.height(4.dp))
                    Text(
                        text = displayDescription,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 2
                    )
                }
            }
        }
    }

    @Composable
    private fun ActiveModelCard(activeModelText: String, onSelectModel: () -> Unit) {
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onSelectModel() },
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f),
            ),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.08f)),
            elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "ACTIVE MODEL",
                        style = MaterialTheme.typography.labelSmall,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f)
                    )
                    Spacer(Modifier.height(2.dp))
                    Text(
                        text = activeModelText.removePrefix("Model: "),
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 1
                    )
                }
                Icon(
                    imageVector = Icons.Filled.FolderOpen,
                    contentDescription = "Manage Models",
                    tint = MaterialTheme.colorScheme.secondary,
                    modifier = Modifier.size(18.dp)
                )
            }
        }
    }

    @Composable
    private fun PrimaryActionButton(isRunning: Boolean, onClick: () -> Unit) {
        val containerColor = if (isRunning) Color(0xFFDC2626) else MaterialTheme.colorScheme.primary
        val contentColor = if (isRunning) Color.White else Color(0xFF070B13)
        val icon = if (isRunning) Icons.Filled.Stop else Icons.Filled.PlayArrow
        val label = if (isRunning) "STOP SERVICE" else "START SERVICE"
        
        Card(
            modifier = Modifier
                .width(220.dp)
                .height(52.dp)
                .clickable { onClick() }
                .shadow(6.dp, RoundedCornerShape(26.dp)),
            shape = RoundedCornerShape(26.dp),
            colors = CardDefaults.cardColors(
                containerColor = containerColor,
                contentColor = contentColor
            )
        ) {
            Row(
                modifier = Modifier.fillMaxSize(),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    modifier = Modifier.size(22.dp)
                )
                Spacer(Modifier.width(8.dp))
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelLarge,
                    fontWeight = FontWeight.Bold,
                    fontSize = 14.sp,
                    letterSpacing = 0.8.sp
                )
            }
        }
    }

    @Composable
    private fun BackendChips(rootReady: Boolean, shizukuReady: Boolean) {
        Row(
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            BackendChip(label = "Root", ready = rootReady)
            Spacer(Modifier.width(12.dp))
            BackendChip(label = "Shizuku", ready = shizukuReady)
        }
    }

    @Composable
    private fun BackendChip(label: String, ready: Boolean) {
        val color = if (ready) StatusGreen else MaterialTheme.colorScheme.onSurfaceVariant
        AssistChip(
            onClick = {},
            label = { Text(label, fontWeight = FontWeight.SemiBold, fontSize = 12.sp) },
            leadingIcon = {
                Box(
                    modifier = Modifier
                        .size(8.dp)
                        .clip(CircleShape)
                        .background(color),
                )
            },
            colors = AssistChipDefaults.assistChipColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f),
            ),
            shape = RoundedCornerShape(10.dp)
        )
    }

    @Composable
    private fun ModelStoreScreen(
        onClose: () -> Unit,
        onImportModel: () -> Unit
    ) {
        var selectedTab by remember { mutableStateOf(0) }

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 24.dp, vertical = 16.dp)
            ) {
                // Header
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        IconButton(onClick = onClose) {
                            Icon(Icons.Filled.ArrowBack, contentDescription = "Back")
                        }
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = "Model Store",
                            style = MaterialTheme.typography.headlineSmall,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        if (selectedTab == 0) {
                            IconButton(
                                onClick = { fetchStoreModelsAsync() },
                                enabled = !isFetchingStoreState
                            ) {
                                if (isFetchingStoreState) {
                                    CircularProgressIndicator(
                                        modifier = Modifier.size(20.dp),
                                        strokeWidth = 2.dp,
                                        color = MaterialTheme.colorScheme.primary
                                    )
                                } else {
                                    Icon(Icons.Filled.Refresh, contentDescription = "Refresh")
                                }
                            }
                        }
                        
                        Spacer(Modifier.width(12.dp))
                        
                        OutlinedButton(
                            onClick = onImportModel,
                            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 8.dp),
                            shape = RoundedCornerShape(12.dp),
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.5f))
                        ) {
                            Icon(Icons.Filled.FolderOpen, contentDescription = null, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(6.dp))
                            Text("Import Local", style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }

                Spacer(Modifier.height(12.dp))

                // Tab Selector
                TabRow(
                    selectedTabIndex = selectedTab,
                    containerColor = Color.Transparent,
                    divider = { HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.08f)) },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Tab(
                        selected = selectedTab == 0,
                        onClick = { selectedTab = 0 },
                        text = { Text("Store Catalog", fontWeight = FontWeight.Bold) }
                    )
                    Tab(
                        selected = selectedTab == 1,
                        onClick = { selectedTab = 1 },
                        text = { Text("My Models (${installedModelsState.size})", fontWeight = FontWeight.Bold) }
                    )
                }

                Spacer(Modifier.height(16.dp))

                // Tab Content
                Box(modifier = Modifier.weight(1f)) {
                    if (selectedTab == 0) {
                        CatalogTabContent()
                    } else {
                        InstalledTabContent()
                    }
                }
            }
        }
    }

    @Composable
    private fun CatalogTabContent() {
        if (isFetchingStoreState && storeModelsState.isEmpty()) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    CircularProgressIndicator(color = MaterialTheme.colorScheme.primary)
                    Spacer(Modifier.height(12.dp))
                    Text("Fetching catalog from GitHub...", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        } else if (storeModelsState.isEmpty()) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("No models available in store.", color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(Modifier.height(12.dp))
                    OutlinedButton(onClick = { fetchStoreModelsAsync() }) {
                        Text("Retry Fetch")
                    }
                }
            }
        } else {
            LazyColumn(
                verticalArrangement = Arrangement.spacedBy(12.dp),
                modifier = Modifier.fillMaxSize()
            ) {
                items(storeModelsState) { model ->
                    val isInstalled = installedModelsState.any { it.id == model.id }
                    val isDownloading = downloadingModelIdState == model.id
                    val isActive = modelCatalog.getActiveModel()?.id == model.id
                    
                    CatalogModelCard(
                        model = model,
                        isInstalled = isInstalled,
                        isActive = isActive,
                        isDownloading = isDownloading,
                        onDownload = { downloadStoreModelAsync(model) },
                        onUse = {
                            if (modelCatalog.setActiveModel(model.id)) {
                                applyActiveModelSelection()
                                reinitializeNativeIfIdle()
                            }
                        }
                    )
                }
            }
        }
    }

    @Composable
    private fun CatalogModelCard(
        model: StoreModelDefinition,
        isInstalled: Boolean,
        isActive: Boolean,
        isDownloading: Boolean,
        onDownload: () -> Unit,
        onUse: () -> Unit
    ) {
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(14.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
            ),
            border = BorderStroke(
                1.dp,
                if (isActive) MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
                else MaterialTheme.colorScheme.outline.copy(alpha = 0.08f)
            )
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                text = model.title,
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            if (isActive) {
                                Spacer(Modifier.width(8.dp))
                                Box(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(4.dp))
                                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.15f))
                                        .padding(horizontal = 6.dp, vertical = 2.dp)
                                ) {
                                    Text(
                                        text = "ACTIVE",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.primary,
                                        fontWeight = FontWeight.Bold
                                    )
                                }
                            }
                        }
                        Spacer(Modifier.height(4.dp))
                        Text(
                            text = model.description,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    
                    Spacer(Modifier.width(16.dp))
                    
                    Box {
                        if (isDownloading) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(24.dp),
                                color = MaterialTheme.colorScheme.primary,
                                strokeWidth = 2.5.dp
                            )
                        } else if (isActive) {
                            // Already active
                        } else if (isInstalled) {
                            OutlinedButton(
                                onClick = onUse,
                                shape = RoundedCornerShape(10.dp),
                                contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp)
                            ) {
                                Text("Use", style = MaterialTheme.typography.labelMedium)
                            }
                        } else if (model.isDownloadable) {
                            FilledTonalButton(
                                onClick = onDownload,
                                shape = RoundedCornerShape(10.dp),
                                contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp)
                            ) {
                                Text("Download", style = MaterialTheme.typography.labelMedium)
                            }
                        } else {
                            Text(
                                text = "Demo",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                fontWeight = FontWeight.Bold
                            )
                        }
                    }
                }
                
                Spacer(Modifier.height(10.dp))
                
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Start,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    val sizeStr = if (model.totalSizeBytes > 0L) formatBytes(model.totalSizeBytes) else "metadata only"
                    Text(
                        text = "Size: $sizeStr",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )
                    Spacer(Modifier.width(16.dp))
                    Text(
                        text = if (model.isDownloadable) "Downloadable" else "Metadata only",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )
                }
            }
        }
    }

    @Composable
    private fun InstalledTabContent() {
        if (installedModelsState.isEmpty()) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text(
                    text = "No models installed. Import model files or download from store.",
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        } else {
            LazyColumn(
                verticalArrangement = Arrangement.spacedBy(12.dp),
                modifier = Modifier.fillMaxSize()
            ) {
                items(installedModelsState) { model ->
                    val isActive = modelCatalog.getActiveModel()?.id == model.id
                    InstalledModelCard(
                        model = model,
                        isActive = isActive,
                        onUse = {
                            if (modelCatalog.setActiveModel(model.id)) {
                                applyActiveModelSelection()
                                reinitializeNativeIfIdle()
                            }
                        },
                        onDelete = {
                            showDeleteModelConfirmationDialog(model)
                        }
                    )
                }
            }
        }
    }

    @Composable
    private fun InstalledModelCard(
        model: InstalledModel,
        isActive: Boolean,
        onUse: () -> Unit,
        onDelete: () -> Unit
    ) {
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(14.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f)
            ),
            border = BorderStroke(
                1.dp,
                if (isActive) MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
                else MaterialTheme.colorScheme.outline.copy(alpha = 0.08f)
            )
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                text = model.title,
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            if (isActive) {
                                Spacer(Modifier.width(8.dp))
                                Box(
                                    modifier = Modifier
                                        .clip(RoundedCornerShape(4.dp))
                                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.15f))
                                        .padding(horizontal = 6.dp, vertical = 2.dp)
                                ) {
                                    Text(
                                        text = "ACTIVE",
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.primary,
                                        fontWeight = FontWeight.Bold
                                    )
                                }
                            }
                        }
                        Spacer(Modifier.height(4.dp))
                        Text(
                            text = model.description,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    
                    Spacer(Modifier.width(16.dp))
                    
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        if (!isActive) {
                            OutlinedButton(
                                onClick = onUse,
                                shape = RoundedCornerShape(10.dp),
                                contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp)
                            ) {
                                Text("Use", style = MaterialTheme.typography.labelMedium)
                            }
                            
                            Spacer(Modifier.width(8.dp))
                        }
                        
                        if (model.id != "asset-default") {
                            IconButton(onClick = onDelete) {
                                Icon(
                                    imageVector = Icons.Filled.Delete,
                                    contentDescription = "Delete Model",
                                    tint = MaterialTheme.colorScheme.error
                                )
                            }
                        }
                    }
                }
                
                Spacer(Modifier.height(10.dp))
                
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Start,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    val sizeStr = if (model.totalSizeBytes > 0L) formatBytes(model.totalSizeBytes) else "n/a"
                    Text(
                        text = "Size: $sizeStr",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )
                    Spacer(Modifier.width(16.dp))
                    Text(
                        text = "Source: ${model.source.name.lowercase()}",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f)
                    )
                }
            }
        }
    }

    @Composable
    private fun statusAccentFor(statusText: String, isRunning: Boolean): Color {
        val lower = statusText.lowercase()
        return when {
            isRunning || lower.contains("running") -> StatusGreen
            lower.contains("fail") -> StatusRed
            lower.contains("wait") || lower.contains("loading") || lower.contains("starting") || lower.contains("stopping") -> StatusAmber
            else -> StatusGrey
        }
    }

    private fun beginAsyncRootCheck(onCompleted: ((Boolean) -> Unit)? = null) {
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
                    Log.i(TAG, "Root available  -  initializing aimbot")
                    if (statusTextState != "Status: Init Failed") {
                        if (nativeInitAimbot()) {
                            Log.i(TAG, "Aimbot initialized successfully")
                            if (nativeGetTouchBackend() == 0) {
                                showAppToast("Root granted! Aimbot enabled.", false)
                            }
                        } else {
                            Log.w(TAG, "Aimbot init failed after root grant")
                            showAppToast("Root granted but aimbot init failed. Check /dev/uinput.", true)
                        }
                    }
                } else {
                    Log.w(TAG, "Root denied or unavailable")
                }
                onCompleted?.invoke(hasRoot)
            }
        }
    }
}
