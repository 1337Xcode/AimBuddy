package com.aimbuddy

import android.util.Log
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.security.MessageDigest

private const val TAG = "ModelStore"

/** What an install adds to a model's folder name while it is under way: the files being downloaded, and the old ones moved aside. */
internal const val DOWNLOAD_SUFFIX = ".download"
internal const val PREVIOUS_SUFFIX = ".previous"

/**
 * Puts the downloaded folder in place of [targetDir], keeping the old one until the new one is there,
 * so a failure at any step loses nothing.
 */
internal fun swapIn(downloaded: File, targetDir: File) {
    val previous = File(targetDir.parentFile, targetDir.name + PREVIOUS_SUFFIX)
    previous.deleteRecursively()
    if (targetDir.exists() && !targetDir.renameTo(previous)) {
        throw IOException("Could not move the old model out of the way")
    }
    if (!downloaded.renameTo(targetDir)) {
        previous.renameTo(targetDir)
        throw IOException("Could not move the model into place")
    }
    previous.deleteRecursively()
}

/** One file of a store model: where it is, how big it must be and the SHA-256 it must have. */
data class StoreFile(val url: String, val sizeBytes: Long, val sha256: String)

/** A model the store offers, as the store's index describes it. */
data class StoreModelDefinition(
    val id: String,
    val title: String,
    val description: String,
    val author: String,
    val version: Int,
    val icon: StoreFile?,
    val param: StoreFile,
    val bin: StoreFile,
    val profile: StoreFile,
) {
    val totalSizeBytes: Long
        get() = param.sizeBytes + bin.sizeBytes + profile.sizeBytes
}

/** The files of a model that was just installed. */
class InstalledFiles(val param: File, val bin: File)

/** The models the store lists, and whether this is the copy saved by an earlier visit because the store could not be reached now. */
data class StoreList(val models: List<StoreModelDefinition>, val saved: Boolean)

/**
 * The store's list is written for a newer AimBuddy than this one. Not an [IOException]: the store was reached and
 * read, so showing the copy saved by an earlier visit as if there were no connection would hide the real reason.
 */
class StoreNeedsNewerAppException : Exception("The store needs a newer version of AimBuddy")

/**
 * Reads the store: the models published in the project's `model` folder on GitHub, and the files of
 * each. The list is one small file (`index.json`), so opening the store costs a single request, and
 * the last copy is kept so the store still opens without a connection.
 *
 * Everything that comes from the network is checked before it is used: the index must be well
 * formed, file paths must stay inside the store, and every downloaded file must have exactly the
 * size and SHA-256 the index gives for it.
 */
