package com.aimbuddy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.awt.Font
import java.io.File

/** Every Chinese character the in-game menu can show must have a glyph in the font that ships for it, or the menu draws blanks. */
class MenuFontTest {

    // Unit tests run with the app module as their working directory.
    private val menuTexts = File("src/main/cpp/utils/i18n.h").readText(Charsets.UTF_8)
    private val font = Font.createFont(Font.TRUETYPE_FONT, File("src/main/assets/fonts/NotoSansSC-Subset.ttf"))

    @Test
    fun theChineseFontHasEveryCharacterTheMenuTextsUse() {
        val chinese = menuTexts.filter { it.code >= FIRST_CJK_CODE }.toSet()
        assertTrue("the menu texts hold Chinese characters", chinese.size > 100)
        val missing = chinese.filterNot { font.canDisplay(it) }
        assertEquals("characters with no glyph: ${missing.joinToString("")}", emptyList<Char>(), missing)
    }

    private companion object {
        /** The first code point of the CJK blocks. The Latin and Cyrillic texts are drawn by another font. */
        const val FIRST_CJK_CODE = 0x2E80
    }
}
