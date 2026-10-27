package com.aimbuddy

import android.content.Context
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/** Where a model on the phone came from. */
enum class ModelSource {
    ASSET,
    LOCAL,
    STORE
}

/** A model on the phone. [version] is the store's version number; 0 for a model the player imported. */
data class InstalledModel(
    val id: String,
    val title: String,
    val description: String,
    val source: ModelSource,
    val paramPath: String?,
    val binPath: String?,
    val totalSizeBytes: Long,
    val version: Int = 0,
) {
    /** True when the model's files are there to be loaded. */
    fun canUse(): Boolean {
        return when (source) {
            ModelSource.ASSET -> true
            ModelSource.LOCAL,
            ModelSource.STORE -> {
                !paramPath.isNullOrBlank() && !binPath.isNullOrBlank() &&
                    File(paramPath).exists() && File(binPath).exists()
            }
        }
    }
}

/**
 * The list of models on the phone and which one is in use, kept in the app's preferences. The model
 * that ships inside the app is always on the list; the others are downloaded or imported.
 */
class ModelCatalog(context: Context) {
    private val modelsDir = File(context.filesDir, "models")

    companion object {
        // Set once the first time a catalog is used in this process; see recoverInterruptedInstalls().
        private val recovered = AtomicBoolean(false)

        /**
         * Whether a downloaded copy of the bundled model stays in place of the bundled one: only if its
         * files are there and it is at least as new. A copy that lost its files gives way to the bundled one.
         */
        fun keepsDownloadedCopy(downloaded: InstalledModel?, bundled: BundledModel.Info): Boolean =
            downloaded != null && downloaded.canUse() && downloaded.version >= bundled.version

        private const val PREFS_NAME = "aimbuddy_prefs"
        private const val KEY_INSTALLED_MODELS_JSON = "installed_models_json"
        private const val KEY_ACTIVE_MODEL_ID = "active_model_id"
    }

    private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    /**
     * Puts the model that ships inside the app on the list, or takes it off when this build has none.
     * A downloaded copy of the same model stays in its place as long as it is at least as new.
     */
    fun ensureBundledModel(bundled: BundledModel.Info?) {
        recoverInterruptedInstalls()
        val models = loadInstalledModels().toMutableList()
        val downloaded = models.firstOrNull { it.id == BundledModel.ID && it.source != ModelSource.ASSET }
        // The entry is rebuilt from what the app says now. This also drops the one an older version
        // of the app saved under another id.
        models.removeAll { it.source == ModelSource.ASSET }

        if (bundled != null) {
            if (!keepsDownloadedCopy(downloaded, bundled)) {
                if (downloaded != null) {
                    models.remove(downloaded)
                    deleteFiles(downloaded)
                }
                models.add(
                    InstalledModel(
                        id = BundledModel.ID,
                        title = bundled.title,
                        description = bundled.description,
                        source = ModelSource.ASSET,
                        paramPath = null,
                        binPath = null,
                        totalSizeBytes = 0L,
                        version = bundled.version,
                    )
                )
            }
        }

        saveInstalledModels(models)

        val activeId = prefs.getString(KEY_ACTIVE_MODEL_ID, null)
        val activeStillValid = models.any { it.id == activeId && it.canUse() }
        if (!activeStillValid) {
            val fallback = models.firstOrNull { it.source == ModelSource.ASSET }
                ?: models.firstOrNull { it.canUse() }
            prefs.edit().putString(KEY_ACTIVE_MODEL_ID, fallback?.id).apply()
        }
    }

    /**
     * Clears what an install that was killed halfway left behind: a half-downloaded folder is
     * deleted, and an old model that was moved aside but never replaced is put back. It runs once
     * per process, when no install of this process can be under way yet.
     */
    private fun recoverInterruptedInstalls() {
        if (!recovered.compareAndSet(false, true)) {
            return
        }
        modelsDir.listFiles()?.forEach { folder ->
            when {
                folder.name.endsWith(DOWNLOAD_SUFFIX) -> folder.deleteRecursively()
                folder.name.endsWith(PREVIOUS_SUFFIX) -> {
                    val target = File(modelsDir, folder.name.removeSuffix(PREVIOUS_SUFFIX))
                    if (target.exists()) folder.deleteRecursively() else folder.renameTo(target)
                }
            }
        }
    }

    /** Adds a model, or replaces the one with the same id, and optionally makes it the one in use. */
    fun addOrUpdateModel(model: InstalledModel, makeActive: Boolean = true) {
        val models = loadInstalledModels().toMutableList()
        models.removeAll { it.id == model.id }
        models.add(model)
        saveInstalledModels(models)

        if (makeActive) {
            prefs.edit().putString(KEY_ACTIVE_MODEL_ID, model.id).apply()
        }
    }

