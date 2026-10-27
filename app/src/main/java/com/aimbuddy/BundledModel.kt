package com.aimbuddy

import android.content.Context
import org.json.JSONObject

/**
 * The model that ships inside the app, so the first run needs no connection. The build copies it
 * from the store's `model/aimbuddy-general` folder (see `copyBundledModel` in app/build.gradle).
 */
object BundledModel {
    /** The same id the model has in the store, so the store knows the app already has it. */
    const val ID = "aimbuddy-general"

    // The names the native code loads the model by.
    const val PARAM_ASSET = "models/yolo26n-opt.param"
    const val BIN_ASSET = "models/yolo26n-opt.bin"
    private const val INFO_ASSET = "models/model.json"

    private const val FALLBACK_TITLE = "AimBuddy General"

    /** What the bundled model calls itself. */
    class Info(val title: String, val description: String, val version: Int)

    /** The bundled model's name and version, or null when this build has no model inside. */
    fun read(context: Context): Info? {
        if (!hasAsset(context, PARAM_ASSET) || !hasAsset(context, BIN_ASSET)) {
            return null
        }
        val json = runCatching {
            JSONObject(context.assets.open(INFO_ASSET).use { it.bufferedReader().readText() })
        }.getOrNull()
        return Info(
            title = json?.optString("title").orEmpty().ifBlank { FALLBACK_TITLE },
            description = json?.optString("description").orEmpty(),
            version = json?.optInt("version", 1)?.coerceAtLeast(1) ?: 1,
        )
    }

    private fun hasAsset(context: Context, path: String): Boolean = try {
        context.assets.open(path).use { }
        true
    } catch (_: Exception) {
        false
    }
}
