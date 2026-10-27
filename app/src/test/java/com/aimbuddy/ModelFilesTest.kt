package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException

/**
 * A model that works must survive a download that fails, and the model in use must always be one that
 * can be loaded: the app ships with none, so a phone can have none.
 */
class ModelFilesTest {

    @get:Rule
    val folder = TemporaryFolder()

    private fun modelFolder(name: String, content: String): File =
        folder.newFolder(name).also { File(it, "model.param").writeText(content) }

    private fun installed(id: String, param: File, bin: File) = InstalledModel(
        id = id,
        title = "Model $id",
        description = "",
        source = ModelSource.STORE,
        paramPath = param.path,
        binPath = bin.path,
        totalSizeBytes = 0,
        version = 1,
    )

    private fun modelWithFiles(id: String): InstalledModel =
        installed(id, folder.newFile("$id.param"), folder.newFile("$id.bin"))

    private fun modelWithoutFiles(id: String): InstalledModel =
        installed(id, File(folder.root, "$id-gone.param"), File(folder.root, "$id-gone.bin"))

    @Test
    fun swapReplacesTheOldModelAndLeavesNothingBehind() {
        val target = modelFolder("model", "old")
        val downloaded = modelFolder("model.download", "new")
        swapIn(downloaded, target)
        assertEquals("new", File(target, "model.param").readText())
        assertFalse(downloaded.exists())
        assertFalse(File(target.parentFile, "model.previous").exists())
    }

    @Test
    fun swapWorksWhenThereIsNoModelYet() {
        val target = File(folder.root, "model")
        swapIn(modelFolder("model.download", "new"), target)
        assertEquals("new", File(target, "model.param").readText())
    }

    @Test
    fun aFailedSwapKeepsTheOldModel() {
        val target = modelFolder("model", "old")
        try {
            swapIn(File(folder.root, "nothing-was-downloaded"), target)
            fail("moved a folder that is not there")
        } catch (expected: IOException) {
            // the model that worked is still in place
        }
        assertEquals("old", File(target, "model.param").readText())
        assertFalse(File(target.parentFile, "model.previous").exists())
    }

    @Test
    fun theChosenModelIsTheOneInUseWhenItsFilesAreThere() {
        val models = listOf(modelWithFiles("first"), modelWithFiles("second"))
        assertEquals("second", ModelCatalog.chooseActive(models, "second")?.id)
    }

    @Test
    fun aChosenModelThatLostItsFilesGivesWayToOneThatCanBeUsed() {
        val models = listOf(modelWithoutFiles("gone"), modelWithFiles("kept"))
        assertEquals("kept", ModelCatalog.chooseActive(models, "gone")?.id)
    }

    @Test
    fun withNoModelThatCanBeUsedThereIsNoModelInUse() {
        assertNull(ModelCatalog.chooseActive(emptyList(), null))
        assertNull(ModelCatalog.chooseActive(listOf(modelWithoutFiles("gone")), "gone"))
    }
}
