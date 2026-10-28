package com.aimbuddy.ui.home

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Stop
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.aimbuddy.AssistState
import com.aimbuddy.R
import com.aimbuddy.TouchHelper
import com.aimbuddy.ui.TouchAccess
import com.aimbuddy.ui.components.Chevron
import com.aimbuddy.ui.components.Group
import com.aimbuddy.ui.components.IconBadge
import com.aimbuddy.ui.components.ListRow
import com.aimbuddy.ui.components.RowValue
import com.aimbuddy.ui.components.ContentMaxWidth
import com.aimbuddy.ui.components.screenPadding
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.ui.theme.Space
import com.aimbuddy.update.UpdateStatus

/** What the status area says for one reading of the assist: a word and a sentence. */
private class Status(val title: String, val hint: String, val isError: Boolean = false)

@Composable
private fun statusFor(snapshot: AssistState.Snapshot): Status = when (snapshot.phase) {
    AssistState.Phase.IDLE -> if (snapshot.failure != null) {
        Status(stringResource(R.string.status_failed), stringResource(failureText(snapshot.failure)), isError = true)
    } else {
        Status(stringResource(R.string.status_ready), stringResource(R.string.hint_ready))
    }
    AssistState.Phase.STARTING ->
        Status(stringResource(R.string.status_starting), stringResource(R.string.hint_starting))
    AssistState.Phase.STOPPING ->
        Status(stringResource(R.string.status_stopping), stringResource(R.string.hint_stopping))
    AssistState.Phase.RUNNING -> {
        val hint = when (val touch = snapshot.touch) {
            is AssistState.Touch.Connected -> R.string.hint_running_assist
            AssistState.Touch.Starting -> R.string.hint_connecting
            is AssistState.Touch.Unavailable -> unavailableHint(touch.reason)
            AssistState.Touch.Off -> R.string.hint_running_no_assist
        }
        Status(stringResource(R.string.status_running), stringResource(hint))
    }
}

/** The sentence for a touch helper that could not be used: what the player can do depends on why. */
internal fun unavailableHint(reason: Int): Int = when (reason) {
    TouchHelper.Reason.NO_PRIVILEGE -> R.string.hint_running_no_assist
    TouchHelper.Reason.NO_TOUCHSCREEN,
    TouchHelper.Reason.TOUCHSCREEN_DENIED,
    TouchHelper.Reason.VIRTUAL_DEVICE_DENIED -> R.string.hint_touch_denied
    TouchHelper.Reason.GRAB_FAILED -> R.string.hint_touch_busy
    else -> R.string.hint_touch_unreachable
}

/** The sentence that explains why a run ended without the player asking. */
private fun failureText(failure: AssistState.Failure): Int = when (failure) {
    AssistState.Failure.CAPTURE_DENIED -> R.string.capture_denied
    AssistState.Failure.OVERLAY_DENIED -> R.string.overlay_denied
    AssistState.Failure.MODEL_FAILED -> R.string.model_failed
    AssistState.Failure.START_FAILED -> R.string.start_failed
    AssistState.Failure.CAPTURE_ENDED -> R.string.capture_ended
}

/**
 * The first screen. The round button starts and stops the assist; under it a line says how things
 * are. A short list below leads to the model, the touch access and the settings. On a phone held
 * sideways the button and the list sit side by side.
 */
@Composable
fun HomeScreen(
    snapshot: AssistState.Snapshot,
    modelName: String?,
    detectsByColour: Boolean,
    touchAccess: TouchAccess,
    updateStatus: UpdateStatus,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onOpenModels: () -> Unit,
    onOpenSettings: () -> Unit,
    onTouchAccess: () -> Unit,
    onOpenProject: () -> Unit,
    onUpdate: () -> Unit,
) {
    val status = statusFor(snapshot)
    BoxWithConstraints(modifier = Modifier.fillMaxSize().background(AppTheme.palette.background)) {
        val sideBySide = maxWidth > maxHeight && maxWidth >= 600.dp
        val list: @Composable () -> Unit = {
            HomeList(snapshot, modelName, detectsByColour, touchAccess, updateStatus, onOpenModels, onOpenSettings, onTouchAccess, onOpenProject, onUpdate)
        }
        val padding = screenPadding()
        if (sideBySide) {
            Row(
                modifier = Modifier.fillMaxSize().padding(padding),
                horizontalArrangement = Arrangement.spacedBy(Space.xl),
            ) {
                Box(modifier = Modifier.weight(1f).fillMaxHeight(), contentAlignment = Alignment.Center) {
                    Hero(snapshot, status, onStart, onStop)
                }
                Column(
                    modifier = Modifier.weight(1.2f).fillMaxHeight().verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.Center,
                ) {
                    Spacer(Modifier.height(Space.l))
                    list()
                    Spacer(Modifier.height(Space.l))
                }
            }
        } else {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(padding)
                    .heightIn(min = maxHeight - padding.calculateTopPadding() - padding.calculateBottomPadding()),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                Hero(snapshot, status, onStart, onStop)
                Spacer(Modifier.height(Space.xl))
                Box(modifier = Modifier.widthIn(max = ContentMaxWidth)) { list() }
            }
        }
    }
}

