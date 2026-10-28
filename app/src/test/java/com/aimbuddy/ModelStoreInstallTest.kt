package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayInputStream
import java.io.File
import java.io.IOException
import java.security.MessageDigest

/** Installing a model from the store: the bytes that arrive, and a download that arrives damaged. */
class ModelStoreInstallTest {

    @get:Rule
    val folder = TemporaryFolder()

    private val baseUrl = "https://example.invalid/model"

    private val paramBytes = ByteArray(40_000) { (it % 251).toByte() }
    private val binBytes = ByteArray(300_000) { (it % 241).toByte() }
    private val profileBytes = "{\"roles\":[\"body\"]}".toByteArray()

    private fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    private fun storeFile(name: String, bytes: ByteArray) =
        StoreFile(url = "$baseUrl/test/$name", sizeBytes = bytes.size.toLong(), sha256 = sha256(bytes))

    private val model = StoreModelDefinition(
        id = "test",
        title = "Test",
        description = "",
        author = "",
        version = 1,
        icon = null,
        param = storeFile("model.param", paramBytes),
        bin = storeFile("model.bin", binBytes),
        profile = storeFile("model.json", profileBytes),
    )

    private fun serving(bytesOf: (String) -> ByteArray = { url ->
        when {
            url.endsWith("model.param") -> paramBytes
            url.endsWith("model.bin") -> binBytes
            else -> profileBytes
        }
    }) = ModelStoreRepository(baseUrl, folder.newFolder("cache"), { url -> ByteArrayInputStream(bytesOf(url)) }) { error("no index") }

    @Test
    fun theThreeFilesAreInstalledAndTheBytesThatArrivedAreReportedUpToTheTotal() {
        val target = File(folder.root, "models/test")
        val seen = mutableListOf<Pair<Long, Long>>()

        val files = serving().install(model, target) { done, total -> seen += done to total }

        assertEquals(paramBytes.size.toLong(), files.param.length())
        assertEquals(binBytes.size.toLong(), files.bin.length())
        assertTrue(File(target, "model.json").isFile)
        val total = (paramBytes.size + binBytes.size + profileBytes.size).toLong()
        assertTrue(seen.all { it.second == total })
        assertEquals(seen.map { it.first }.sorted(), seen.map { it.first })   // never goes back
        assertEquals(total, seen.last().first)
    }

    @Test
    fun aFileThatDoesNotMatchItsChecksumIsRefusedAndNothingIsInstalled() {
        val target = File(folder.root, "models/test")
        val damaged = serving { url -> if (url.endsWith("model.bin")) ByteArray(binBytes.size) { 7 } else paramBytes }

        try {
            damaged.install(model, target) { _, _ -> }
            fail("a damaged download must not be installed")
        } catch (expected: IOException) {
            // the way a damaged file is refused
        }

        assertFalse(target.exists())
        assertFalse(File(folder.root, "models/test$DOWNLOAD_SUFFIX").exists())
    }

    @Test
    fun aFileLargerThanTheStoreSaysIsRefusedWhileItArrives() {
        val target = File(folder.root, "models/test")
        val tooBig = serving { url -> if (url.endsWith("model.param")) ByteArray(paramBytes.size + 10) else binBytes }

        try {
            tooBig.install(model, target) { _, _ -> }
            fail("a file larger than listed must not be installed")
        } catch (expected: IOException) {
            // the way a file that is too big is refused
        }

        assertFalse(target.exists())
    }
}
