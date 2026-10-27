package com.aimbuddy.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.ExperimentalTextApi
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontVariation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import com.aimbuddy.R

/** Geist, one variable font file that provides every weight the app uses. */
@OptIn(ExperimentalTextApi::class)
val Geist = FontFamily(
    Font(R.font.geist_variable, FontWeight.Normal, variationSettings = FontVariation.Settings(FontVariation.weight(400))),
    Font(R.font.geist_variable, FontWeight.Medium, variationSettings = FontVariation.Settings(FontVariation.weight(500))),
    Font(R.font.geist_variable, FontWeight.SemiBold, variationSettings = FontVariation.Settings(FontVariation.weight(600))),
    Font(R.font.geist_variable, FontWeight.Bold, variationSettings = FontVariation.Settings(FontVariation.weight(700))),
)

private fun style(size: Float, line: Float, weight: FontWeight, tracking: Float = 0f) = TextStyle(
    fontFamily = Geist,
    fontSize = size.sp,
    lineHeight = line.sp,
    fontWeight = weight,
    letterSpacing = tracking.em,
)

/**
 * The type scale. Material's names are mapped to the roles the screens use:
 * headlineLarge is the large title, titleLarge a section title, titleMedium a row's title,
 * bodyMedium the supporting line under it and bodySmall the small print.
 */
val AppTypography = Typography(
    displayLarge = style(48f, 52f, FontWeight.Bold, -0.02f),
    headlineLarge = style(34f, 41f, FontWeight.Bold, -0.015f),
    headlineMedium = style(28f, 34f, FontWeight.Bold, -0.01f),
    headlineSmall = style(22f, 28f, FontWeight.SemiBold, -0.005f),
    titleLarge = style(20f, 26f, FontWeight.SemiBold),
    titleMedium = style(17f, 22f, FontWeight.Medium),
    titleSmall = style(15f, 20f, FontWeight.Medium),
    bodyLarge = style(17f, 24f, FontWeight.Normal),
    bodyMedium = style(15f, 21f, FontWeight.Normal),
    bodySmall = style(13f, 18f, FontWeight.Normal),
    labelLarge = style(17f, 22f, FontWeight.SemiBold),
    labelMedium = style(13f, 18f, FontWeight.Medium, 0.01f),
    labelSmall = style(12f, 16f, FontWeight.Medium, 0.02f),
)
