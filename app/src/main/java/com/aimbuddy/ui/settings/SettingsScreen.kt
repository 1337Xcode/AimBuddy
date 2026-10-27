package com.aimbuddy.ui.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aimbuddy.R
import com.aimbuddy.ui.Appearance
import com.aimbuddy.ui.SettingsView
import com.aimbuddy.ui.components.Group
import com.aimbuddy.ui.components.ListRow
import com.aimbuddy.ui.components.ScreenScaffold
import com.aimbuddy.ui.components.SegmentedRow
import com.aimbuddy.ui.components.SliderRow
import com.aimbuddy.ui.components.Swatch
import com.aimbuddy.ui.components.SwatchRow
import com.aimbuddy.ui.components.SwitchRow
import com.aimbuddy.ui.theme.Accent
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.ui.theme.Space
import com.aimbuddy.ui.theme.ThemeMode
import kotlin.math.abs
import kotlin.math.roundToInt

/** The colors offered for the boxes around targets. */
private val BoxColors = listOf(
    Triple(R.string.color_red, Color(0xFFFF3B30), floatArrayOf(1.0f, 0.23f, 0.19f)),
    Triple(R.string.color_orange, Color(0xFFFF9500), floatArrayOf(1.0f, 0.58f, 0.0f)),
    Triple(R.string.color_yellow, Color(0xFFFFD60A), floatArrayOf(1.0f, 0.84f, 0.04f)),
    Triple(R.string.color_green, Color(0xFF30D158), floatArrayOf(0.19f, 0.82f, 0.35f)),
    Triple(R.string.color_cyan, Color(0xFF64D2FF), floatArrayOf(0.39f, 0.82f, 1.0f)),
    Triple(R.string.color_blue, Color(0xFF0A84FF), floatArrayOf(0.04f, 0.52f, 1.0f)),
    Triple(R.string.color_purple, Color(0xFFBF5AF2), floatArrayOf(0.75f, 0.35f, 0.95f)),
    Triple(R.string.color_pink, Color(0xFFFF375F), floatArrayOf(1.0f, 0.22f, 0.37f)),
    Triple(R.string.color_white, Color(0xFFFFFFFF), floatArrayOf(1.0f, 1.0f, 1.0f)),
)

/** What the settings screen can change. Each call sends changes to the running app's settings. */
class SettingsActions(
    val set: (String, String) -> Unit,
    val setAll: (List<Pair<String, String>>) -> Unit,
    val setLanguage: (Int) -> Unit,
    val setThemeMode: (ThemeMode) -> Unit,
    val setAccent: (Accent) -> Unit,
    val reset: () -> Unit,
)

/**
 * Every setting, in groups. The values come from the app's settings, so what is changed here is what
 * the overlay and the assist use. Nothing needs saving: a change takes effect at once.
 */
@Composable
fun SettingsScreen(settings: SettingsView, appearance: Appearance, actions: SettingsActions, onBack: () -> Unit) {
    var confirmReset by remember { mutableStateOf(false) }

    ScreenScaffold(title = stringResource(R.string.settings_title), onBack = onBack, columnWidth = 360.dp, maxWidth = 1100.dp) {
        item { OverlayGroup(settings, actions) }
        item { AimGroup(settings, actions) }
        item { CalibrationGroup(settings, actions) }
        item { TouchGroup(settings, actions) }
        item { AppGroup(settings, appearance, actions) }
        item {
            Group {
                row {
                    ListRow(
                        title = stringResource(R.string.s_reset),
                        titleColor = AppTheme.palette.danger,
                        onClick = { confirmReset = true },
                    )
                }
            }
        }
    }

    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text(stringResource(R.string.s_reset_title)) },
            text = { Text(stringResource(R.string.s_reset_message)) },
            confirmButton = {
                TextButton(onClick = {
                    actions.reset()
                    confirmReset = false
                }) {
                    Text(stringResource(R.string.s_reset_confirm), color = AppTheme.palette.danger)
                }
            },
            dismissButton = {
                TextButton(onClick = { confirmReset = false }) { Text(stringResource(R.string.dialog_cancel)) }
            },
            containerColor = AppTheme.palette.surface,
            shape = MaterialTheme.shapes.medium,
        )
    }
}

@Composable
private fun OverlayGroup(settings: SettingsView, actions: SettingsActions) {
    val red = settings.float("boxColorR", 1f)
    val green = settings.float("boxColorG", 0.2f)
    val blue = settings.float("boxColorB", 0.2f)
    val selectedColor = BoxColors.indices.minByOrNull { index ->
        val c = BoxColors[index].third
        abs(c[0] - red) + abs(c[1] - green) + abs(c[2] - blue)
    } ?: 0

    Column(verticalArrangement = Arrangement.spacedBy(Space.l)) {
        Group(title = stringResource(R.string.settings_overlay)) {
            row {
                SwatchRow(
                    title = stringResource(R.string.s_box_color),
                    swatches = BoxColors.map { Swatch(stringResource(it.first), it.second) },
                    selected = selectedColor,
                    onSelect = { index ->
                        val c = BoxColors[index].third
                        actions.setAll(listOf("boxColorR" to c[0].toString(), "boxColorG" to c[1].toString(), "boxColorB" to c[2].toString()))
                    },
                )
            }
            row {
                val thickness = settings.int("boxThickness", 2)
                SliderRow(
                    title = stringResource(R.string.s_box_thickness),
                    value = thickness.toFloat(),
                    range = 1f..6f,
                    steps = 4,
                    valueText = "$thickness px",
                    onValueChange = { actions.set("boxThickness", it.roundToInt().toString()) },
                )
            }
            toggleRow("showLabels", R.string.s_labels, true, settings, actions)
            toggleRow("showAimPoint", R.string.s_aim_point, true, settings, actions)
            toggleRow("showSnapLine", R.string.s_snap_line, false, settings, actions)
            toggleRow("showCount", R.string.s_count, false, settings, actions)
            toggleRow("showPerformance", R.string.s_performance, false, settings, actions)
        }
        Group {
            row {
                val confidence = settings.float("confidence", 0.5f)
                SliderRow(
                    title = stringResource(R.string.s_confidence),
                    value = confidence,
                    range = 0.1f..0.95f,
                    valueText = "${(confidence * 100).roundToInt()}%",
                    onValueChange = { actions.set("confidence", it.toString()) },
                )
            }
            row {
                val zone = settings.float("detectionZoneRadius", 200f)
                SliderRow(
                    title = stringResource(R.string.s_zone),
                    value = zone,
                    range = 100f..650f,
                    valueText = "${zone.roundToInt()} px",
                    onValueChange = { actions.set("detectionZoneRadius", it.roundToInt().toString()) },
                )
            }
        }
    }
}

