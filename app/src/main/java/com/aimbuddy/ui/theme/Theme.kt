package com.aimbuddy.ui.theme

import androidx.annotation.StringRes
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.shape.RoundedCornerShape
import com.aimbuddy.R

/** Whether the app follows the phone's light or dark setting, or always uses one. */
enum class ThemeMode { SYSTEM, DARK, LIGHT }

/**
 * The color the app uses for buttons, switches and highlights. Each one has a bright form for the
 * dark theme and a deeper form for the light theme, so text and marks on it stay readable in both.
 */
enum class Accent(@StringRes val label: Int, val dark: Color, val light: Color) {
    BLUE(R.string.color_blue, Color(0xFF0A84FF), Color(0xFF007AFF)),
    INDIGO(R.string.color_indigo, Color(0xFF5E5CE6), Color(0xFF5856D6)),
    PURPLE(R.string.color_purple, Color(0xFFBF5AF2), Color(0xFF8944AB)),
    PINK(R.string.color_pink, Color(0xFFFF375F), Color(0xFFD30F45)),
    RED(R.string.color_red, Color(0xFFFF453A), Color(0xFFD70015)),
    ORANGE(R.string.color_orange, Color(0xFFFF9F0A), Color(0xFFC93400)),
    YELLOW(R.string.color_yellow, Color(0xFFFFD60A), Color(0xFF8A6A00)),
    GREEN(R.string.color_green, Color(0xFF30D158), Color(0xFF248A3D)),
    CYAN(R.string.color_cyan, Color(0xFF64D2FF), Color(0xFF0071A4)),
    GRAPHITE(R.string.color_graphite, Color(0xFF98989D), Color(0xFF636366)),
}

/** Black on a light color and white on a dark one, whichever reads better. */
private fun contentOn(color: Color): Color = if (color.luminance() > 0.4f) Color.Black else Color.White

/**
 * The colors every screen draws with, beyond what Material has a slot for. Grouped lists sit on a
 * `background` and are made of `surface` cards; `raised` is for controls inside a card.
 */
data class Palette(
    val background: Color,
    val surface: Color,
    val raised: Color,
    val separator: Color,
    val text: Color,
    val textDim: Color,
    val accent: Color,
    val onAccent: Color,
    val danger: Color,
    val success: Color,
    val warning: Color,
    val isDark: Boolean,
)

private fun paletteFor(accent: Accent, dark: Boolean): Palette = if (dark) {
    Palette(
        background = Color(0xFF000000),
        surface = Color(0xFF1C1C1E),
        raised = Color(0xFF2C2C2E),
        separator = Color(0xFF38383A),
        text = Color(0xFFFFFFFF),
        textDim = Color(0xFF8E8E93),
        accent = accent.dark,
        onAccent = contentOn(accent.dark),
        danger = Color(0xFFFF453A),
        success = Color(0xFF30D158),
        warning = Color(0xFFFFD60A),
        isDark = true,
    )
} else {
    Palette(
        background = Color(0xFFF2F2F7),
        surface = Color(0xFFFFFFFF),
        raised = Color(0xFFE9E9EF),
        separator = Color(0xFFD1D1D6),
        text = Color(0xFF000000),
        textDim = Color(0xFF6C6C70),
        accent = accent.light,
        onAccent = contentOn(accent.light),
        danger = Color(0xFFFF3B30),
        success = Color(0xFF248A3D),
        warning = Color(0xFFB25000),
        isDark = false,
    )
}

/**
 * Spacing, in a Fibonacci sequence: each step is about 1.6 times the one before, the golden ratio.
 * Using only these keeps every gap on every screen in proportion with the others.
 */
object Space {
    val hairline: Dp = 1.dp
    val xs: Dp = 4.dp
    val s: Dp = 8.dp
    val m: Dp = 13.dp
    val l: Dp = 21.dp
    val xl: Dp = 34.dp
    val xxl: Dp = 55.dp
}

private val LocalPalette = staticCompositionLocalOf { paletteFor(Accent.BLUE, dark = true) }

/** The palette of the current theme. */
object AppTheme {
    val palette: Palette
        @Composable
        @ReadOnlyComposable
        get() = LocalPalette.current
}

private fun materialColors(p: Palette): ColorScheme {
    val base = if (p.isDark) darkColorScheme() else lightColorScheme()
    return base.copy(
        primary = p.accent,
        onPrimary = p.onAccent,
        secondary = p.accent,
        background = p.background,
        onBackground = p.text,
        surface = p.surface,
        onSurface = p.text,
        surfaceVariant = p.raised,
        onSurfaceVariant = p.textDim,
        outline = p.separator,
        outlineVariant = p.separator,
        error = p.danger,
        onError = Color.White,
        surfaceContainer = p.surface,
        surfaceContainerHigh = p.surface,
        surfaceContainerHighest = p.raised,
    )
}

private val AppShapes = Shapes(
    extraSmall = RoundedCornerShape(8.dp),
    small = RoundedCornerShape(13.dp),
    medium = RoundedCornerShape(21.dp),
    large = RoundedCornerShape(34.dp),
    extraLarge = RoundedCornerShape(55.dp),
)

/** Applies the app's look: colors from the chosen mode and accent, Geist type, soft corners. */
@Composable
fun AimBuddyTheme(mode: ThemeMode, accent: Accent, content: @Composable () -> Unit) {
    val dark = when (mode) {
        ThemeMode.SYSTEM -> isSystemInDarkTheme()
        ThemeMode.DARK -> true
        ThemeMode.LIGHT -> false
    }
    val palette = paletteFor(accent, dark)
    CompositionLocalProvider(LocalPalette provides palette) {
        MaterialTheme(
            colorScheme = materialColors(palette),
            typography = AppTypography,
            shapes = AppShapes,
            content = content,
        )
    }
}
