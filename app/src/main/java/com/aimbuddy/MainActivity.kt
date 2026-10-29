package com.aimbuddy

import android.Manifest
import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionConfig
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.util.Log
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AppCompatDelegate
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.staggeredgrid.rememberLazyStaggeredGridState
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.core.os.LocaleListCompat
import androidx.core.view.WindowCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.aimbuddy.ui.AppViewModel
import com.aimbuddy.ui.UiMessage
import com.aimbuddy.ui.home.HomeScreen
import com.aimbuddy.ui.models.ModelsScreen
import com.aimbuddy.ui.settings.SettingsActions
import com.aimbuddy.ui.settings.SettingsScreen
import com.aimbuddy.ui.touch.TouchAccessActions
import com.aimbuddy.ui.touch.TouchAccessScreen
import com.aimbuddy.ui.theme.AimBuddyTheme
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.update.UpdateProblem
import com.aimbuddy.update.UpdateState
import com.aimbuddy.update.UpdateStatus
import rikka.shizuku.Shizuku

/** The screens the app moves between. */
private enum class Screen { HOME, MODELS, SETTINGS, TOUCH }

/**
 * The app's window: it shows the screens and walks the player through the permission dialogs that
 * must come from an activity (display over other apps, notifications, Shizuku, screen capture).
 *
 * It does no assist work itself. The assist runs in [AssistService], so closing this screen leaves
 * it running over the game, and opening it again shows where things stand.
 */
class MainActivity : AppCompatActivity() {

    private val viewModel: AppViewModel by viewModels()

    // Notifications are asked for once for each press of Start. A player who says no twice is not shown the
    // dialog again, and the system answers a request at once, so asking again would loop and never reach the next step.
    private var notificationsAnswered = false

    // Dialogs that belong to the start flow.
    private var showOverlayDialog by mutableStateOf(false)
    private var showNotice by mutableStateOf(false)

    private val shizukuBinderReceivedListener = Shizuku.OnBinderReceivedListener { runOnUiThread { refreshTouchAccess() } }
    private val shizukuBinderDeadListener = Shizuku.OnBinderDeadListener { runOnUiThread { refreshTouchAccess() } }
    private val shizukuPermissionListener = Shizuku.OnRequestPermissionResultListener { requestCode, grantResult ->
        if (requestCode != REQUEST_SHIZUKU_PERMISSION && requestCode != REQUEST_SHIZUKU_PERMISSION_FROM_SCREEN) {
            return@OnRequestPermissionResultListener
        }
        runOnUiThread {
            refreshTouchAccess()
            if (grantResult != PackageManager.PERMISSION_GRANTED) {
                viewModel.post(UiMessage(R.string.shizuku_denied))
            }
            // The question asked by Start is followed by the next step of Start; the one asked on the touch access screen is not.
            if (requestCode == REQUEST_SHIZUKU_PERMISSION) {
                requestScreenCapture()
            }
        }
    }

