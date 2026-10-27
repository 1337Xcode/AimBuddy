package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException

/** A model that works must survive a download that fails, and a copy that lost its files must not hide the bundled one. */
class ModelFilesTest {

    @get:Rule
    val folder = TemporaryFolder()

    private fun modelFolder(name: String, content: String): File =
        folder.newFolder(name).also { File(it, "model.param").writeText(content) }

    private fun installed(version: Int, param: File? = null, bin: File? = null) = InstalledModel(
        id = BundledModel.ID,
        title = "AimBuddy General",
        description = "",
        source = ModelSource.STORE,
        paramPath = param?.path,
        binPath = bin?.path,
        totalSizeBytes = 0,
        version = version,
    )

    private val bundled = BundledModel.Info(title = "AimBuddy General", description = "", version = 2)

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
    fun aDownloadedCopyStaysOnlyWhenItsFilesAreThereAndItIsAsNew() {
        val param = folder.newFile("model.param")
        val bin = folder.newFile("model.bin")
        assertTrue(ModelCatalog.keepsDownloadedCopy(installed(2, param, bin), bundled))
        assertTrue(ModelCatalog.keepsDownloadedCopy(installed(3, param, bin), bundled))
        assertFalse("older", ModelCatalog.keepsDownloadedCopy(installed(1, param, bin), bundled))
        assertFalse("no files", ModelCatalog.keepsDownloadedCopy(installed(2), bundled))
        assertFalse("files gone", ModelCatalog.keepsDownloadedCopy(installed(2, File(folder.root, "gone.param"), bin), bundled))
        assertFalse("no copy", ModelCatalog.keepsDownloadedCopy(null, bundled))
    }
}
