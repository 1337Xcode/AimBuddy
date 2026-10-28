package com.aimbuddy

import android.util.Log
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.io.IOException

/**
 * What a model says about itself, read from a small JSON file saved next to it.
 *
 * Players train their own models, so the app cannot guess what the classes mean. The file
 * has three optional fields:
 *
 * ```
 * { "roles": ["body", "head"], "endToEnd": false, "inputSize": 256 }
 * ```
 *
 * - `roles` lists what each class is, in class order: "body" (an enemy), "head" (an enemy's head)
 *   or "ignore" (anything that must not be highlighted, such as teammates).
 * - `endToEnd` is true for exports that already remove duplicate boxes (shape N x 6).
 * - `inputSize` is the network input size in pixels, 32 to 2048; leave it out to have it detected.
 *
 * Without a file, one class means "body" and two classes mean "body" then "head".
 */
data class ModelProfileSpec(
    val roles: String?,
    val endToEnd: Boolean,
    val inputSize: Int
) {
    companion object {
        private const val TAG = "ModelProfileSpec"

        /** The smallest and the largest network input, in pixels, that a description may state. */
        private const val MIN_INPUT_SIZE = 32
        private const val MAX_INPUT_SIZE = 2048

        /** Used when a model comes without a description. */
        val UNKNOWN = ModelProfileSpec(roles = null, endToEnd = false, inputSize = 0)

        /**
         * Reads the description for the model whose `.param` file is [paramPath].
         * `<name>.json` next to the `.param` file wins over `model.json` in the same folder.
         */
        fun readFor(paramPath: String?): ModelProfileSpec {
            if (paramPath.isNullOrBlank()) return UNKNOWN
            val param = File(paramPath)
            val candidates = listOf(
                File(param.parentFile, param.nameWithoutExtension + ".json"),
                File(param.parentFile, "model.json")
            )
            val file = candidates.firstOrNull { it.isFile } ?: return UNKNOWN
            return try {
                parse(file.readText())
            } catch (e: JSONException) {
                Log.w(TAG, "Ignoring unreadable model description ${file.name}: ${e.message}")
                UNKNOWN
            } catch (e: IOException) {
                Log.w(TAG, "Could not read the model description ${file.name}: ${e.message}")
                UNKNOWN
            }
        }

        /** Parses the JSON text of a description. Unknown fields are ignored. */
        fun parse(json: String): ModelProfileSpec {
            val root = JSONObject(json)
            val roles = when (val raw = root.opt("roles")) {
                is JSONArray -> (0 until raw.length()).joinToString(",") { raw.optString(it) }
                is String -> raw
                else -> null
            }
            return ModelProfileSpec(
                roles = roles?.takeIf { it.isNotBlank() },
                endToEnd = root.optBoolean("endToEnd", false),
                // A number no network input has is not a size: the detector works it out instead of allocating it.
                inputSize = root.optInt("inputSize", 0).takeIf { it in MIN_INPUT_SIZE..MAX_INPUT_SIZE } ?: 0
            )
        }
    }
}