    /** Removes a model and its files. The one inside the app cannot be removed. Returns false if nothing was removed. */
    fun deleteModel(modelId: String): Boolean {
        val models = loadInstalledModels().toMutableList()
        val model = models.firstOrNull { it.id == modelId } ?: return false
        if (model.source == ModelSource.ASSET) return false

        models.remove(model)
        saveInstalledModels(models)
        deleteFiles(model)

        val activeId = prefs.getString(KEY_ACTIVE_MODEL_ID, null)
        if (activeId == modelId) {
            val fallback = models.firstOrNull { it.source == ModelSource.ASSET }
                ?: models.firstOrNull { it.canUse() }
            prefs.edit().putString(KEY_ACTIVE_MODEL_ID, fallback?.id).apply()
        }
        return true
    }

    /** Makes a model the one in use. Returns false if there is no such model or its files are gone. */
    fun setActiveModel(modelId: String): Boolean {
        val model = loadInstalledModels().firstOrNull { it.id == modelId } ?: return false
        if (!model.canUse()) {
            return false
        }
        prefs.edit().putString(KEY_ACTIVE_MODEL_ID, modelId).apply()
        return true
    }

    /** The model in use, or the one inside the app if the chosen one is gone; null when there is none at all. */
    fun getActiveModel(): InstalledModel? {
        val models = loadInstalledModels()
        val activeId = prefs.getString(KEY_ACTIVE_MODEL_ID, null)
        val active = models.firstOrNull { it.id == activeId && it.canUse() }
        if (active != null) {
            return active
        }
        return models.firstOrNull { it.source == ModelSource.ASSET } ?: models.firstOrNull { it.canUse() }
    }

    /** The models on the phone, the one inside the app first. */
    fun getInstalledModels(): List<InstalledModel> {
        return loadInstalledModels().filter { it.canUse() || it.source == ModelSource.ASSET }
            .sortedWith(compareBy<InstalledModel> { it.source != ModelSource.ASSET }.thenBy { it.title.lowercase() })
    }

    /** Turns a name into something safe to use as an id and a folder name. */
    fun sanitizeModelId(raw: String): String {
        val clean = raw.lowercase().replace(Regex("[^a-z0-9._-]"), "-").trim('-')
        return if (clean.isBlank()) "model-${System.currentTimeMillis()}" else clean
    }

    /** Deletes a model's files, and its folder when the model has one of its own. */
    private fun deleteFiles(model: InstalledModel) {
        try {
            model.paramPath?.let { path ->
                val file = File(path)
                val folder = file.parentFile
                if (folder != null && folder.name == model.id && folder.parentFile?.name == "models") {
                    folder.deleteRecursively()
                } else {
                    file.delete()
                }
            }
            model.binPath?.let { File(it).delete() }
        } catch (_: Exception) {
            // The files are only leftovers once the model is off the list.
        }
    }

    private fun loadInstalledModels(): List<InstalledModel> {
        val raw = prefs.getString(KEY_INSTALLED_MODELS_JSON, "[]") ?: "[]"
        // A damaged list means no models are known; the one inside the app is put back, and imported ones can be imported again.
        val arr = try {
            JSONArray(raw)
        } catch (e: JSONException) {
            return emptyList()
        }
        val out = mutableListOf<InstalledModel>()
        for (i in 0 until arr.length()) {
            val obj = arr.optJSONObject(i) ?: continue
            val source = when (obj.optString("source")) {
                ModelSource.ASSET.name -> ModelSource.ASSET
                ModelSource.STORE.name -> ModelSource.STORE
                else -> ModelSource.LOCAL
            }
            out.add(
                InstalledModel(
                    id = obj.optString("id"),
                    title = obj.optString("title"),
                    description = obj.optString("description"),
                    source = source,
                    paramPath = obj.optString("paramPath").ifBlank { null },
                    binPath = obj.optString("binPath").ifBlank { null },
                    totalSizeBytes = obj.optLong("totalSizeBytes", 0L),
                    version = obj.optInt("version", 0),
                )
            )
        }
        return out
    }

    private fun saveInstalledModels(models: List<InstalledModel>) {
        val arr = JSONArray()
        for (model in models) {
            val obj = JSONObject()
            obj.put("id", model.id)
            obj.put("title", model.title)
            obj.put("description", model.description)
            obj.put("source", model.source.name)
            obj.put("paramPath", model.paramPath ?: "")
            obj.put("binPath", model.binPath ?: "")
            obj.put("totalSizeBytes", model.totalSizeBytes)
            obj.put("version", model.version)
            arr.put(obj)
        }
        prefs.edit().putString(KEY_INSTALLED_MODELS_JSON, arr.toString()).apply()
    }
}