    // The permission dialogs, answered one after another by continueStartFlow().
    private val overlayPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            if (Settings.canDrawOverlays(this)) {
                continueStartFlow()
            } else {
                viewModel.post(UiMessage(R.string.overlay_denied))
            }
        }
    private val notificationPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) {
            // Allowed or not, the assist can run; the notification is only an easy way to stop it.
            notificationsAnswered = true
            continueStartFlow()
        }
    private val screenCaptureLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            val data = result.data
            if (result.resultCode == Activity.RESULT_OK && data != null) {
                Log.i(TAG, "Screen capture allowed")
                AssistService.start(this, result.resultCode, data)
            } else {
                viewModel.post(UiMessage(R.string.capture_denied))
            }
        }

    // Back from the page where installing apps from AimBuddy is allowed: carry on with the update if it was.
    private val installPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            val ready = UpdateState.status.value as? UpdateStatus.ReadyToInstall
            if (ready != null && packageManager.canRequestPackageInstalls()) {
                viewModel.installUpdate(ready)
            }
        }

    // Importing a model: first the .param file, then the matching .bin file.
    private val importParamLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            viewModel.onParamPicked(uri)
            viewModel.post(UiMessage(R.string.models_import_hint))
            importBinLauncher.launch(arrayOf("application/octet-stream", "*/*"))
        }
    }
    private val importBinLauncher = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            viewModel.onBinPicked(uri)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            val appearance by viewModel.appearance.collectAsStateWithLifecycle()
            AimBuddyTheme(appearance.mode, appearance.accent) {
                SystemBarIcons()
                AppRoot()
            }
        }

        Shizuku.addRequestPermissionResultListener(shizukuPermissionListener)
        Shizuku.addBinderReceivedListenerSticky(shizukuBinderReceivedListener)
        Shizuku.addBinderDeadListener(shizukuBinderDeadListener)
        viewModel.checkForUpdate(manual = false)
        showNotice = !getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getBoolean(PREF_NOTICE_SHOWN, false)
    }

    override fun onResume() {
        super.onResume()
        refreshTouchAccess()
        viewModel.refreshSettings()
        viewModel.refreshModels()
    }

    override fun onStop() {
        viewModel.saveSettingsNow()
        super.onStop()
    }

    override fun onDestroy() {
        Shizuku.removeRequestPermissionResultListener(shizukuPermissionListener)
        Shizuku.removeBinderReceivedListener(shizukuBinderReceivedListener)
        Shizuku.removeBinderDeadListener(shizukuBinderDeadListener)
        // The assist is not stopped here: it belongs to the service and keeps running over the game.
        super.onDestroy()
    }

    // ---------------------------------------------------------------------------------------
    // Screens
    // ---------------------------------------------------------------------------------------

    /** Makes the status and navigation bar icons dark or light to suit the app's colors. */
    @Composable
    private fun SystemBarIcons() {
        val view = LocalView.current
        val dark = AppTheme.palette.isDark
        SideEffect {
            val controller = WindowCompat.getInsetsController(window, view)
            controller.isAppearanceLightStatusBars = !dark
            controller.isAppearanceLightNavigationBars = !dark
        }
    }

    @Composable
    private fun AppRoot() {
        val models by viewModel.models.collectAsStateWithLifecycle()
        // A phone with no model (a first run, or an update from a build that carried one inside) opens
        // on the models screen once, where the guide says how to get one.
        val opensOnModels = remember { models.installed.isEmpty() && !modelGuideShown() }
        var screen by rememberSaveable { mutableStateOf(if (opensOnModels) Screen.MODELS else Screen.HOME) }
        // The touch access screen opens from Home and from Settings, and goes back to the one it came from.
        var touchReturn by rememberSaveable { mutableStateOf(Screen.HOME) }
        // Settings is left for the touch access screen and comes back to where it was scrolled; from Home it opens at the top.
        val settingsListState = rememberLazyStaggeredGridState()
        LaunchedEffect(Unit) {
            if (opensOnModels) {
                markModelGuideShown()
            }
        }
        val snapshot by AssistState.state.collectAsStateWithLifecycle()
        val settings by viewModel.settings.collectAsStateWithLifecycle()
        val detectsByColour = settings.int("detector", 0) == 1
        val appearance by viewModel.appearance.collectAsStateWithLifecycle()
        val touchAccess by viewModel.touchAccess.collectAsStateWithLifecycle()
        val touchTest by viewModel.touchTest.collectAsStateWithLifecycle()
        val updateStatus by viewModel.updateStatus.collectAsStateWithLifecycle()
        val autoUpdateCheck by viewModel.autoUpdateCheck.collectAsStateWithLifecycle()
        val snackbar = remember { SnackbarHostState() }
        val context = LocalContext.current

        LaunchedEffect(Unit) {
            viewModel.messages.collect { message ->
                snackbar.showSnackbar(context.getString(message.resId, *message.args.toTypedArray()))
            }
        }
        LaunchedEffect(Unit) {
            // The installer opens by itself only while the player is looking at AimBuddy and the assist is off:
            // an update replaces the app, and that must never happen under a game. Otherwise the banner offers Install.
            viewModel.installRequests.collect {
                if (lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED) && !AssistState.state.value.isActive) {
                    beginInstall(it)
                }
            }
        }
        BackHandler(enabled = screen != Screen.HOME) { screen = if (screen == Screen.TOUCH) touchReturn else Screen.HOME }

        Box(modifier = Modifier.fillMaxSize()) {
            AnimatedContent(
                targetState = screen,
                transitionSpec = {
                    val forward = targetState.ordinal > initialState.ordinal
                    val offset: (Int) -> Int = { width -> if (forward) width / 5 else -width / 5 }
                    (slideInHorizontally(tween(260)) { offset(it) } + fadeIn(tween(260))) togetherWith
                        (slideOutHorizontally(tween(260)) { -offset(it) } + fadeOut(tween(160)))
                },
                label = "screen",
            ) { current ->
                when (current) {
                    Screen.HOME -> HomeScreen(
                        snapshot = snapshot,
                        modelName = models.installed.firstOrNull { it.id == models.activeId }?.title,
                        detectsByColour = detectsByColour,
                        touchAccess = touchAccess,
                        updateStatus = updateStatus,
                        onStart = {
                            if (models.activeId == null && !detectsByColour) {
                                // Nothing to detect with yet: the store is where the first model comes from.
                                screen = Screen.MODELS
                                viewModel.post(UiMessage(R.string.models_needed))
                            } else {
                                startFlow()
                            }
                        },
                        onStop = { AssistService.stop(this@MainActivity) },
                        onOpenModels = { screen = Screen.MODELS },
                        onOpenSettings = {
                            settingsListState.requestScrollToItem(0)
                            screen = Screen.SETTINGS
                        },
                        onTouchAccess = {
                            touchReturn = Screen.HOME
                            screen = Screen.TOUCH
                        },
                        onOpenProject = { openUrl(Project.URL) },
                        onUpdate = { updateAction() },
                    )
                    Screen.MODELS -> ModelsScreen(
                        state = models,
                        running = snapshot.isActive,
                        onBack = { screen = Screen.HOME },
                        onImport = { launchImport() },
                        onUse = viewModel::useModel,
                        onDelete = viewModel::deleteModel,
                        onGet = viewModel::downloadModel,
                        onCancel = viewModel::cancelDownload,
                        onLoadStore = viewModel::loadStore,
                    )
                    Screen.SETTINGS -> SettingsScreen(
                        settings = settings,
                        appearance = appearance,
                        updateStatus = updateStatus,
                        autoUpdateCheck = autoUpdateCheck,
                        actions = SettingsActions(
                            set = viewModel::setSetting,
                            setAll = viewModel::setSettings,
                            setLanguage = { index -> changeLanguage(index) },
                            setThemeMode = viewModel::setThemeMode,
                            setAccent = viewModel::setAccent,
                            reset = viewModel::resetSettings,
                            openUrl = { url -> openUrl(url) },
                            updateAction = { updateAction() },
                            setAutoUpdateCheck = viewModel::setAutoUpdateCheck,
                            openTouchAccess = {
                                touchReturn = Screen.SETTINGS
                                screen = Screen.TOUCH
                            },
                        ),
                        listState = settingsListState,
                        onBack = { screen = Screen.HOME },
                    )
                    Screen.TOUCH -> TouchAccessScreen(
                        settings = settings,
                        access = touchAccess,
                        test = touchTest,
                        assistRunning = snapshot.phase != AssistState.Phase.IDLE,
                        actions = TouchAccessActions(
                            setRoute = { viewModel.setSetting("touchBackend", it) },
                            allowShizuku = { allowShizuku() },
                            openShizuku = { openShizuku() },
                            openShizukuGuide = { openUrl(Project.SHIZUKU_GUIDE_URL) },
                            runTest = viewModel::testTouchAccess,
                        ),
                        onBack = { screen = touchReturn },
                    )
                }
            }
            SnackbarHost(hostState = snackbar, modifier = Modifier.align(Alignment.BottomCenter))
            Dialogs(homeShowing = screen == Screen.HOME)
        }
    }

    /** The dialogs of the start flow and the first-run notice. The notice waits for the home screen, so it never covers the models guide. */
    @Composable
    private fun Dialogs(homeShowing: Boolean) {
        val palette = AppTheme.palette
        if (showOverlayDialog) {
            AlertDialog(
                onDismissRequest = { showOverlayDialog = false },
                title = { Text(stringResource(R.string.overlay_title)) },
                text = { Text(stringResource(R.string.overlay_message)) },
                confirmButton = {
                    TextButton(onClick = {
                        showOverlayDialog = false
                        overlayPermissionLauncher.launch(
                            Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName"))
                        )
                    }) { Text(stringResource(R.string.overlay_open_settings)) }
                },
                dismissButton = {
                    TextButton(onClick = { showOverlayDialog = false }) { Text(stringResource(R.string.dialog_cancel)) }
                },
                containerColor = palette.surface,
                shape = MaterialTheme.shapes.medium,
            )
        }
        if (showNotice && homeShowing) {
            AlertDialog(
                onDismissRequest = { dismissNotice() },
                title = { Text(stringResource(R.string.notice_title)) },
                text = { Text(stringResource(R.string.notice_message)) },
                confirmButton = {
                    TextButton(onClick = {
                        dismissNotice()
                        openUrl(Project.URL)
                    }) { Text(stringResource(R.string.notice_open_github)) }
                },
                dismissButton = {
                    TextButton(onClick = { dismissNotice() }) { Text(stringResource(R.string.notice_dismiss)) }
                },
                containerColor = palette.surface,
                shape = MaterialTheme.shapes.medium,
            )
        }
    }

    private fun dismissNotice() {
        showNotice = false
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit().putBoolean(PREF_NOTICE_SHOWN, true).apply()
    }

    /** Whether the player has been taken to the models screen to get a first model. */
    private fun modelGuideShown(): Boolean =
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getBoolean(PREF_MODEL_GUIDE_SHOWN, false)

    private fun markModelGuideShown() {
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit().putBoolean(PREF_MODEL_GUIDE_SHOWN, true).apply()
    }

    // ---------------------------------------------------------------------------------------
    // Starting: the permission dialogs, in order
    // ---------------------------------------------------------------------------------------

    private fun startFlow() {
        if (AssistState.state.value.phase != AssistState.Phase.IDLE) {
            return
        }
        // The test has the touchscreen for a moment; a run that began now would find it taken.
        if (viewModel.touchTest.value == TouchTest.Running) {
            viewModel.post(UiMessage(R.string.touch_test_wait))
            return
        }
        notificationsAnswered = false
        continueStartFlow()
    }

    /** Takes the next step that is still missing, then returns; each dialog's answer calls this again. */
    private fun continueStartFlow() {
        if (!Settings.canDrawOverlays(this)) {
            showOverlayDialog = true
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && !notificationsAnswered &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            return
        }
        if (shizukuNeedsPermission()) {
            try {
                Shizuku.requestPermission(REQUEST_SHIZUKU_PERMISSION)
                viewModel.post(UiMessage(R.string.shizuku_ask))
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

    /** Asks Shizuku to allow AimBuddy; when Shizuku will not ask again, takes the player to its own list of apps. */
    private fun allowShizuku() {
        val canAsk = try {
            !Shizuku.isPreV11() && !Shizuku.shouldShowRequestPermissionRationale()
        } catch (e: RuntimeException) {
            Log.w(TAG, "Shizuku could not say whether it can be asked: ${e.message}")
            false
        }
        if (!canAsk) {
            viewModel.post(UiMessage(R.string.touch_allow_in_shizuku))
            openShizuku()
            return
        }
        try {
            Shizuku.requestPermission(REQUEST_SHIZUKU_PERMISSION_FROM_SCREEN)
        } catch (e: RuntimeException) {
            Log.w(TAG, "Shizuku permission request failed: ${e.message}")
            viewModel.post(UiMessage(R.string.touch_allow_in_shizuku))
            openShizuku()
        }
    }

    /** Opens the Shizuku app, where it is started and where the apps it serves are listed. */
    private fun openShizuku() {
        val launch = packageManager.getLaunchIntentForPackage(SHIZUKU_PACKAGE)
        if (launch == null) {
            openUrl(Project.SHIZUKU_GUIDE_URL)
            return
        }
        startActivity(launch)
    }

    private fun refreshTouchAccess() {
        viewModel.updateTouchAccess(shizuku = probeShizuku(packageManager), root = RootShell.status())
    }

    // ---------------------------------------------------------------------------------------
    // Small actions
    // ---------------------------------------------------------------------------------------

    private fun launchImport() {
        try {
            importParamLauncher.launch(arrayOf("application/octet-stream", "*/*"))
        } catch (e: ActivityNotFoundException) {
            viewModel.post(UiMessage(R.string.models_import_failed))
        }
    }

    /** Switches the language of the app and of the overlay menu together. */
    private fun changeLanguage(index: Int) {
        viewModel.setSetting("language", index)
        val tag = when (index) {
            1 -> "zh"
            2 -> "ru"
            else -> "en"
        }
        AppCompatDelegate.setApplicationLocales(LocaleListCompat.forLanguageTags(tag))
    }

    /** What a tap on the update row does: fetch the update, install it, or look again, as the state calls for. */
    private fun updateAction() {
        when (val status = UpdateState.status.value) {
            is UpdateStatus.Available -> viewModel.startUpdate()
            is UpdateStatus.Failed -> when {
                // Trying again cannot change which key signed the update: the way on is the releases page.
                status.problem == UpdateProblem.DIFFERENT_SIGNER -> openUrl(Project.RELEASES_PAGE_URL)
                status.info != null -> viewModel.startUpdate()
                else -> viewModel.checkForUpdate(manual = true)
            }
            is UpdateStatus.ReadyToInstall -> beginInstall(status)
            UpdateStatus.Unknown, UpdateStatus.UpToDate -> viewModel.checkForUpdate(manual = true)
            else -> Unit   // checking, downloading or installing: already on its way
        }
    }

    /** Hands a checked update to the installer, once the player has allowed AimBuddy to install apps. */
    private fun beginInstall(ready: UpdateStatus.ReadyToInstall) {
        if (packageManager.canRequestPackageInstalls()) {
            viewModel.installUpdate(ready)
            return
        }
        viewModel.post(UiMessage(R.string.update_allow_installs))
        installPermissionLauncher.launch(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:$packageName")))
    }

    private fun openUrl(url: String) {
        try {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
        } catch (e: ActivityNotFoundException) {
            viewModel.post(UiMessage(R.string.no_browser))
        }
    }

    private companion object {
        const val TAG = "MainActivity"
        const val REQUEST_SHIZUKU_PERMISSION = 2001
        const val REQUEST_SHIZUKU_PERMISSION_FROM_SCREEN = 2002
        const val PREFS_NAME = "aimbuddy_prefs"
        const val PREF_NOTICE_SHOWN = "oss_notice_shown"
        const val PREF_MODEL_GUIDE_SHOWN = "model_guide_shown"

        /** The value of the "touch access" setting that means root (see the settings in native code). */
        const val TOUCH_BACKEND_ROOT = 2
    }
}