@Composable
private fun AimGroup(settings: SettingsView, actions: SettingsActions) {
    Group(title = stringResource(R.string.settings_aim)) {
        row {
            SegmentedRow(
                title = stringResource(R.string.s_aim_at),
                options = listOf(stringResource(R.string.s_head), stringResource(R.string.s_chest), stringResource(R.string.s_centre)),
                selected = settings.int("aimZone", 0).coerceIn(0, 2),
                onSelect = { actions.set("aimZone", it.toString()) },
            )
        }
        row {
            val strength = settings.float("aimStrength", 0.6f)
            SliderRow(
                title = stringResource(R.string.s_strength),
                value = strength,
                range = 0f..1f,
                valueText = "${(strength * 100).roundToInt()}%",
                onValueChange = { actions.set("aimStrength", it.toString()) },
            )
        }
        row {
            val reach = settings.float("aimFovRadius", 220f)
            SliderRow(
                title = stringResource(R.string.s_reach),
                value = reach,
                range = 50f..650f,
                valueText = "${reach.roundToInt()} px",
                onValueChange = { actions.set("aimFovRadius", it.roundToInt().toString()) },
            )
        }
        row {
            SegmentedRow(
                title = stringResource(R.string.s_prefer),
                options = listOf(stringResource(R.string.s_nearest), stringResource(R.string.s_largest), stringResource(R.string.s_certain)),
                selected = settings.int("aimPriority", 0).coerceIn(0, 2),
                onSelect = { actions.set("aimPriority", it.toString()) },
            )
        }
    }
}

@Composable
private fun CalibrationGroup(settings: SettingsView, actions: SettingsActions) {
    Group(footer = stringResource(R.string.s_calibration_footer)) {
        row {
            val gain = settings.float("sceneGain", 1.5f)
            SliderRow(
                title = stringResource(R.string.s_sensitivity),
                value = gain,
                range = 0.2f..6f,
                valueText = String.format(java.util.Locale.US, "%.2f", gain),
                onValueChange = { actions.set("sceneGain", it.toString()) },
            )
        }
        row {
            val delay = settings.float("actuationDelayMs", 50f)
            SliderRow(
                title = stringResource(R.string.s_delay),
                value = delay,
                range = 0f..200f,
                valueText = "${delay.roundToInt()} ms",
                onValueChange = { actions.set("actuationDelayMs", it.roundToInt().toString()) },
            )
        }
        toggleRow("invertY", R.string.s_invert, false, settings, actions)
    }
}

@Composable
private fun TouchGroup(settings: SettingsView, actions: SettingsActions) {
    Group(title = stringResource(R.string.settings_touch), footer = stringResource(R.string.s_touch_area_footer)) {
        row {
            SegmentedRow(
                title = stringResource(R.string.s_access),
                options = listOf(stringResource(R.string.s_automatic), stringResource(R.string.touch_shizuku), stringResource(R.string.touch_root)),
                selected = settings.int("touchBackend", 0).coerceIn(0, 2),
                onSelect = { actions.set("touchBackend", it.toString()) },
            )
        }
    }
}

@Composable
private fun AppGroup(settings: SettingsView, appearance: Appearance, actions: SettingsActions) {
    Group(title = stringResource(R.string.settings_app), footer = stringResource(R.string.s_streamer_footer)) {
        row {
            SegmentedRow(
                title = stringResource(R.string.s_language),
                options = listOf("English", "中文", "Русский"),
                selected = settings.int("language", 0).coerceIn(0, 2),
                onSelect = actions.setLanguage,
            )
        }
        row {
            SegmentedRow(
                title = stringResource(R.string.s_appearance),
                options = listOf(stringResource(R.string.s_system), stringResource(R.string.s_dark), stringResource(R.string.s_light)),
                selected = appearance.mode.ordinal,
                onSelect = { actions.setThemeMode(ThemeMode.entries[it]) },
            )
        }
        row {
            SwatchRow(
                title = stringResource(R.string.s_accent),
                swatches = Accent.entries.map { Swatch(stringResource(it.label), if (AppTheme.palette.isDark) it.dark else it.light) },
                selected = appearance.accent.ordinal,
                onSelect = { actions.setAccent(Accent.entries[it]) },
            )
        }
        toggleRow("streamerMode", R.string.s_streamer, false, settings, actions)
    }
}

/** Adds a switch row for a true or false setting. */
private fun com.aimbuddy.ui.components.GroupScope.toggleRow(
    name: String,
    title: Int,
    default: Boolean,
    settings: SettingsView,
    actions: SettingsActions,
) {
    row {
        SwitchRow(
            title = stringResource(title),
            checked = settings.bool(name, default),
            onCheckedChange = { actions.set(name, if (it) "1" else "0") },
        )
    }
}
