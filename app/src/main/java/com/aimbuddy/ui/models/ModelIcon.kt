package com.aimbuddy.ui.models

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Dp
import com.aimbuddy.ui.theme.Space

/** The pairs of colors a model's tile is drawn with: a start and an end of its gradient. */
private val TileColors = listOf(
    Color(0xFF0A84FF) to Color(0xFF5E5CE6),
    Color(0xFFBF5AF2) to Color(0xFF5E5CE6),
    Color(0xFFFF375F) to Color(0xFFFF9F0A),
    Color(0xFFFF9F0A) to Color(0xFFFF453A),
    Color(0xFF30D158) to Color(0xFF0A84FF),
    Color(0xFF64D2FF) to Color(0xFF0A84FF),
    Color(0xFF5E5CE6) to Color(0xFFBF5AF2),
    Color(0xFF636366) to Color(0xFF2C2C2E),
)

/**
 * The tile that stands for a model in a list: its first letter on a gradient that is always the same
 * for the same [id], so every model is easy to tell apart and recognise.
 */
@Composable
fun ModelIcon(id: String, title: String, modifier: Modifier = Modifier, size: Dp = Space.xxl - Space.xs) {
    val (from, to) = TileColors[(id.hashCode() and Int.MAX_VALUE) % TileColors.size]
    Box(
        modifier = modifier
            .size(size)
            .clip(RoundedCornerShape(size * 0.225f))
            .background(Brush.linearGradient(listOf(from, to))),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = title.firstOrNull { it.isLetterOrDigit() }?.uppercase() ?: "?",
            style = MaterialTheme.typography.titleLarge,
            color = Color.White,
        )
    }
}
