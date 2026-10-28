package com.aimbuddy

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.IOException

/** Opening the store: the newest list when there is a connection, the last good one when there is not. */
class ModelStoreFetchTest {

    @get:Rule
    val folder = TemporaryFolder()

    private val baseUrl = "https://example.invalid/model"

    private fun file(path: String): JSONObject = JSONObject().put("path", path).put("size", 1024).put("sha256", "b".repeat(64))

    private fun indexOf(vararg ids: String): String {
        val models = ids.map { id ->
            JSONObject()
                .put("id", id)
                .put("title", "Model $id")
                .put("version", 1)
                .put("param", file("$id/model.param"))
                .put("bin", file("$id/model.bin"))
                .put("profile", file("$id/model.json"))
        }
        return JSONObject().put("schema", 1).put("models", JSONArray(models)).toString()
    }

    private fun idsOf(repository: ModelStoreRepository) = repository.fetchModels().map { it.id }

    @Test
    fun aFreshListIsUsedEvenWhenACopyOfItCannotBeKept() {
        val notAFolder = folder.newFile("cache")   // a file where the folder should be: nothing can be saved in it

        val repository = ModelStoreRepository(baseUrl, notAFolder) { indexOf("fresh-model") }

        assertEquals(listOf("fresh-model"), idsOf(repository))
    }

    @Test
    fun theLastGoodListIsUsedWithoutAConnection() {
        val cache = folder.newFolder("cache")
        var online = true
        val repository = ModelStoreRepository(baseUrl, cache) {
            if (online) indexOf("saved-model") else throw IOException("no connection")
        }
        assertEquals(listOf("saved-model"), idsOf(repository))

        online = false

        assertEquals(listOf("saved-model"), idsOf(repository))
    }

    @Test
    fun withoutAConnectionOrASavedListTheStoreCannotBeRead() {
        val repository = ModelStoreRepository(baseUrl, folder.newFolder("cache")) { throw IOException("no connection") }

        try {
            repository.fetchModels()
            fail("the store should not have opened")
        } catch (expected: IOException) {
            assertEquals(true, expected.message?.startsWith("The store could not be read"))
        }
    }
}
