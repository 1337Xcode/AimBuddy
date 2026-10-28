package com.aimbuddy

import android.content.Context
import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/** Where a model on the phone came from. */
enum class ModelSource {
    LOCAL,
    STORE
}

/** A model on the phone. [version] is the store's version number; 0 for a model the player imported. */
data class InstalledModel(
    val id: String,
    val title: String,
    val description: String,
    val source: ModelSource,
    val paramPath: String,
    val binPath: String,
    val totalSizeBytes: Long,
    val version: Int = 0,
) {
    /** True when the model's files are there to be loaded. */
    fun canUse(): Boolean = File(paramPath).exists() && File(binPath).exists()
}

/**
 * The list of models on the phone and which one is in use, kept in the app's preferences. The app
 * ships with no model: every one on the list was downloaded from the store or imported.
 */
class ModelCatalog internal constructor(private val modelsDir: File, private val prefs: SharedPreferences) {

    constructor(context: Context) : this(
        File(context.filesDir, "models"),
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE),
    )

    companion object {
        // Set once the first time a catalog is used in this process; see recoverInterruptedInstalls().
        private val recovered = AtomicBoolean(false)

        // The list is read, changed and written back whole, by the downloads, the imports and the screens
        // (each with a catalog of its own), so a change takes this lock to be sure of what it read.
        private val listLock = Any()

        /**
         * The model to use: the one chosen if its files are there, otherwise the first model whose
         * files are. Null when no model can be used, which is how a phone with none looks.
         */
        fun chooseActive(models: List<InstalledModel>, chosenId: String?): InstalledModel? =
            models.firstOrNull { it.id == chosenId && it.canUse() } ?: models.firstOrNull { it.canUse() }

        private const val PREFS_NAME = "aimbuddy_prefs"
        private const val KEY_INSTALLED_MODELS_JSON = "installed_models_json"
        private const val KEY_ACTIVE_MODEL_ID = "active_model_id"
    }

    /**
     * Puts things right before the list is read: finishes what an install that was killed halfway
     * left behind, and makes the model in use one that can be loaded (none, if no model can be).
     */
    fun tidy() {
        recoverInterruptedInstalls()
        synchronized(listLock) {
            val chosenId = prefs.getString(KEY_ACTIVE_MODEL_ID, null)
            val activeId = chooseActive(loadInstalledModels(), chosenId)?.id
            if (activeId != chosenId) {
                prefs.edit().putString(KEY_ACTIVE_MODEL_ID, activeId).apply()
            }
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
    fun addOrUpdateModel(model: InstalledModel, makeActive: Boolean = true) = synchronized(listLock) {
        val models = loadInstalledModels().toMutableList()
        models.removeAll { it.id == model.id }
        models.add(model)
        saveInstalledModels(models)

        if (makeActive) {
            prefs.edit().putString(KEY_ACTIVE_MODEL_ID, model.id).apply()
        }
    }

    /** Removes a model and its files. Returns false if there was no such model. */
    fun deleteModel(modelId: String): Boolean = synchronized(listLock) {
        val models = loadInstalledModels().toMutableList()
        val model = models.firstOrNull { it.id == modelId } ?: return false

        models.remove(model)
        saveInstalledModels(models)
        deleteFiles(model)

        val activeId = prefs.getString(KEY_ACTIVE_MODEL_ID, null)
        if (activeId == modelId) {
            prefs.edit().putString(KEY_ACTIVE_MODEL_ID, chooseActive(models, null)?.id).apply()
        }
        true
    }

    /** Makes a model the one in use. Returns false if there is no such model or its files are gone. */
    fun setActiveModel(modelId: String): Boolean = synchronized(listLock) {
        val model = loadInstalledModels().firstOrNull { it.id == modelId } ?: return false
        if (!model.canUse()) {
            return false
        }
        prefs.edit().putString(KEY_ACTIVE_MODEL_ID, modelId).apply()
        true
    }

    /** The model in use, or the first one that can be used if the chosen one is gone; null when there is none at all. */
    fun getActiveModel(): InstalledModel? =
        chooseActive(loadInstalledModels(), prefs.getString(KEY_ACTIVE_MODEL_ID, null))

    /** The models on the phone that can be used, by title. */
    fun getInstalledModels(): List<InstalledModel> =
        loadInstalledModels().filter { it.canUse() }.sortedBy { it.title.lowercase() }

    /** Turns a name into something safe to use as an id and a folder name. */
    fun sanitizeModelId(raw: String): String {
        val clean = raw.lowercase().replace(Regex("[^a-z0-9._-]"), "-").trim('-')
        return if (clean.isBlank()) "model-${System.currentTimeMillis()}" else clean
    }

    /** Deletes a model's files, and its folder when the model has one of its own. */
    private fun deleteFiles(model: InstalledModel) {
        try {
            val file = File(model.paramPath)
            val folder = file.parentFile
            if (folder != null && folder.name == model.id && folder.parentFile?.name == "models") {
                folder.deleteRecursively()
            } else {
                file.delete()
            }
            File(model.binPath).delete()
        } catch (_: Exception) {
            // The files are only leftovers once the model is off the list.
        }
    }

    private fun loadInstalledModels(): List<InstalledModel> {
        val raw = prefs.getString(KEY_INSTALLED_MODELS_JSON, "[]") ?: "[]"
        // A damaged list means no models are known; the ones downloaded can be downloaded again, and imported ones imported again.
        val arr = try {
            JSONArray(raw)
        } catch (e: JSONException) {
            return emptyList()
        }
        val out = mutableListOf<InstalledModel>()
        for (i in 0 until arr.length()) {
            val obj = arr.optJSONObject(i) ?: continue
            val paramPath = obj.optString("paramPath")
            val binPath = obj.optString("binPath")
            // An earlier build kept the model it shipped with on this list, with no files of its own.
            if (paramPath.isBlank() || binPath.isBlank()) {
                continue
            }
            out.add(
                InstalledModel(
                    id = obj.optString("id"),
                    title = obj.optString("title"),
                    description = obj.optString("description"),
                    source = if (obj.optString("source") == ModelSource.STORE.name) ModelSource.STORE else ModelSource.LOCAL,
                    paramPath = paramPath,
                    binPath = binPath,
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
            obj.put("paramPath", model.paramPath)
            obj.put("binPath", model.binPath)
            obj.put("totalSizeBytes", model.totalSizeBytes)
            obj.put("version", model.version)
            arr.put(obj)
        }
        prefs.edit().putString(KEY_INSTALLED_MODELS_JSON, arr.toString()).apply()
    }
}
