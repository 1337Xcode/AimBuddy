package com.aimbuddy.ui

import android.app.Application
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Log
import androidx.annotation.StringRes
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.aimbuddy.InstalledModel
import com.aimbuddy.ModelCatalog
import com.aimbuddy.ModelSource
import com.aimbuddy.ModelStoreRepository
import com.aimbuddy.NativeBridge
import com.aimbuddy.R
import com.aimbuddy.StoreModelDefinition
import com.aimbuddy.ui.theme.Accent
import com.aimbuddy.ui.theme.ThemeMode
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

/** A short message for the player, shown once. */
data class UiMessage(@StringRes val resId: Int, val args: List<Any> = emptyList())

/** How the app's look is set. Kept in the app's preferences. */
data class Appearance(val mode: ThemeMode = ThemeMode.SYSTEM, val accent: Accent = Accent.BLUE)

/** What the models screens show about the store. */
sealed interface StoreState {
    data object Idle : StoreState
    data object Loading : StoreState
    data class Ready(val models: List<StoreModelDefinition>) : StoreState
    data object Failed : StoreState
}

/** What the models screens show. `busyId` is the model being downloaded or imported, if any. */
data class ModelsState(
    val installed: List<InstalledModel> = emptyList(),
    val activeId: String? = null,
    val busyId: String? = null,
    val store: StoreState = StoreState.Idle,
)

/** The settings as the native side keeps them, read by name. */
class SettingsView(private val values: Map<String, String> = emptyMap()) {
    fun float(name: String, fallback: Float): Float = values[name]?.toFloatOrNull() ?: fallback
    fun int(name: String, fallback: Int): Int = values[name]?.toIntOrNull() ?: fallback
    fun bool(name: String, fallback: Boolean): Boolean = values[name]?.let { it == "1" } ?: fallback

    companion object {
        /** Reads the `name=value` lines the native side returns. */
        fun parse(snapshot: String): SettingsView = SettingsView(
            snapshot.lineSequence()
                .mapNotNull { line -> line.indexOf('=').takeIf { it > 0 }?.let { line.substring(0, it) to line.substring(it + 1).trim() } }
                .toMap()
        )
    }
}

/** Which ways of reaching the touchscreen are ready right now. */
data class TouchAccess(val shizuku: Boolean = false, val root: Boolean = false)

/**
 * Everything the screens show that is not about the running assist: the models, the settings and the
 * app's look. It outlives the activity, so rotating the phone or changing the language loses nothing.
 */
class AppViewModel(application: Application) : AndroidViewModel(application) {

    private val context: Context = application
    private val catalog = ModelCatalog(application)
    private val storeRepository = ModelStoreRepository(owner = STORE_OWNER, repo = STORE_REPO, branch = STORE_BRANCH)
    private val preferences = application.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    private val _appearance = MutableStateFlow(readAppearance())
    val appearance: StateFlow<Appearance> = _appearance.asStateFlow()

    private val _settings = MutableStateFlow(SettingsView())
    val settings: StateFlow<SettingsView> = _settings.asStateFlow()

    private val _models = MutableStateFlow(ModelsState())
    val models: StateFlow<ModelsState> = _models.asStateFlow()

    private val _touchAccess = MutableStateFlow(TouchAccess())
    val touchAccess: StateFlow<TouchAccess> = _touchAccess.asStateFlow()

    private val _messages = MutableSharedFlow<UiMessage>(extraBufferCapacity = 4)
    val messages: SharedFlow<UiMessage> = _messages.asSharedFlow()

    private var pendingParam: Uri? = null
    private var pendingName = "model"

    init {
        refreshModels()
        // Loading the native library takes a moment, so it is not done while the first screen is drawn.
        viewModelScope.launch(Dispatchers.Default) {
            NativeBridge.loadSettings(File(application.filesDir, SETTINGS_FILE).path)
            _settings.value = SettingsView.parse(NativeBridge.settingsSnapshot())
        }
    }

    /** Shows a short message to the player. */
    fun post(message: UiMessage) {
        _messages.tryEmit(message)
    }

    // ---------------------------------------------------------------------------------------
    // Look
    // ---------------------------------------------------------------------------------------

    fun setThemeMode(mode: ThemeMode) = updateAppearance { it.copy(mode = mode) }

    fun setAccent(accent: Accent) = updateAppearance { it.copy(accent = accent) }