/** The big round button with the status under it. */
@Composable
private fun Hero(snapshot: AssistState.Snapshot, status: Status, onStart: () -> Unit, onStop: () -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        PowerButton(snapshot.phase, onStart, onStop)
        Spacer(Modifier.height(Space.l))
        Text(
            text = status.title,
            style = MaterialTheme.typography.headlineSmall,
            color = if (status.isError) AppTheme.palette.danger else AppTheme.palette.text,
        )
        Spacer(Modifier.height(Space.s))
        Text(
            text = status.hint,
            style = MaterialTheme.typography.bodyMedium,
            color = AppTheme.palette.textDim,
            textAlign = TextAlign.Center,
            modifier = Modifier.widthIn(max = 320.dp),
        )
    }
}

private val PowerSize = 144.dp

/**
 * The round button. Ready: the accent color with a play mark. Starting: a turning ring. Running:
 * plain green with a stop mark. Stopping: dimmed. Nothing glows or pulses.
 */
@Composable
private fun PowerButton(phase: AssistState.Phase, onStart: () -> Unit, onStop: () -> Unit) {
    val palette = AppTheme.palette
    val starting = phase == AssistState.Phase.STARTING
    val idle = phase == AssistState.Phase.IDLE
    val fill = when (phase) {
        AssistState.Phase.IDLE -> palette.accent
        AssistState.Phase.RUNNING -> palette.success
        else -> palette.surface
    }
    val label = stringResource(if (idle) R.string.action_start else R.string.action_stop)
    Box(
        modifier = Modifier
            .size(PowerSize)
            .clip(CircleShape)
            .background(fill)
            .clickable(enabled = phase != AssistState.Phase.STOPPING, role = Role.Button) {
                if (idle) onStart() else onStop()
            }
            .semantics { contentDescription = label },
        contentAlignment = Alignment.Center,
    ) {
        when {
            starting -> CircularProgressIndicator(
                color = palette.accent,
                strokeWidth = 4.dp,
                modifier = Modifier.size(PowerSize - Space.xl),
            )
            idle -> Icon(
                imageVector = Icons.Rounded.PlayArrow,
                contentDescription = null,
                tint = palette.onAccent,
                modifier = Modifier.size(Space.xxl + Space.l),
            )
            else -> Icon(
                imageVector = Icons.Rounded.Stop,
                contentDescription = null,
                tint = Color.White,
                modifier = Modifier.size(Space.xxl).alpha(if (phase == AssistState.Phase.STOPPING) 0.4f else 1f),
            )
        }
    }
}

/** The list under the hero: an update when there is one, the model, the touch access, the settings, and a line about the project. */
@Composable
private fun HomeList(
    snapshot: AssistState.Snapshot,
    modelName: String?,
    detectsByColour: Boolean,
    touchAccess: TouchAccess,
    updateStatus: UpdateStatus,
    onOpenModels: () -> Unit,
    onOpenSettings: () -> Unit,
    onTouchAccess: () -> Unit,
    onOpenProject: () -> Unit,
    onUpdate: () -> Unit,
) {
    val palette = AppTheme.palette
    val route = (snapshot.touch as? AssistState.Touch.Connected)?.route
    val ready = route != null || touchAccess.shizuku || touchAccess.root
    val touchValue = when {
        route == TouchHelper.Route.ROOT -> stringResource(R.string.touch_root)
        route == TouchHelper.Route.SHIZUKU -> stringResource(R.string.touch_shizuku)
        touchAccess.shizuku -> stringResource(R.string.touch_shizuku)
        touchAccess.root -> stringResource(R.string.touch_root)
        else -> stringResource(R.string.touch_not_set_up)
    }
    Column(verticalArrangement = Arrangement.spacedBy(Space.l)) {
        UpdateBanner(updateStatus, onUpdate)
        Group {
            row {
                // With a colour to look for there is no model: the row says so and leads to where that is chosen.
                ListRow(
                    title = stringResource(if (detectsByColour) R.string.row_detection else R.string.row_model),
                    leading = { IconBadge(Icons.Rounded.Memory, if (modelName != null || detectsByColour) Color(0xFF8E5CF7) else Color(0xFFFF9F0A)) },
                    trailing = {
                        RowValue(
                            if (detectsByColour) stringResource(R.string.detect_by_colour) else modelName ?: stringResource(R.string.model_none),
                            Modifier.weight(1f, fill = false),
                        )
                        Chevron()
                    },
                    onClick = if (detectsByColour) onOpenSettings else onOpenModels,
                )
            }
            row {
                ListRow(
                    title = stringResource(R.string.row_touch_access),
                    leading = { IconBadge(Icons.Rounded.TouchApp, if (ready) palette.success else Color(0xFFFF9F0A)) },
                    trailing = {
                        RowValue(touchValue, Modifier.weight(1f, fill = false))
                        Chevron()
                    },
                    onClick = onTouchAccess,
                )
            }
            row {
                ListRow(
                    title = stringResource(R.string.row_settings),
                    leading = { IconBadge(Icons.Rounded.Settings, Color(0xFF8E8E93)) },
                    trailing = { Chevron() },
                    onClick = onOpenSettings,
                )
            }
        }
        ProjectLink(onOpenProject)
    }
}

/** The line under the list: the GitHub mark and a few words, which open the project's page. */
@Composable
private fun ProjectLink(onClick: () -> Unit) {
    val palette = AppTheme.palette
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(MaterialTheme.shapes.small)
            .clickable(role = Role.Button, onClick = onClick)
            .padding(Space.m),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            painter = painterResource(R.drawable.ic_github),
            contentDescription = null,
            tint = palette.textDim,
            modifier = Modifier.size(Space.l - Space.xs),
        )
        Spacer(Modifier.width(Space.s))
        Text(
            text = stringResource(R.string.open_source),
            style = MaterialTheme.typography.bodySmall,
            color = palette.textDim,
        )
    }
}
