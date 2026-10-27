package com.aimbuddy

import android.Manifest
import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionConfig
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.OpenableColumns
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import rikka.shizuku.Shizuku
import java.io.File
import java.io.FileOutputStream
import kotlin.concurrent.thread

/**
 * The app's screen: starts and stops the assist, shows what it is doing, and manages detection models.
 *
 * It does no assist work itself. The assist runs in [AssistService], so closing this screen leaves
 * it running over the game, and opening it again shows where things stand. What this screen does
 * own are the permission dialogs, which must come from an activity: the overlay permission,
 * notifications, Shizuku and screen capture, in that order.
 */
class MainActivity : AppCompatActivity() {

    // What the Compose screens show.
    private var activeModelTextState by mutableStateOf("Model: Auto")
    private var activeModelIdState by mutableStateOf<String?>(null)
    private var installedModelsState by mutableStateOf<List<InstalledModel>>(emptyList())
    private var showStoreState by mutableStateOf(false)
    private var isFetchingStoreState by mutableStateOf(false)
    private var storeModelsState by mutableStateOf<List<StoreModelDefinition>>(emptyList())
    private var downloadingModelIdState by mutableStateOf<String?>(null)
    private var shizukuReadyState by mutableStateOf(false)
    private var rootReadyState by mutableStateOf(false)

    private var pendingImportParamUri: Uri? = null
    private var pendingImportName: String = "local-model"

    private lateinit var modelCatalog: ModelCatalog
    private val storeRepository = ModelStoreRepository(
        owner = STORE_OWNER,
        repo = STORE_REPO,
        branch = STORE_BRANCH,
    )

    private val shizukuBinderReceivedListener = Shizuku.OnBinderReceivedListener {
        runOnUiThread { refreshTouchAccessState() }
    }
    private val shizukuBinderDeadListener = Shizuku.OnBinderDeadListener {
        runOnUiThread { refreshTouchAccessState() }
    }
    private val shizukuPermissionListener = Shizuku.OnRequestPermissionResultListener { requestCode, grantResult ->
        if (requestCode != REQUEST_SHIZUKU_PERMISSION) {
            return@OnRequestPermissionResultListener
        }
        runOnUiThread {
            refreshTouchAccessState()
            if (grantResult == PackageManager.PERMISSION_GRANTED) {
                showAppToast("Shizuku permission granted.", false)
            } else {
                showAppToast("Shizuku permission denied. Aim assist stays off; targets are still drawn.", true)
            }
            requestScreenCapture()
        }
    }

