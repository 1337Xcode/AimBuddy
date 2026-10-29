package com.aimbuddy.ui.settings

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.staggeredgrid.LazyStaggeredGridState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aimbuddy.BuildConfig
import com.aimbuddy.Project
import com.aimbuddy.R
import com.aimbuddy.ui.Appearance
import com.aimbuddy.ui.SettingsView
import com.aimbuddy.ui.home.updateProblemText
import com.aimbuddy.ui.components.Chevron
import com.aimbuddy.ui.components.Group
import com.aimbuddy.ui.components.GroupScope
import com.aimbuddy.ui.components.ListRow
import com.aimbuddy.ui.components.RowValue
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
import com.aimbuddy.update.UpdateStatus
import kotlin.math.abs
import kotlin.math.roundToInt

/** One color the player can pick for the boxes around targets. */
private class BoxColor(@StringRes val name: Int, val color: Color)

/** The colors offered for the boxes: bright ones that stand out against game scenery. */
private val BoxColors = listOf(
    BoxColor(R.string.color_red, Color(0xFFFF3B30)),
    BoxColor(R.string.color_orange, Color(0xFFFF9500)),
    BoxColor(R.string.color_yellow, Color(0xFFFFD60A)),
    BoxColor(R.string.color_lime, Color(0xFFA3E635)),
    BoxColor(R.string.color_green, Color(0xFF30D158)),
    BoxColor(R.string.color_cyan, Color(0xFF64D2FF)),
    BoxColor(R.string.color_blue, Color(0xFF0A84FF)),
    BoxColor(R.string.color_purple, Color(0xFFBF5AF2)),
    BoxColor(R.string.color_pink, Color(0xFFFF375F)),
    BoxColor(R.string.color_white, Color(0xFFFFFFFF)),
)

/** The third-party parts the app is built from, with the license each one comes under. */
private val Notices = listOf(
    "NCNN" to "BSD 3-Clause, Tencent",
    "Dear ImGui" to "MIT, Omar Cornut",
    "Shizuku API" to "Apache 2.0, RikkaApps",
    "AndroidX, Jetpack Compose, Material icons" to "Apache 2.0, Google",
    "Kotlin coroutines" to "Apache 2.0, JetBrains",
    "Geist" to "SIL Open Font License 1.1, Vercel",
    "Noto Sans SC" to "SIL Open Font License 1.1, Google",
    "Octicons" to "MIT, GitHub",
    "YOLO26" to "AGPL-3.0, Ultralytics",
)

/** What the settings screen can change. Each call sends changes to the running app's settings. */
class SettingsActions(
    val set: (String, String) -> Unit,
    val setAll: (List<Pair<String, String>>) -> Unit,
    val setLanguage: (Int) -> Unit,
    val setThemeMode: (ThemeMode) -> Unit,
    val setAccent: (Accent) -> Unit,
    val reset: () -> Unit,
    val openUrl: (String) -> Unit,
    val updateAction: () -> Unit,
    val setAutoUpdateCheck: (Boolean) -> Unit,
    val openTouchAccess: () -> Unit,
)

/**
 * Every setting, in groups. The values come from the app's settings, so what is changed here is what
 * the overlay and the assist use. Nothing needs saving: a change takes effect at once.
 */
