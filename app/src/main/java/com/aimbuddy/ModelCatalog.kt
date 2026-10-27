package com.aimbuddy

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

enum class ModelSource {
    ASSET,
    LOCAL,
    STORE
}

data class InstalledModel(
    val id: String,
    val title: String,
    val description: String,
    val source: ModelSource,
    val paramPath: String?,
    val binPath: String?,
    val totalSizeBytes: Long
) {
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

class ModelCatalog(private val context: Context) {
    companion object {
        private const val PREFS_NAME = "aimbuddy_prefs"
        private const val KEY_INSTALLED_MODELS_JSON = "installed_models_json"
        private const val KEY_ACTIVE_MODEL_ID = "active_model_id"

        private const val ASSET_MODEL_ID = "asset-default"
        private const val ASSET_MODEL_TITLE = "AimBuddy standard"
        private const val ASSET_MODEL_DESCRIPTION = "Built into the app."
    }

    private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun ensureDefaultAssetModel(hasAssetParam: Boolean, hasAssetBin: Boolean) {
        val models = loadInstalledModels().toMutableList()
        models.removeAll { it.id == ASSET_MODEL_ID }

        if (hasAssetParam && hasAssetBin) {
            models.add(
                InstalledModel(
                    id = ASSET_MODEL_ID,
                    title = ASSET_MODEL_TITLE,
                    description = ASSET_MODEL_DESCRIPTION,
                    source = ModelSource.ASSET,
                    paramPath = null,
                    binPath = null,
                    totalSizeBytes = 0L
                )
            )
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

    fun addOrUpdateModel(model: InstalledModel, makeActive: Boolean = true) {
        val models = loadInstalledModels().toMutableList()
        models.removeAll { it.id == model.id }
        models.add(model)
        saveInstalledModels(models)

        if (makeActive) {
            prefs.edit().putString(KEY_ACTIVE_MODEL_ID, model.id).apply()
        }
    }

    fun deleteModel(modelId: String): Boolean {
        if (modelId == ASSET_MODEL_ID) return false
        val models = loadInstalledModels().toMutableList()
        val model = models.firstOrNull { it.id == modelId } ?: return false
        
        models.remove(model)
        saveInstalledModels(models)

        try {
            model.paramPath?.let { path ->
                val f = File(path)
                if (f.exists()) f.delete()
                val parentDir = f.parentFile
                if (parentDir != null && parentDir.name == modelId && parentDir.parentFile?.name == "models") {
                    parentDir.deleteRecursively()
                }
            }
            model.binPath?.let { path ->
                val f = File(path)
                if (f.exists()) f.delete()
            }
        } catch (_: Exception) {}

        val activeId = prefs.getString(KEY_ACTIVE_MODEL_ID, null)
        if (activeId == modelId) {
            val fallback = models.firstOrNull { it.source == ModelSource.ASSET }
                ?: models.firstOrNull { it.canUse() }
            prefs.edit().putString(KEY_ACTIVE_MODEL_ID, fallback?.id).apply()
        }
        return true
    }

    fun setActiveModel(modelId: String): Boolean {
        val model = loadInstalledModels().firstOrNull { it.id == modelId } ?: return false
        if (!model.canUse()) {
            return false
        }
        prefs.edit().putString(KEY_ACTIVE_MODEL_ID, modelId).apply()
        return true
    }

    fun getActiveModel(): InstalledModel? {
        val models = loadInstalledModels()
        val activeId = prefs.getString(KEY_ACTIVE_MODEL_ID, null)
        val active = models.firstOrNull { it.id == activeId && it.canUse() }
        if (active != null) {
            return active
        }
        return models.firstOrNull { it.source == ModelSource.ASSET } ?: models.firstOrNull { it.canUse() }
    }

    fun getInstalledModels(): List<InstalledModel> {
        return loadInstalledModels().filter { it.canUse() || it.source == ModelSource.ASSET }
            .sortedWith(compareBy<InstalledModel> { it.source != ModelSource.ASSET }.thenBy { it.title.lowercase() })
    }

    fun sanitizeModelId(raw: String): String {
        val clean = raw.lowercase().replace(Regex("[^a-z0-9._-]"), "-").trim('-')
        return if (clean.isBlank()) "model-${System.currentTimeMillis()}" else clean
    }

    private fun loadInstalledModels(): List<InstalledModel> {
        val raw = prefs.getString(KEY_INSTALLED_MODELS_JSON, "[]") ?: "[]"
        val arr = JSONArray(raw)
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
                    totalSizeBytes = obj.optLong("totalSizeBytes", 0L)
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
            arr.put(obj)
        }
        prefs.edit().putString(KEY_INSTALLED_MODELS_JSON, arr.toString()).apply()
    }
}
