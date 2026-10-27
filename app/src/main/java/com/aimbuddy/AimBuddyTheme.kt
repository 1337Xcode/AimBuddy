package com.aimbuddy

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

/** The colors that tell the state of the assist. They are always paired with a word, never alone. */
internal object StatusColors {
    val Green = Color(0xFF34D399)
    val Amber = Color(0xFFFBBF24)
    val Red = Color(0xFFF87171)
    val Grey = Color(0xFF6B7280)
}

private val AimBuddyColors = darkColorScheme(
    primary = Color(0xFF8AB4F8),
    onPrimary = Color(0xFF0B1220),
    secondary = Color(0xFF93C5FD),
    background = Color(0xFF0B0F17),
    surface = Color(0xFF111722),
    surfaceVariant = Color(0xFF1B2230),
    onBackground = Color(0xFFE5E7EB),
    onSurface = Color(0xFFE5E7EB),
    onSurfaceVariant = Color(0xFF9CA3AF),
)

/** The app's theme: one dark palette shared by every screen. */
@Composable
fun AimBuddyTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = AimBuddyColors, content = content)
}