class ModelStoreRepository(
    private val baseUrl: String,
    private val cacheDir: File,
    /** How the bytes of a file are read; the network by default. The one who asked for the stream closes it. */
    private val openFile: (String) -> InputStream = { Http.openStream(it) },
    /** How the text of an address is read; the network by default. */
    private val readText: (String) -> String = { Http.getText(it) },
) {

    /** The stream a download is reading, so that [abort] can end a read that is waiting for a connection that has gone quiet. */
    @Volatile
    private var reading: InputStream? = null

    /**
     * Ends the read a download is waiting in, for the player who has asked it to stop: a read on a stalled connection
     * would else go on for as long as the connection's timeout. The install ends with a [DownloadCancelledException].
     */
    fun abort() {
        try {
            reading?.close()
        } catch (e: IOException) {
            Log.w(TAG, "Could not close the download: ${e.message}")
        }
    }

    /** Fetches the list of models. Without a connection the copy saved by the last success is used, and says so. */
    fun fetchModels(): StoreList {
        val cache = File(cacheDir, INDEX_FILE)
        val text: String
        val models: List<StoreModelDefinition>
        try {
            text = readText("$baseUrl/$INDEX_FILE")
            models = parseIndex(text, baseUrl)
        } catch (e: IOException) {
            return fallBackToSavedList(cache, e)
        } catch (e: JSONException) {
            return fallBackToSavedList(cache, e)
        }
        // A copy that cannot be kept (a full disk) must not hide the list that was just read.
        try {
            saveText(cache, text)
        } catch (e: IOException) {
            Log.w(TAG, "Could not keep a copy of the store's list: ${e.message}")
        }
        return StoreList(models, saved = false)
    }

    /** Reads the list saved by the last success, or rethrows [cause] when there is none. */
    private fun fallBackToSavedList(cache: File, cause: Exception): StoreList {
        if (!cache.isFile) {
            throw IOException("The store could not be read: ${cause.message}", cause)
        }
        Log.w(TAG, "Using the saved list of models: ${cause.message}")
        return StoreList(parseIndex(cache.readText(), baseUrl), saved = true)
    }

    /**
     * Downloads a model into [targetDir], replacing what is there. The files go into a folder beside
     * it first and are checked one by one, so a broken or altered download never replaces a model
     * that works. [onProgress] gets the bytes that have arrived in all and the bytes there will be, each
     * time more arrive. Between two reads of the network [isCancelled] is asked: when it says true the
     * install ends with a [DownloadCancelledException] and leaves nothing behind.
     */
    fun install(
        model: StoreModelDefinition,
        targetDir: File,
        isCancelled: () -> Boolean = { false },
        onProgress: (bytesDone: Long, bytesTotal: Long) -> Unit,
    ): InstalledFiles {
        val parent = targetDir.parentFile ?: throw IOException("There is no place to put the model")
        val work = File(parent, targetDir.name + DOWNLOAD_SUFFIX)
        work.deleteRecursively()
        if (!work.mkdirs()) {
            throw IOException("Could not create ${work.path}")
        }
        var finished = false
        try {
            val total = model.totalSizeBytes.coerceAtLeast(1L)
            var doneBytes = 0L
            val steps = listOf(model.param to PARAM_FILE, model.bin to BIN_FILE, model.profile to PROFILE_FILE)
            for ((file, name) in steps) {
                download(file, File(work, name), isCancelled) { read -> onProgress(doneBytes + read, total) }
                doneBytes += file.sizeBytes
            }
            swapIn(work, targetDir)
            finished = true
            return InstalledFiles(File(targetDir, PARAM_FILE), File(targetDir, BIN_FILE))
        } finally {
            if (!finished) {
                work.deleteRecursively()
            }
        }
    }

    /**
     * The picture of a model, downloaded once and kept. Returns null when the model has none or it
     * could not be fetched; the screens then draw a tile with the model's first letter instead.
     */
    fun iconFor(model: StoreModelDefinition): File? {
        val icon = model.icon ?: return null
        val file = File(cacheDir, "icons/${model.id}-${icon.sha256.take(12)}.png")
        if (file.isFile) {
            return file
        }
        // Through a temporary file of its own, so a download that was cut short is never taken for the picture, and two
        // visits to the store that fetch the same picture at once do not write into one file.
        var temporary: File? = null
        return try {
            file.parentFile?.mkdirs()
            temporary = File.createTempFile("${model.id}-", ".part", file.parentFile)
            download(icon, temporary, { false }, abortable = false) { }
            if (temporary.renameTo(file) || file.isFile) file else null
        } catch (e: IOException) {
            Log.w(TAG, "The icon of ${model.id} could not be fetched: ${e.message}")
            null
        } finally {
            temporary?.delete()
        }
    }

    /**
     * Downloads one file to [destination] and checks it against the size and SHA-256 the index gave. An [abortable]
     * download is the one [abort] closes; a picture fetched alongside it must not take that place from it.
     */
    private fun download(
        file: StoreFile,
        destination: File,
        isCancelled: () -> Boolean,
        abortable: Boolean = true,
        onBytes: (Long) -> Unit,
    ) {
        val digest = MessageDigest.getInstance("SHA-256")
        var received = 0L
        try {
            openFile(file.url).use { input ->
                if (abortable) {
                    reading = input
                }
                destination.outputStream().use { output ->
                    val buffer = ByteArray(BUFFER_BYTES)
                    while (true) {
                        if (isCancelled()) {
                            throw DownloadCancelledException()
                        }
                        val count = try {
                            input.read(buffer)
                        } catch (e: IOException) {
                            // A read that abort() ended is the player's stop, not a broken connection.
                            throw if (isCancelled()) DownloadCancelledException() else e
                        }
                        if (count < 0) {
                            break
                        }
                        received += count
                        if (received > file.sizeBytes) {
                            throw IOException("${destination.name} is larger than the store says")
                        }
                        digest.update(buffer, 0, count)
                        output.write(buffer, 0, count)
                        onBytes(received)
                    }
                }
            }
        } finally {
            if (abortable) {
                reading = null
            }
        }
        if (received != file.sizeBytes || toHex(digest.digest()) != file.sha256) {
            throw IOException("${destination.name} does not match the store's checksum")
        }
    }

    /** Saves text next to the final file first, then renames it, so a failed write never damages the old copy. */
    private fun saveText(destination: File, text: String) {
        destination.parentFile?.mkdirs()
        val temporary = File(destination.path + ".tmp")
        temporary.writeText(text)
        if (!temporary.renameTo(destination)) {
            temporary.delete()
        }
    }

    companion object {
        const val PARAM_FILE = "model.param"
        const val BIN_FILE = "model.bin"
        const val PROFILE_FILE = "model.json"

        private const val INDEX_FILE = "index.json"
        private const val SUPPORTED_SCHEMA = 1
        private const val BUFFER_BYTES = 32 * 1024
        private const val MAX_FILE_BYTES = 64L * 1024 * 1024

        /** A model's id: what its folder is called in the store. Two to 40 characters, and a dash is never the last. */
        private val ID_PATTERN = Regex("[a-z0-9][a-z0-9-]{0,38}[a-z0-9]")

        /** A path inside the store: lowercase words joined by slashes, never leading out of it. */
        private val PATH_PATTERN = Regex("[a-z0-9][a-z0-9._-]*(/[a-z0-9][a-z0-9._-]*)*")
        private val SHA256_PATTERN = Regex("[0-9a-f]{64}")

        /**
         * Reads the text of `index.json`. A model whose entry is damaged is skipped with a warning, so
         * one bad entry cannot hide the rest; a list the app is too old to read is a [StoreNeedsNewerAppException].
         */
        fun parseIndex(text: String, baseUrl: String): List<StoreModelDefinition> {
            val root = JSONObject(text)
            val schema = root.getInt("schema")
            if (schema > SUPPORTED_SCHEMA) {
                throw StoreNeedsNewerAppException()
            }
            val entries = root.getJSONArray("models")
            val models = mutableListOf<StoreModelDefinition>()
            for (index in 0 until entries.length()) {
                val entry = entries.optJSONObject(index) ?: continue
                try {
                    models += parseEntry(entry, baseUrl)
                } catch (e: JSONException) {
                    Log.w(TAG, "Skipping a damaged store entry: ${e.message}")
                } catch (e: IllegalArgumentException) {
                    Log.w(TAG, "Skipping a damaged store entry: ${e.message}")
                }
            }
            return models.sortedBy { it.title.lowercase() }
        }

        /** Reads one model of the index; throws when a field is missing or not in the allowed form. */
        private fun parseEntry(json: JSONObject, baseUrl: String): StoreModelDefinition {
            val id = json.getString("id")
            require(ID_PATTERN.matches(id)) { "bad model id: $id" }
            return StoreModelDefinition(
                id = id,
                title = json.getString("title").trim().also { require(it.isNotEmpty()) { "$id has no title" } },
                description = json.optString("description").trim(),
                author = json.optString("author").trim(),
                version = json.getInt("version").also { require(it >= 1) { "$id has no version" } },
                icon = json.optJSONObject("icon")?.let { parseFile(it, baseUrl) },
                param = parseFile(json.getJSONObject("param"), baseUrl),
                bin = parseFile(json.getJSONObject("bin"), baseUrl),
                profile = parseFile(json.getJSONObject("profile"), baseUrl),
            )
        }

        /** Reads a file entry and turns its path into an address, refusing a path that could leave the store. */
        private fun parseFile(json: JSONObject, baseUrl: String): StoreFile {
            val path = json.getString("path")
            require(PATH_PATTERN.matches(path)) { "bad file path: $path" }
            val size = json.getLong("size")
            require(size in 1..MAX_FILE_BYTES) { "bad file size for $path" }
            val sha256 = json.getString("sha256")
            require(SHA256_PATTERN.matches(sha256)) { "bad checksum for $path" }
            return StoreFile(url = "$baseUrl/$path", sizeBytes = size, sha256 = sha256)
        }

        private fun toHex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it) }
    }
}