    // The permission dialogs, answered one after another by continueStartFlow().
    private val overlayPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            if (Settings.canDrawOverlays(this)) {
                continueStartFlow()
            } else {
                showAppToast("The overlay permission is required.", true)
            }
        }
    private val notificationPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) {
            // Allowed or not, the assist can run; the notification is only an easy way to stop it.
            continueStartFlow()
        }
    private val screenCaptureLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            val data = result.data
            if (result.resultCode == Activity.RESULT_OK && data != null) {
                Log.i(TAG, "Screen capture allowed")
                AssistService.start(this, result.resultCode, data)
            } else {
                showAppToast("Screen capture permission is required.", true)
            }
        }

    // Importing a model: first the .param file, then the matching .bin file.
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
        importModelFiles(paramUri, uri)
        pendingImportParamUri = null
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        WindowCompat.setDecorFitsSystemWindows(window, false)

        setContent {
            AimBuddyTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    val snapshot by AssistState.state.collectAsState()
                    Box(modifier = Modifier.fillMaxSize()) {
                        LauncherScreen(
                            snapshot = snapshot,
                            activeModelText = activeModelTextState,
                            rootReady = rootReadyState,
                            shizukuReady = shizukuReadyState,
                            onStart = { startFlow() },
                            onStop = { AssistService.stop(this@MainActivity) },
                            onImportModel = { onImportModelClicked() },
                            onOpenStore = { openModelStore() },
                            onOpenGithub = { openUrl(OSS_GITHUB_URL) },
                            onOpenCreator = { openUrl(CREATOR_URL) },
                        )
                        if (showStoreState) {
                            ModelStoreScreen(
                                state = ModelStoreState(
                                    catalog = storeModelsState,
                                    installed = installedModelsState,
                                    activeModelId = activeModelIdState,
                                    downloadingModelId = downloadingModelIdState,
                                    isFetching = isFetchingStoreState,
                                ),
                                actions = ModelStoreActions(
                                    onClose = { showStoreState = false },
                                    onImportModel = { onImportModelClicked() },
                                    onRefresh = { fetchStoreModelsAsync() },
                                    onDownload = { downloadStoreModelAsync(it) },
                                    onUse = { useModel(it) },
                                    onDelete = { showDeleteModelConfirmationDialog(it) },
                                ),
                            )
                        }
                    }
                }
            }
        }
        enableImmersiveMode()

        modelCatalog = ModelCatalog(this)
        val hasAssetParam = assetExists(ASSET_MODEL_PARAM)
        val hasAssetBin = assetExists(ASSET_MODEL_BIN)
        if (!hasAssetParam || !hasAssetBin) {
            showAppToast("No model is bundled with this build. Import or download one.", false)
        }
        modelCatalog.ensureDefaultAssetModel(hasAssetParam, hasAssetBin)
        migrateLegacySingleImportedModel()
        refreshModelState()

        // Read once per process, so a screen opened while the assist runs does not undo its changes.
        NativeBridge.loadSettings(File(filesDir, SETTINGS_FILE).path)

        Shizuku.addRequestPermissionResultListener(shizukuPermissionListener)
        Shizuku.addBinderReceivedListenerSticky(shizukuBinderReceivedListener)
        Shizuku.addBinderDeadListener(shizukuBinderDeadListener)
        maybeShowOpenSourceDialogOnce()
    }

    override fun onResume() {
        super.onResume()
        refreshTouchAccessState()
    }

    override fun onDestroy() {
        Shizuku.removeRequestPermissionResultListener(shizukuPermissionListener)
        Shizuku.removeBinderReceivedListener(shizukuBinderReceivedListener)
        Shizuku.removeBinderDeadListener(shizukuBinderDeadListener)
        // The assist is not stopped here: it belongs to the service and keeps running over the game.
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            enableImmersiveMode()
        }
    }

    /** Hides the status and navigation bars; a swipe from the edge shows them for a moment. */
    private fun enableImmersiveMode() {
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
    }

    // ---------------------------------------------------------------------------------------
    // Starting: the permission dialogs, in order
    // ---------------------------------------------------------------------------------------

    private fun startFlow() {
        if (AssistState.state.value.phase != AssistState.Phase.IDLE) {
            return
        }
        continueStartFlow()
    }

    /** Takes the next step that is still missing, then returns; each dialog's answer calls this again. */
    private fun continueStartFlow() {
        if (!Settings.canDrawOverlays(this)) {
            AlertDialog.Builder(this)
                .setTitle("Overlay Permission Required")
                .setMessage(
                    "AimBuddy needs the 'Display over other apps' permission to draw over the game.\n\n" +
                        "Tap 'Open Settings', find AimBuddy in the list and enable it, then come back and press Start."
                )
                .setPositiveButton("Open Settings") { _, _ ->
                    overlayPermissionLauncher.launch(
                        Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName"))
                    )
                }
                .setNegativeButton("Cancel", null)
                .show()
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            return
        }
        if (shizukuNeedsPermission()) {
            try {
                Shizuku.requestPermission(REQUEST_SHIZUKU_PERMISSION)
                showAppToast("Approve the Shizuku request to allow aim assist.", false)
                return
            } catch (e: Throwable) {
                Log.w(TAG, "Shizuku permission request failed: ${e.message}")
            }
        }
        requestScreenCapture()
    }

    /** True when Shizuku is running, could carry the touch helper, and has not been allowed for this app yet. */
    private fun shizukuNeedsPermission(): Boolean {
        if (NativeBridge.touchBackend() == TOUCH_BACKEND_ROOT) {
            return false
        }
        return try {
            Shizuku.pingBinder() &&
                !Shizuku.isPreV11() &&
                Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED &&
                !Shizuku.shouldShowRequestPermissionRationale()
        } catch (_: Throwable) {
            false
        }
    }

    private fun requestScreenCapture() {
        val manager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        // From Android 14 the dialog can offer to share a single app. The assist needs the whole
        // screen (the game and the overlay), so it asks for exactly that.
        val intent = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            manager.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay())
        } else {
            manager.createScreenCaptureIntent()
        }
        screenCaptureLauncher.launch(intent)
    }

    private fun refreshTouchAccessState() {
        shizukuReadyState = try {
            Shizuku.pingBinder() && Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
        } catch (_: Throwable) {
            false
        }
        rootReadyState = RootShell.isGranted()
    }

    // ---------------------------------------------------------------------------------------
    // Models
    // ---------------------------------------------------------------------------------------

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
            val modelDir = outFile.parentFile ?: return false
            if (!modelDir.exists() && !modelDir.mkdirs()) {
                Log.e(TAG, "Failed to create local model directory")
                return false
            }
            contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(outFile).use { output -> input.copyTo(output) }
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

    /** Copies the chosen `.param` and `.bin` into the app's storage and makes the pair the active model. */
    private fun importModelFiles(paramUri: Uri, binUri: Uri) {
        val modelId = modelCatalog.sanitizeModelId("local-${pendingImportName}-${System.currentTimeMillis()}")
        val installDir = File(filesDir, "models/$modelId")
        val paramFile = File(installDir, "$modelId.param")
        val binFile = File(installDir, "$modelId.bin")

        if (!copyUriToFile(paramUri, paramFile) || !copyUriToFile(binUri, binFile)) {
            showAppToast("Failed to import selected model files", true)
            return
        }
        val installed = InstalledModel(
            id = modelId,
            title = pendingImportName,
            description = "Imported from local storage",
            source = ModelSource.LOCAL,
            paramPath = paramFile.absolutePath,
            binPath = binFile.absolutePath,
            totalSizeBytes = paramFile.length() + binFile.length(),
        )
        modelCatalog.addOrUpdateModel(installed, makeActive = true)
        onModelChanged("Model imported from storage")
    }

    /** Re-reads the list of installed models and which one is active, for the screen. */
    private fun refreshModelState() {
        val active = modelCatalog.getActiveModel()
        installedModelsState = modelCatalog.getInstalledModels()
        activeModelIdState = active?.id
        activeModelTextState = if (active == null) {
            "Model: Missing"
        } else {
            "Model: ${active.title} (${active.source.name.lowercase()})"
        }
    }

    /** The active model changed; a running assist keeps its model until it is started again. */
    private fun onModelChanged(message: String) {
        refreshModelState()
        val running = AssistState.state.value.isActive
        showAppToast(if (running) "$message. It is used the next time AimBuddy starts." else message, false)
    }

    private fun useModel(modelId: String) {
        if (modelCatalog.setActiveModel(modelId)) {
            onModelChanged("Model switched")
        }
    }

    private fun onImportModelClicked() {
        try {
            importParamLauncher.launch(arrayOf("application/octet-stream", "*/*"))
        } catch (e: ActivityNotFoundException) {
            showAppToast("No file picker found on this device", true)
        }
    }

    /** Models saved by an older version of the app as a single pair of paths are moved into the catalog. */
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
            totalSizeBytes = paramFile.length() + binFile.length(),
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
                    totalSizeBytes = downloaded.first.length() + downloaded.second.length(),
                )
                modelCatalog.addOrUpdateModel(installed, makeActive = true)

                runOnUiThread {
                    downloadingModelIdState = null
                    onModelChanged("Downloaded and switched to ${model.title}")
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
                    onModelChanged("Model deleted")
                } else {
                    showAppToast("Failed to delete model", true)
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    // ---------------------------------------------------------------------------------------
    // Notices
    // ---------------------------------------------------------------------------------------

    private fun maybeShowOpenSourceDialogOnce() {
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        if (prefs.getBoolean(PREF_OSS_NOTICE_SHOWN, false)) {
            return
        }
        AlertDialog.Builder(this)
            .setTitle("AimBuddy Notice")
            .setMessage(
                "AimBuddy is completely free and open source. If you paid for this app, you were scammed.\n\n" +
                    "Repository: $OSS_GITHUB_URL"
            )
            .setPositiveButton("Open GitHub") { _, _ ->
                prefs.edit().putBoolean(PREF_OSS_NOTICE_SHOWN, true).apply()
                openUrl(OSS_GITHUB_URL)
            }
            .setNegativeButton("Cancel") { _, _ ->
                prefs.edit().putBoolean(PREF_OSS_NOTICE_SHOWN, true).apply()
            }
            .setCancelable(false)
            .show()
    }

    /** Shows a short message. Errors are also written to the log so they can be found later. */
    private fun showAppToast(message: String, isError: Boolean) {
        if (isError) {
            Log.w(TAG, message)
        }
        Toast.makeText(this, message, Toast.LENGTH_LONG).show()
    }

    private fun openUrl(url: String) {
        try {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
        } catch (e: ActivityNotFoundException) {
            showAppToast("No browser found to open $url", true)
        }
    }

    private companion object {
        const val TAG = "MainActivity"
        const val REQUEST_SHIZUKU_PERMISSION = 2001
        const val OSS_GITHUB_URL = "https://github.com/1337XCode/AimBuddy"
        const val CREATOR_URL = "https://www.darshanchheda.com"
        const val PREFS_NAME = "aimbuddy_prefs"
        const val PREF_OSS_NOTICE_SHOWN = "oss_notice_shown"
        const val PREF_MODEL_PARAM_PATH = "model_param_path"
        const val PREF_MODEL_BIN_PATH = "model_bin_path"
        const val ASSET_MODEL_PARAM = "models/yolo26n-opt.param"
        const val ASSET_MODEL_BIN = "models/yolo26n-opt.bin"
        const val SETTINGS_FILE = "settings.cfg"
        const val STORE_OWNER = "1337Xcode"
        const val STORE_REPO = "AimBuddy"
        const val STORE_BRANCH = "master"

        /** The value of the "touch route" setting that means root (see the settings in native code). */
        const val TOUCH_BACKEND_ROOT = 2
    }
}
