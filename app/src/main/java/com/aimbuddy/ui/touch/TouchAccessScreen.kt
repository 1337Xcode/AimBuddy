package com.aimbuddy.ui.touch

import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aimbuddy.R
import com.aimbuddy.RootShell
import com.aimbuddy.ShizukuStatus
import com.aimbuddy.TouchHelper
import com.aimbuddy.TouchTest
import com.aimbuddy.ui.SettingsView
import com.aimbuddy.ui.TouchAccess
import com.aimbuddy.ui.components.Chevron
import com.aimbuddy.ui.components.Group
import com.aimbuddy.ui.components.ListRow
import com.aimbuddy.ui.components.PillButton
import com.aimbuddy.ui.components.RowValue
import com.aimbuddy.ui.components.ScreenScaffold
import com.aimbuddy.ui.components.SegmentedRow
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.ui.theme.Space

/** What the touch access screen can do for the player. */
class TouchAccessActions(
    val setRoute: (Int) -> Unit,
    val allowShizuku: () -> Unit,
    val openShizuku: () -> Unit,
    val openShizukuGuide: () -> Unit,
    val runTest: () -> Unit,
)

/**
 * How AimBuddy reaches the touchscreen: which route to use, where each route stands on this phone, and a
 * test that starts the touch helper the way Start does. Without Shizuku or root the assist only outlines people.
 */
@Composable
fun TouchAccessScreen(
    settings: SettingsView,
    access: TouchAccess,
    test: TouchTest,
    assistRunning: Boolean,
    actions: TouchAccessActions,
    onBack: () -> Unit,
) {
    ScreenScaffold(title = stringResource(R.string.row_touch_access), onBack = onBack, columnWidth = 340.dp, maxWidth = 1200.dp) {
        item { RouteGroup(settings, actions) }
        item { TestGroup(test, settings.int("touchBackend", 0), assistRunning, actions) }
        item { PhoneGroup(access, actions) }
    }
}

/** The route the player wants: Shizuku, root, or whichever works, Shizuku first. */
@Composable
private fun RouteGroup(settings: SettingsView, actions: TouchAccessActions) {
    Group(footer = stringResource(R.string.touch_route_footer)) {
        row {
            SegmentedRow(
                title = stringResource(R.string.touch_reach_with),
                options = listOf(
                    stringResource(R.string.s_automatic),
                    stringResource(R.string.touch_shizuku),
                    stringResource(R.string.touch_root),
                ),
                selected = settings.int("touchBackend", 0).coerceIn(0, 2),
                onSelect = actions.setRoute,
            )
        }
    }
}

/** Where each route stands on this phone, with what to do about it when it is not ready. */
@Composable
private fun PhoneGroup(access: TouchAccess, actions: TouchAccessActions) {
    val palette = AppTheme.palette
    Group(title = stringResource(R.string.touch_phone_group), footer = stringResource(R.string.touch_phone_footer)) {
        row { ShizukuRow(access.shizuku, actions) }
        row {
            val (text, tint) = when (access.root) {
                RootShell.Status.ALLOWED -> R.string.touch_root_allowed to palette.success
                RootShell.Status.UNAVAILABLE -> R.string.touch_root_unavailable to palette.warning
                RootShell.Status.NOT_ASKED -> R.string.touch_root_unknown to palette.textDim
            }
            ListRow(
                title = stringResource(R.string.touch_root),
                trailing = { StatusValue(stringResource(text), tint) },
            )
        }
    }
}

