package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.util.concurrent.CountDownLatch
import kotlin.concurrent.thread

/** The list of models is read, changed and written back; two changes made at the same time must both survive. */
class ModelCatalogTest {

    @get:Rule
    val folder = TemporaryFolder()

    private fun modelWithFiles(id: String): InstalledModel {
        val param = File(folder.root, "$id.param").also { it.writeText("param") }
        val bin = File(folder.root, "$id.bin").also { it.writeText("bin") }
        return InstalledModel(
            id = id,
            title = "Model $id",
            description = "",
            source = ModelSource.STORE,
            paramPath = param.path,
            binPath = bin.path,
            totalSizeBytes = 8,
            version = 1,
        )
    }

    @Test
    fun modelsAddedAtTheSameTimeAllStayOnTheList() {
        val catalog = ModelCatalog(File(folder.root, "models"), InMemoryPreferences())
        val perThread = 150
        val models = (0 until 2 * perThread).map { modelWithFiles("model-$it") }
        val together = CountDownLatch(1)

        val adders = listOf(models.take(perThread), models.drop(perThread)).map { share ->
            thread {
                together.await()
                share.forEach { catalog.addOrUpdateModel(it, makeActive = false) }
            }
        }
        together.countDown()
        adders.forEach { it.join() }

        assertEquals(models.map { it.id }.sorted(), catalog.getInstalledModels().map { it.id }.sorted())
    }

    @Test
    fun aModelAddedAgainReplacesItself() {
        val catalog = ModelCatalog(File(folder.root, "models"), InMemoryPreferences())
        val first = modelWithFiles("general")

        catalog.addOrUpdateModel(first)
        catalog.addOrUpdateModel(first.copy(title = "Newer", version = 2))

        val listed = catalog.getInstalledModels().single()
        assertEquals("Newer", listed.title)
        assertEquals(2, listed.version)
        assertTrue(catalog.getActiveModel()?.id == "general")
    }
}
