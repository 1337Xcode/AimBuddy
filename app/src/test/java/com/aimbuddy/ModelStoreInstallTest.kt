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
import java.io.InputStream
import java.security.MessageDigest
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

/** Installing a model from the store: the bytes that arrive, a download that is cancelled, and one that arrives damaged. */
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
    fun aCancelledDownloadLeavesNothingBehindAndTheOldModelAsItWas() {
        val target = File(folder.root, "models/test")
        target.mkdirs()
        File(target, "model.param").writeText("the old model")
        var asked = 0

        try {
            serving().install(model, target, isCancelled = { ++asked > 3 }) { _, _ -> }
            fail("the install should have been cancelled")
        } catch (expected: DownloadCancelledException) {
            // the way a stop is told
        }

        assertEquals("the old model", File(target, "model.param").readText())
        assertFalse(File(folder.root, "models/test$DOWNLOAD_SUFFIX").exists())
    }

    @Test
    fun aReadThatIsWaitingOnAQuietConnectionIsEndedWhenTheDownloadIsAborted() {
        val target = File(folder.root, "models/test")
        val reading = CountDownLatch(1)
        val stalled = object : InputStream() {
            private val closed = CountDownLatch(1)
            override fun read(): Int = throw UnsupportedOperationException()
            override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
                reading.countDown()
                closed.await()   // nothing arrives, for as long as the connection is open
                throw IOException("Socket closed")
            }
            override fun close() = closed.countDown()
        }
        val repository = ModelStoreRepository(baseUrl, folder.newFolder("cache"), { stalled }) { error("no index") }
        var cancelled = false
        var outcome: Throwable? = null
        val install = thread {
            outcome = try {
                repository.install(model, target, isCancelled = { cancelled }) { _, _ -> }
                null
            } catch (e: Throwable) {
                e
            }
        }

        assertTrue(reading.await(5, TimeUnit.SECONDS))
        cancelled = true
        repository.abort()
        install.join(5_000)

        assertFalse("the install should have ended", install.isAlive)
        assertTrue("it ended with $outcome", outcome is DownloadCancelledException)
        assertFalse(target.exists())
    }

    @Test
    fun aFileThatDoesNotMatchItsChecksumIsRefusedAndNothingIsInstalled() {
        val target = File(folder.root, "models/test")
        val damaged = serving { url -> if (url.endsWith("model.bin")) ByteArray(binBytes.size) { 7 } else paramBytes }

        try {
            damaged.install(model, target) { _, _ -> }
            fail("a damaged download must not be installed")
        } catch (expected: IOException) {
            assertEquals(TransferFailure.DAMAGED, TransferFailure.of(expected))
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
            assertEquals(TransferFailure.DAMAGED, TransferFailure.of(expected))
        }

        assertFalse(target.exists())
    }
}