    private fun updateAppearance(change: (Appearance) -> Appearance) {
        val updated = change(_appearance.value)
        _appearance.value = updated
        preferences.edit()
            .putString(KEY_THEME_MODE, updated.mode.name)
            .putString(KEY_ACCENT, updated.accent.name)
            .apply()
    }

    private fun readAppearance(): Appearance = Appearance(
        mode = runCatching { ThemeMode.valueOf(preferences.getString(KEY_THEME_MODE, null) ?: "") }.getOrDefault(ThemeMode.SYSTEM),
        accent = runCatching { Accent.valueOf(preferences.getString(KEY_ACCENT, null) ?: "") }.getOrDefault(Accent.BLUE),
    )

    // ---------------------------------------------------------------------------------------
    // Settings
    // ---------------------------------------------------------------------------------------

    /** Re-reads the settings; the assist's menu may have changed them since the screen last looked. */
    fun refreshSettings() {
        viewModelScope.launch(Dispatchers.Default) {
            _settings.value = SettingsView.parse(NativeBridge.settingsSnapshot())
        }
    }

    fun setSetting(name: String, value: String) {
        NativeBridge.setSetting(name, value)
        _settings.value = SettingsView.parse(NativeBridge.settingsSnapshot())
    }

    /** Changes several settings at once, so the screens redraw once. */
    fun setSettings(values: List<Pair<String, String>>) {
        values.forEach { (name, value) -> NativeBridge.setSetting(name, value) }
        _settings.value = SettingsView.parse(NativeBridge.settingsSnapshot())
    }

    fun setSetting(name: String, value: Boolean) = setSetting(name, if (value) "1" else "0")

    fun setSetting(name: String, value: Float) = setSetting(name, value.toString())

    fun setSetting(name: String, value: Int) = setSetting(name, value.toString())

    fun resetSettings() {
        NativeBridge.resetSettings()
        _settings.value = SettingsView.parse(NativeBridge.settingsSnapshot())
    }

    // ---------------------------------------------------------------------------------------
    // Touch access
    // ---------------------------------------------------------------------------------------

    fun updateTouchAccess(shizuku: Boolean, root: Boolean) {
        _touchAccess.value = TouchAccess(shizuku, root)
    }

    // ---------------------------------------------------------------------------------------
    // Models
    // ---------------------------------------------------------------------------------------

    /** Re-reads which models are installed and which one is in use. */
    fun refreshModels() {
        val hasParam = assetExists(ASSET_MODEL_PARAM)
        val hasBin = assetExists(ASSET_MODEL_BIN)
        catalog.ensureDefaultAssetModel(hasParam, hasBin)
        migrateLegacyModel()
        _models.update {
            it.copy(installed = catalog.getInstalledModels(), activeId = catalog.getActiveModel()?.id)
        }
    }

    fun useModel(id: String) {
        if (catalog.setActiveModel(id)) {
            refreshModels()
            _messages.tryEmit(UiMessage(R.string.models_next_start))
        }
    }

    fun deleteModel(id: String) {
        catalog.deleteModel(id)
        refreshModels()
    }

    /** Loads the store's list of models in the background. */
    fun loadStore() {
        if (_models.value.store is StoreState.Loading) {
            return
        }
        _models.update { it.copy(store = StoreState.Loading) }
        viewModelScope.launch {
            val result = withContext(Dispatchers.IO) { runCatching { storeRepository.fetchAvailableModels() } }
            _models.update {
                it.copy(store = result.fold({ list -> StoreState.Ready(list) }, { StoreState.Failed }))
            }
            result.exceptionOrNull()?.let { Log.w(TAG, "The store could not be read: ${it.message}") }
        }
    }

    /** Downloads a model from the store and makes it the one in use. */
    fun downloadModel(model: StoreModelDefinition) {
        if (_models.value.busyId != null) {
            return
        }
        _models.update { it.copy(busyId = model.id) }
        viewModelScope.launch {
            val installed = withContext(Dispatchers.IO) {
                runCatching {
                    val id = catalog.sanitizeModelId(model.id)
                    val files = storeRepository.downloadModel(model, File(context.filesDir, "models/$id"))
                    InstalledModel(
                        id = id,
                        title = model.title,
                        description = model.description,
                        source = ModelSource.STORE,
                        paramPath = files.first.absolutePath,
                        binPath = files.second.absolutePath,
                        totalSizeBytes = files.first.length() + files.second.length(),
                    )
                }
            }
            installed.onSuccess {
                catalog.addOrUpdateModel(it, makeActive = true)
                _messages.tryEmit(UiMessage(R.string.models_next_start))
            }.onFailure {
                Log.w(TAG, "Download failed: ${it.message}")
                _messages.tryEmit(UiMessage(R.string.models_store_error))
            }
            _models.update { it.copy(busyId = null) }
            refreshModels()
        }
    }