/** The Shizuku row: how it stands, and a tap that does the next thing it needs, if there is one. */
@Composable
private fun ShizukuRow(status: ShizukuStatus, actions: TouchAccessActions) {
    val palette = AppTheme.palette
    val (text, tint) = when (status) {
        ShizukuStatus.READY -> R.string.touch_shizuku_ready to palette.success
        ShizukuStatus.NEEDS_PERMISSION -> R.string.touch_shizuku_allow to palette.warning
        ShizukuStatus.NOT_RUNNING -> R.string.touch_shizuku_not_running to palette.warning
        ShizukuStatus.NOT_INSTALLED -> R.string.touch_shizuku_not_installed to palette.textDim
    }
    val nextStep: (() -> Unit)? = when (status) {
        ShizukuStatus.READY -> null
        ShizukuStatus.NEEDS_PERMISSION -> actions.allowShizuku
        ShizukuStatus.NOT_RUNNING -> actions.openShizuku
        ShizukuStatus.NOT_INSTALLED -> actions.openShizukuGuide
    }
    ListRow(
        title = stringResource(R.string.touch_shizuku),
        trailing = {
            StatusValue(stringResource(text), tint)
            if (nextStep != null) {
                Chevron()
            }
        },
        onClick = nextStep,
    )
}

/** A value at the end of a row with a dot before it that shows how things stand: green is fine, amber needs something done. */
@Composable
private fun RowScope.StatusValue(text: String, tint: Color) {
    Box(modifier = Modifier.size(Space.s).background(tint, CircleShape))
    RowValue(text, Modifier.weight(1f, fill = false))
}

/** The test button and what the last test found. */
@Composable
private fun TestGroup(test: TouchTest, route: Int, assistRunning: Boolean, actions: TouchAccessActions) {
    Group(footer = stringResource(if (assistRunning) R.string.touch_test_stop_first else R.string.touch_test_footer)) {
        row {
            Column(
                modifier = Modifier.fillMaxWidth().padding(Space.l),
                verticalArrangement = Arrangement.spacedBy(Space.m),
            ) {
                PillButton(
                    text = stringResource(R.string.touch_test_button),
                    onClick = actions.runTest,
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !assistRunning,
                    loading = test == TouchTest.Running,
                )
                TestResult(test, route)
            }
        }
    }
}

/** What the last test found: that it works and through which route, or why it does not and what the helper said. */
@Composable
private fun TestResult(test: TouchTest, route: Int) {
    val palette = AppTheme.palette
    when (test) {
        TouchTest.Idle, TouchTest.Running -> Unit
        is TouchTest.Passed -> Text(
            text = stringResource(if (test.route == TouchHelper.Route.ROOT) R.string.touch_test_works_root else R.string.touch_test_works_shizuku),
            style = MaterialTheme.typography.bodyMedium,
            color = palette.success,
        )
        is TouchTest.Failed -> Column(verticalArrangement = Arrangement.spacedBy(Space.xs)) {
            Text(
                text = stringResource(failureText(test.reason, route)),
                style = MaterialTheme.typography.bodyMedium,
                color = palette.danger,
            )
            val helperLog = helperLogOf(test.detail)
            if (helperLog.isNotBlank()) {
                Text(text = helperLog, style = MaterialTheme.typography.bodySmall, color = palette.textDim, maxLines = HELPER_LOG_LINES)
            }
        }
    }
}

/** The most lines of what the helper printed that the screen shows. */
private const val HELPER_LOG_LINES = 6

/** The sentence for a reason the helper could not be used. `route` is the player's choice: 1 Shizuku, 2 root, 0 whichever works. */
@StringRes
private fun failureText(reason: Int, route: Int): Int = when (reason) {
    TouchHelper.Reason.NO_PRIVILEGE -> when (route) {
        1 -> R.string.touch_fail_no_shizuku
        2 -> R.string.touch_fail_no_root
        else -> R.string.touch_fail_no_route
    }
    TouchHelper.Reason.NO_TOUCHSCREEN -> R.string.touch_fail_no_touchscreen
    TouchHelper.Reason.TOUCHSCREEN_DENIED, TouchHelper.Reason.VIRTUAL_DEVICE_DENIED -> R.string.touch_fail_denied
    TouchHelper.Reason.GRAB_FAILED -> R.string.touch_fail_busy
    else -> R.string.touch_fail_unreachable
}

/** What the helper printed: the failure's detail is the English sentence for the reason, then a new line, then the log. */
private fun helperLogOf(detail: String): String = detail.substringAfter('\n', missingDelimiterValue = "")