@Composable
fun SettingsScreen(
    settings: SettingsView,
    appearance: Appearance,
    updateStatus: UpdateStatus,
    autoUpdateCheck: Boolean,
    actions: SettingsActions,
    listState: LazyStaggeredGridState,
    onBack: () -> Unit,
) {
    var confirmReset by remember { mutableStateOf(false) }
    var showNotices by remember { mutableStateOf(false) }

    ScreenScaffold(
        title = stringResource(R.string.settings_title),
        onBack = onBack,
        columnWidth = 340.dp,
        maxWidth = 1200.dp,
        gridState = listState,
    ) {
        item { OverlayGroup(settings, actions) }
        item { DetectionGroup(settings, actions) }
        item { AimGroup(settings, actions) }
        item { CameraGroup(settings, actions) }
        item { TouchGroup(settings, actions) }
        item { AppGroup(settings, appearance, actions) }
        item { AboutGroup(updateStatus, autoUpdateCheck, actions, onShowNotices = { showNotices = true }) }
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
    if (showNotices) {
        NoticesDialog(onDismiss = { showNotices = false })
    }
}

/** How the boxes around targets look, and what is drawn besides them. */
@Composable
private fun OverlayGroup(settings: SettingsView, actions: SettingsActions) {
    val red = settings.float("boxColorR", 1f)
    val green = settings.float("boxColorG", 0.2f)
    val blue = settings.float("boxColorB", 0.2f)
    val selectedColor = BoxColors.indices.minByOrNull { index ->
        val color = BoxColors[index].color
        abs(color.red - red) + abs(color.green - green) + abs(color.blue - blue)
    } ?: 0

    Group(title = stringResource(R.string.settings_overlay)) {
        row {
            SwatchRow(
                title = stringResource(R.string.s_box_color),
                swatches = BoxColors.map { Swatch(stringResource(it.name), it.color) },
                selected = selectedColor,
                onSelect = { index ->
                    val color = BoxColors[index].color
                    actions.setAll(
                        listOf(
                            "boxColorR" to color.red.toString(),
                            "boxColorG" to color.green.toString(),
                            "boxColorB" to color.blue.toString(),
                        )
                    )
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
        toggleRow("showDetectionArea", R.string.s_detection_outline, true, settings, actions)
        toggleRow("showAimReach", R.string.s_aim_reach_circle, true, settings, actions)
        toggleRow("showSnapLine", R.string.s_snap_line, false, settings, actions)
        toggleRow("showCount", R.string.s_count, false, settings, actions)
        toggleRow("showPerformance", R.string.s_performance, false, settings, actions)
    }
}

/** How sure the detector has to be, and how much of the screen it looks at. */
@Composable
private fun DetectionGroup(settings: SettingsView, actions: SettingsActions) {
    val byColour = settings.int("detector", 0) == 1
    Group(
        title = stringResource(R.string.settings_detection),
        footer = stringResource(if (byColour) R.string.s_colour_footer else R.string.s_detect_footer),
    ) {
        row {
            SegmentedRow(
                title = stringResource(R.string.s_detect_with),
                options = listOf(stringResource(R.string.s_detect_model), stringResource(R.string.s_detect_colour)),
                selected = settings.int("detector", 0).coerceIn(0, 1),
                onSelect = { actions.set("detector", it.toString()) },
            )
        }
        if (byColour) {
            row {
                val tolerance = settings.float("colourTolerance", 14f)
                SliderRow(
                    title = stringResource(R.string.s_colour_tolerance),
                    value = tolerance,
                    range = 4f..40f,
                    valueText = "${tolerance.roundToInt()}°",
                    onValueChange = { actions.set("colourTolerance", it.roundToInt().toString()) },
                )
            }
            row {
                val smallest = settings.float("colourMinHeight", 40f)
                SliderRow(
                    title = stringResource(R.string.s_colour_smallest),
                    value = smallest,
                    range = 10f..300f,
                    valueText = "${smallest.roundToInt()} px",
                    onValueChange = { actions.set("colourMinHeight", it.roundToInt().toString()) },
                )
            }
        } else {
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

/** Where the assist aims, how hard it pulls, and which target it takes. */
@Composable
private fun AimGroup(settings: SettingsView, actions: SettingsActions) {
    Group(title = stringResource(R.string.settings_aim), footer = stringResource(R.string.s_aim_footer)) {
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
        row {
            SegmentedRow(
                title = stringResource(R.string.s_after_landing),
                options = listOf(stringResource(R.string.s_keep_following), stringResource(R.string.s_one_correction)),
                selected = settings.int("aimAfterLanding", 0).coerceIn(0, 1),
                onSelect = { actions.set("aimAfterLanding", it.toString()) },
            )
        }
        row {
            SegmentedRow(
                title = stringResource(R.string.s_switching),
                options = listOf(stringResource(R.string.s_sticky), stringResource(R.string.s_balanced), stringResource(R.string.s_quick)),
                selected = settings.int("aimSwitching", 1).coerceIn(0, 2),
                onSelect = { actions.set("aimSwitching", it.toString()) },
            )
        }
    }
}

/** How the game's camera answers a finger: measured while playing, or set by hand. */
@Composable
private fun CameraGroup(settings: SettingsView, actions: SettingsActions) {
    Group(title = stringResource(R.string.settings_camera), footer = stringResource(R.string.s_calibration_footer)) {
        row {
            SwitchRow(
                title = stringResource(R.string.s_auto_calibrate),
                subtitle = stringResource(R.string.s_auto_calibrate_hint),
                checked = settings.bool("autoCalibrate", true),
                onCheckedChange = { actions.set("autoCalibrate", if (it) "1" else "0") },
            )
        }
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

/** Which way the assist reaches the touchscreen: the choice is made, and tested, on a screen of its own. */
@Composable
private fun TouchGroup(settings: SettingsView, actions: SettingsActions) {
    val route = when (settings.int("touchBackend", 0)) {
        1 -> stringResource(R.string.touch_shizuku)
        2 -> stringResource(R.string.touch_root)
        else -> stringResource(R.string.s_automatic)
    }
    Group(title = stringResource(R.string.settings_touch), footer = stringResource(R.string.s_touch_area_footer)) {
        row {
            ListRow(
                title = stringResource(R.string.row_touch_access),
                trailing = {
                    RowValue(route)
                    Chevron()
                },
                onClick = actions.openTouchAccess,
            )
        }
    }
}

/** The app's own language and look, and keeping the overlay out of recordings. */
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

/** Which version this is, whether there is a newer one, where the source is, and under what terms it is shared. */
@Composable
private fun AboutGroup(updateStatus: UpdateStatus, autoUpdateCheck: Boolean, actions: SettingsActions, onShowNotices: () -> Unit) {
    Group(title = stringResource(R.string.settings_about)) {
        row {
            ListRow(
                title = stringResource(R.string.about_version),
                trailing = { RowValue(BuildConfig.AIMBUDDY_VERSION) },
            )
        }
        row {
            ListRow(
                title = stringResource(R.string.about_updates),
                trailing = { RowValue(updateText(updateStatus)) },
                onClick = actions.updateAction,
            )
        }
        row {
            SwitchRow(
                title = stringResource(R.string.s_auto_update),
                checked = autoUpdateCheck,
                onCheckedChange = actions.setAutoUpdateCheck,
            )
        }
        row {
            ListRow(
                title = stringResource(R.string.about_source),
                leading = {
                    Icon(
                        painter = painterResource(R.drawable.ic_github),
                        contentDescription = null,
                        tint = AppTheme.palette.textDim,
                        modifier = Modifier.size(Space.l),
                    )
                },
                trailing = { Chevron() },
                onClick = { actions.openUrl(Project.URL) },
            )
        }
        row {
            ListRow(
                title = stringResource(R.string.about_license),
                trailing = { Chevron() },
                onClick = { actions.openUrl(Project.LICENSE_URL) },
            )
        }
        row {
            ListRow(
                title = stringResource(R.string.about_notices),
                trailing = { Chevron() },
                onClick = onShowNotices,
            )
        }
    }
}

/** What the updates row says about where the update stands. */
@Composable
private fun updateText(status: UpdateStatus): String = when (status) {
    UpdateStatus.Unknown -> stringResource(R.string.about_updates_check)
    UpdateStatus.Checking -> stringResource(R.string.about_updates_checking)
    UpdateStatus.UpToDate -> stringResource(R.string.about_updates_up_to_date)
    is UpdateStatus.Available -> stringResource(R.string.about_updates_available, status.info.version)
    is UpdateStatus.Downloading -> stringResource(R.string.update_downloading)
    is UpdateStatus.ReadyToInstall -> stringResource(R.string.update_ready)
    is UpdateStatus.Installing -> stringResource(R.string.update_installing)
    is UpdateStatus.Failed -> stringResource(updateProblemText(status.problem))
}

/** The third-party parts the app is built from, with their licenses. */
@Composable
private fun NoticesDialog(onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.about_notices)) },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(Space.s)) {
                Notices.forEach { (name, license) ->
                    Column {
                        Text(text = name, style = MaterialTheme.typography.titleSmall, color = AppTheme.palette.text)
                        Text(text = license, style = MaterialTheme.typography.bodySmall, color = AppTheme.palette.textDim)
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.dialog_close)) }
        },
        containerColor = AppTheme.palette.surface,
        shape = MaterialTheme.shapes.medium,
    )
}

/** Adds a switch row for a true or false setting. */
private fun GroupScope.toggleRow(
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