    /** Remembers the `.param` file the player picked, until the matching `.bin` file arrives. */
    fun onParamPicked(uri: Uri) {
        pendingParam = uri
        pendingName = displayName(uri).substringBeforeLast('.', "model")
    }

    /** Copies the picked `.param` and `.bin` into the app's storage and makes the pair the model in use. */
    fun onBinPicked(binUri: Uri) {
        val paramUri = pendingParam ?: return
        pendingParam = null
        val name = pendingName
        viewModelScope.launch {
            val installed = withContext(Dispatchers.IO) {
                runCatching {
                    val id = catalog.sanitizeModelId("local-$name-${System.currentTimeMillis()}")
                    val dir = File(context.filesDir, "models/$id")
                    val paramFile = File(dir, "$id.param")
                    val binFile = File(dir, "$id.bin")
                    check(copy(paramUri, paramFile) && copy(binUri, binFile)) { "The files could not be copied" }
                    InstalledModel(
                        id = id,
                        title = name,
                        description = "",
                        source = ModelSource.LOCAL,
                        paramPath = paramFile.absolutePath,
                        binPath = binFile.absolutePath,
                        totalSizeBytes = paramFile.length() + binFile.length(),
                    )
                }
            }
            installed.onSuccess {
                catalog.addOrUpdateModel(it, makeActive = true)
                _messages.tryEmit(UiMessage(R.string.models_next_start))
            }.onFailure {
                Log.w(TAG, "Import failed: ${it.message}")
                _messages.tryEmit(UiMessage(R.string.models_import_failed))
            }
            refreshModels()
        }
    }

    private fun copy(from: Uri, to: File): Boolean {
        val directory = to.parentFile ?: return false
        if (!directory.exists() && !directory.mkdirs()) {
            return false
        }
        return context.contentResolver.openInputStream(from)?.use { input ->
            FileOutputStream(to).use { output -> input.copyTo(output) }
            true
        } ?: false
    }

    private fun displayName(uri: Uri): String {
        var name = "model"
        context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) {
                name = cursor.getString(index) ?: name
            }
        }
        return name
    }

    private fun assetExists(path: String): Boolean = try {
        context.assets.open(path).use { }
        true
    } catch (_: Exception) {
        false
    }

    /** A model saved by an older version of the app as a single pair of paths is moved into the catalog. */
    private fun migrateLegacyModel() {
        val param = preferences.getString(KEY_LEGACY_PARAM, null)
        val bin = preferences.getString(KEY_LEGACY_BIN, null)
        if (param.isNullOrBlank() || bin.isNullOrBlank() || !File(param).exists() || !File(bin).exists()) {
            return
        }
        catalog.addOrUpdateModel(
            InstalledModel(
                id = catalog.sanitizeModelId("legacy-local-model"),
                title = "Earlier import",
                description = "",
                source = ModelSource.LOCAL,
                paramPath = param,
                binPath = bin,
                totalSizeBytes = File(param).length() + File(bin).length(),
            ),
            makeActive = false,
        )
        preferences.edit().remove(KEY_LEGACY_PARAM).remove(KEY_LEGACY_BIN).apply()
    }

    private companion object {
        const val TAG = "AppViewModel"
        const val PREFS_NAME = "aimbuddy_prefs"
        const val KEY_THEME_MODE = "theme_mode"
        const val KEY_ACCENT = "accent"
        const val KEY_LEGACY_PARAM = "model_param_path"
        const val KEY_LEGACY_BIN = "model_bin_path"
        const val ASSET_MODEL_PARAM = "models/yolo26n-opt.param"
        const val ASSET_MODEL_BIN = "models/yolo26n-opt.bin"
        const val SETTINGS_FILE = "settings.cfg"
        const val STORE_OWNER = "1337Xcode"
        const val STORE_REPO = "AimBuddy"
        const val STORE_BRANCH = "master"
    }
}
