package com.aimbuddy

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.fail
import org.junit.Test
import java.io.IOException

/** The store's index comes from the network, so what the app accepts from it has to be strict. */
class ModelStoreIndexTest {

    private val baseUrl = "https://raw.githubusercontent.com/1337Xcode/AimBuddy/master/model"
    private val sha = "a".repeat(64)

    private fun file(path: String, size: Long = 1024, hash: String = sha): JSONObject =
        JSONObject().put("path", path).put("size", size).put("sha256", hash)

    private fun entry(id: String, title: String = "Model $id", version: Int = 1, fileDir: String = id): JSONObject =
        JSONObject()
            .put("id", id)
            .put("title", title)
            .put("description", "What it is for")
            .put("author", "Someone")
            .put("version", version)
            .put("param", file("$fileDir/model.param"))
            .put("bin", file("$fileDir/model.bin"))
            .put("profile", file("$fileDir/model.json"))

    private fun index(vararg entries: JSONObject, schema: Int = 1): String =
        JSONObject().put("schema", schema).put("models", JSONArray(entries.toList())).toString()

    private fun idsIn(text: String) = ModelStoreRepository.parseIndex(text, baseUrl).map { it.id }

    @Test
    fun readsAModelAndBuildsItsAddresses() {
        val model = ModelStoreRepository.parseIndex(index(entry("general")), baseUrl).single()
        assertEquals("general", model.id)
        assertEquals("Model general", model.title)
        assertEquals(1, model.version)
        assertEquals("$baseUrl/general/model.param", model.param.url)
        assertEquals("$baseUrl/general/model.bin", model.bin.url)
        assertEquals(3L * 1024, model.totalSizeBytes)
        assertNull(model.icon)
    }

    @Test
    fun listsModelsByTitle() {
        val text = index(entry("b-model", title = "Zebra"), entry("a-model", title = "apple"), entry("c-model", title = "Mango"))
        assertEquals(listOf("a-model", "c-model", "b-model"), idsIn(text))
    }

    @Test
    fun oneDamagedEntryDoesNotHideTheOthers() {
        val broken = entry("broken").apply { remove("bin") }
        val noTitle = entry("untitled").put("title", "  ")
        assertEquals(listOf("good"), idsIn(index(broken, entry("good"), noTitle)))
    }

    @Test
    fun refusesPathsThatCouldLeaveTheStore() {
        val paths = listOf("../secret/model.param", "/etc/passwd", "model/../x", "https://evil.example/model.param", "UPPER/model.param", "a//b", ".hidden/x", "")
        for (path in paths) {
            val bad = entry("bad").put("param", file(path))
            assertEquals("accepted \"$path\"", emptyList<String>(), idsIn(index(bad)))
        }
    }

    @Test
    fun refusesIdsThatAreNotFolderNames() {
        for (id in listOf("a", "Upper", "ends-with-dash-", "-starts", "has space", "dots.in.it", "x".repeat(41), "../up")) {
            assertEquals("accepted \"$id\"", emptyList<String>(), idsIn(index(entry(id, fileDir = "folder"))))
        }
        assertEquals(listOf("ab", "x-y", "x".repeat(40)), idsIn(index(entry("ab"), entry("x-y"), entry("x".repeat(40)))))
    }

    @Test
    fun refusesFilesWithoutARealSizeOrChecksum() {
        val sizes = listOf(0L, -5L, 64L * 1024 * 1024 + 1)
        for (size in sizes) {
            assertEquals(emptyList<String>(), idsIn(index(entry("big").put("bin", file("big/model.bin", size = size)))))
        }
        for (hash in listOf("", "xyz", "A".repeat(64), "a".repeat(63))) {
            assertEquals("accepted \"$hash\"", emptyList<String>(), idsIn(index(entry("hash").put("bin", file("hash/model.bin", hash = hash)))))
        }
    }

    @Test
    fun refusesAModelWithoutAVersion() {
        assertEquals(emptyList<String>(), idsIn(index(entry("zero", version = 0))))
    }

    @Test
    fun readsAnIconWhenThereIsOne() {
        val withIcon = entry("pretty").put("icon", file("pretty/icon.png", size = 2048))
        assertEquals("$baseUrl/pretty/icon.png", ModelStoreRepository.parseIndex(index(withIcon), baseUrl).single().icon!!.url)
    }

    @Test
    fun refusesAListMadeForANewerApp() {
        try {
            ModelStoreRepository.parseIndex(index(entry("general"), schema = 2), baseUrl)
            fail("accepted a newer schema")
        } catch (expected: StoreNeedsNewerAppException) {
            // the app has to be updated before it can read this store
        }
    }

    @Test
    fun refusesTextThatIsNotAnIndex() {
        for (text in listOf("", "Not Found", "[]", "{}", "{\"schema\": 1}")) {
            try {
                ModelStoreRepository.parseIndex(text, baseUrl)
                fail("accepted \"$text\"")
            } catch (expected: JSONException) {
                // the caller falls back to the saved list
            }
        }
    }
}
