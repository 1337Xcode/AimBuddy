package com.aimbuddy.ui.models

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.staggeredgrid.LazyStaggeredGridScope
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.CloudOff
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Download
import androidx.compose.material.icons.rounded.FileOpen
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aimbuddy.InstalledModel
import com.aimbuddy.ModelSource
import com.aimbuddy.R
import com.aimbuddy.StoreModelDefinition
import com.aimbuddy.TransferFailure
import com.aimbuddy.TransferProgress
import com.aimbuddy.ui.ModelsState
import com.aimbuddy.ui.StoreState
import com.aimbuddy.ui.Transfer
import com.aimbuddy.ui.components.Group
import com.aimbuddy.ui.components.IconBadge
import com.aimbuddy.ui.components.ListRow
import com.aimbuddy.ui.components.PillButton
import com.aimbuddy.ui.components.ScreenScaffold
import com.aimbuddy.ui.components.Segmented
import com.aimbuddy.ui.components.SmallPill
import com.aimbuddy.ui.components.TopAction
import com.aimbuddy.ui.formatBytes
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.ui.theme.Space

/** The size of the check, the spinner and the other small marks at the end of a row. */
private val MarkSize = Space.l + Space.xs

/**
 * The models screen: the models on the phone and the store, one tab each. A model is picked with
 * one tap and used the next time the assist starts.
 */
@Composable
fun ModelsScreen(
    state: ModelsState,
    running: Boolean,
    onBack: () -> Unit,
    onImport: () -> Unit,
    onUse: (String) -> Unit,
    onDelete: (String) -> Unit,
    onGet: (StoreModelDefinition) -> Unit,
    onCancel: () -> Unit,
    onLoadStore: () -> Unit,
) {
    // With no model yet, the store is the tab with something to do.
    var tab by rememberSaveable { mutableIntStateOf(if (state.installed.isEmpty()) 1 else 0) }
    // A screen opened with no model is the first run's guide: once the first model is in, the guide
    // gives way to a line that says so, and a way back to Start.
    val guided = rememberSaveable { state.installed.isEmpty() }
    var deleting by remember { mutableStateOf<InstalledModel?>(null) }

    LaunchedEffect(tab, state.store) {
        if (tab == 1 && state.store is StoreState.Idle) {
            onLoadStore()
        }
    }

    ScreenScaffold(
        title = stringResource(R.string.models_title),
        onBack = onBack,
        actions = {
            TopAction(onClick = onImport) {
                Icon(
                    imageVector = Icons.Rounded.FileOpen,
                    contentDescription = stringResource(R.string.models_import),
                    tint = AppTheme.palette.accent,
                    modifier = Modifier.size(MarkSize),
                )
            }
        },
    ) {
        if (state.installed.isEmpty()) {
            guide()
        } else if (guided) {
            ready(onDone = onBack)
        }
        item {
            Segmented(
                options = listOf(stringResource(R.string.models_installed), stringResource(R.string.models_store)),
                selected = tab,
                onSelect = { tab = it },
            )
        }
        if (tab == 0) {
            installedTab(state, running, onUse, onRequestDelete = { deleting = it })
        } else {
            storeTab(state, onGet, onCancel, onLoadStore)
        }
    }

    deleting?.let { model ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text(stringResource(R.string.models_delete_title)) },
            text = { Text(stringResource(R.string.models_delete_message, model.title)) },
            confirmButton = {
                TextButton(onClick = {
                    onDelete(model.id)
                    deleting = null
                }) {
                    Text(stringResource(R.string.models_delete), color = AppTheme.palette.danger)
                }
            },
            dismissButton = {
                TextButton(onClick = { deleting = null }) { Text(stringResource(R.string.dialog_cancel)) }
            },
            containerColor = AppTheme.palette.surface,
            shape = MaterialTheme.shapes.medium,
        )
    }
}

/** What to do with no model yet: download one, or import your own. It is there only until there is a model. */
private fun LazyStaggeredGridScope.guide() {
    item {
        Group(
            title = stringResource(R.string.models_guide_title),
            footer = stringResource(R.string.models_guide_footer),
        ) {
            row {
                ListRow(
                    title = stringResource(R.string.models_guide_download_title),
                    subtitle = stringResource(R.string.models_guide_download_text),
                    leading = { IconBadge(Icons.Rounded.Download, AppTheme.palette.accent) },
                )
            }
            row {
                ListRow(
                    title = stringResource(R.string.models_guide_import_title),
                    subtitle = stringResource(R.string.models_guide_import_text),
                    leading = { IconBadge(Icons.Rounded.FileOpen, Color(0xFF8E5CF7)) },
                )
            }
        }
    }
}

/** What the guide turns into once the first model is on the phone: it is ready, so back to Start. */
private fun LazyStaggeredGridScope.ready(onDone: () -> Unit) {
    item {
        Group {
            row {
                ListRow(
                    title = stringResource(R.string.models_ready_title),
                    subtitle = stringResource(R.string.hint_ready),
                    leading = { IconBadge(Icons.Rounded.Check, AppTheme.palette.success) },
                    trailing = { SmallPill(text = stringResource(R.string.models_ready_done), onClick = onDone) },
                )
            }
        }
    }
}

/** The models on the phone. */
private fun LazyStaggeredGridScope.installedTab(
    state: ModelsState,
    running: Boolean,
    onUse: (String) -> Unit,
    onRequestDelete: (InstalledModel) -> Unit,
) {
    if (state.installed.isEmpty()) {
        item { EmptyNote(stringResource(R.string.models_none_installed)) }
        return
    }
    item {
        Group(footer = if (running) stringResource(R.string.models_next_start) else null) {
            state.installed.forEach { model ->
                row { InstalledRow(model, active = model.id == state.activeId, onUse = onUse, onRequestDelete = onRequestDelete) }
            }
        }
    }
}

/** One model on the phone: its tile, its name, where it came from, and whether it is the one in use. */
@Composable
private fun InstalledRow(model: InstalledModel, active: Boolean, onUse: (String) -> Unit, onRequestDelete: (InstalledModel) -> Unit) {
    val source = when (model.source) {
        ModelSource.STORE -> stringResource(R.string.models_from_store)
        ModelSource.LOCAL -> stringResource(R.string.models_from_file)
    }
    val subtitle = listOfNotNull(
        source,
        model.version.takeIf { it > 0 }?.let { stringResource(R.string.models_version, it) },
        model.totalSizeBytes.takeIf { it > 0 }?.let { formatBytes(it) },
    ).joinToString(" · ")
    ModelRow(
        id = model.id,
        title = model.title,
        lines = listOf(subtitle),
        onClick = if (active) null else ({ onUse(model.id) }),
    ) {
        if (active) {
            Icon(
                imageVector = Icons.Rounded.Check,
                contentDescription = stringResource(R.string.models_in_use),
                tint = AppTheme.palette.accent,
                modifier = Modifier.size(MarkSize),
            )
        } else {
            SmallPill(text = stringResource(R.string.models_use), onClick = { onUse(model.id) })
        }
        TopAction(onClick = { onRequestDelete(model) }) {
            Icon(
                imageVector = Icons.Rounded.Delete,
                contentDescription = stringResource(R.string.models_delete),
                tint = AppTheme.palette.textDim,
                modifier = Modifier.size(Space.l),
            )
        }
    }
}

/** The models the project offers. */
private fun LazyStaggeredGridScope.storeTab(
    state: ModelsState,
    onGet: (StoreModelDefinition) -> Unit,
    onCancel: () -> Unit,
    onRetry: () -> Unit,
) {
    when (val store = state.store) {
        StoreState.Idle, StoreState.Loading -> item {
            Box(modifier = Modifier.fillMaxWidth().padding(vertical = Space.xl), contentAlignment = Alignment.Center) {
                CircularProgressIndicator(color = AppTheme.palette.accent, strokeWidth = 3.dp)
            }
        }
        StoreState.NeedsNewerApp -> item {
            Box(modifier = Modifier.fillMaxWidth().padding(vertical = Space.l), contentAlignment = Alignment.Center) {
                EmptyNote(stringResource(R.string.models_store_needs_newer_app))
            }
        }
        StoreState.Failed -> item {
            Column(
                modifier = Modifier.fillMaxWidth().padding(vertical = Space.l),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(Space.l),
            ) {
                EmptyNote(stringResource(R.string.models_store_error))
                PillButton(text = stringResource(R.string.models_retry), onClick = onRetry)
            }
        }
        is StoreState.Ready -> {
            if (store.saved) {
                item {
                    Group {
                        row {
                            ListRow(
                                title = stringResource(R.string.models_store_saved),
                                leading = { IconBadge(Icons.Rounded.CloudOff, AppTheme.palette.textDim) },
                                trailing = { SmallPill(text = stringResource(R.string.models_retry), onClick = onRetry) },
                            )
                        }
                    }
                }
            }
            if (store.models.isEmpty()) {
                item { EmptyNote(stringResource(R.string.models_store_empty)) }
            } else {
                item {
                    Group {
                        store.models.forEach { model ->
                            row {
                                StoreRow(
                                    model = model,
                                    installedVersion = state.installedVersion(model.id),
                                    transfer = state.transfer?.takeIf { it.modelId == model.id },
                                    anotherDownloadIsRunning = state.transfer != null && state.transfer.modelId != model.id,
                                    failure = state.failure?.takeIf { state.failedId == model.id },
                                    picturePath = store.icons[model.id],
                                    onGet = onGet,
                                    onCancel = onCancel,
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * One model in the store: its tile, its name, what it is for, who made it and how big it is, and a
 * way to get it, update it to a newer version, or see that it is already on the phone. While it is
 * being downloaded the row shows how far it has got and a way to stop; when the last try failed it
 * says why and offers another.
 */
@Composable
private fun StoreRow(
    model: StoreModelDefinition,
    installedVersion: Int?,
    transfer: Transfer?,
    anotherDownloadIsRunning: Boolean,
    failure: TransferFailure?,
    picturePath: String?,
    onGet: (StoreModelDefinition) -> Unit,
    onCancel: () -> Unit,
) {
    val details = listOfNotNull(
        model.author.takeIf { it.isNotBlank() },
        stringResource(R.string.models_version, model.version),
        formatBytes(model.totalSizeBytes),
    ).joinToString(" · ")
    ModelRow(
        id = model.id,
        title = model.title,
        lines = listOfNotNull(model.description.takeIf { it.isNotBlank() }, details),
        picturePath = picturePath,
        below = {
            when {
                transfer != null -> TransferLine(transfer)
                failure != null -> Text(
                    text = stringResource(failureText(failure)),
                    style = MaterialTheme.typography.bodySmall,
                    color = AppTheme.palette.danger,
                )
            }
        },
    ) {
        when {
            transfer != null && transfer.cancelling -> Text(
                text = stringResource(R.string.models_cancelling),
                style = MaterialTheme.typography.labelLarge,
                color = AppTheme.palette.textDim,
            )
            transfer != null -> SmallPill(text = stringResource(R.string.dialog_cancel), onClick = onCancel)
            anotherDownloadIsRunning -> Unit   // one at a time: the others wait for their turn
            failure != null -> SmallPill(text = stringResource(R.string.models_retry), onClick = { onGet(model) })
            installedVersion == null -> SmallPill(text = stringResource(R.string.models_get), onClick = { onGet(model) })
            model.version > installedVersion -> SmallPill(text = stringResource(R.string.models_update), onClick = { onGet(model) })
            else -> Icon(
                imageVector = Icons.Rounded.Check,
                contentDescription = stringResource(R.string.models_installed_label),
                tint = AppTheme.palette.textDim,
                modifier = Modifier.size(MarkSize),
            )
        }
    }
}

/** What to tell the player about a download that failed. */
private fun failureText(failure: TransferFailure): Int = when (failure) {
    TransferFailure.OFFLINE -> R.string.models_error_offline
    TransferFailure.DAMAGED -> R.string.models_error_damaged
    TransferFailure.NO_SPACE -> R.string.models_error_space
    TransferFailure.OTHER -> R.string.models_download_failed
}

/** How far a download has got: a bar, and under it what has arrived of the total, how fast, and how long is left. */
@Composable
private fun TransferLine(transfer: Transfer) {
    val progress = transfer.progress
    Column(modifier = Modifier.padding(top = Space.xs), verticalArrangement = Arrangement.spacedBy(Space.xs)) {
        LinearProgressIndicator(
            progress = { progress.fraction },
            color = AppTheme.palette.accent,
            trackColor = AppTheme.palette.textDim.copy(alpha = 0.2f),
            modifier = Modifier.fillMaxWidth(),
        )
        Text(
            text = transferSummary(progress),
            style = MaterialTheme.typography.bodySmall,
            color = AppTheme.palette.textDim,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

/** "12.4 MB of 38.0 MB · 3.1 MB/s · 8 s left", as much of it as is known yet. */
@Composable
private fun transferSummary(progress: TransferProgress): String {
    val parts = mutableListOf(
        stringResource(R.string.models_progress, formatBytes(progress.bytesDone), formatBytes(progress.bytesTotal)),
    )
    if (progress.bytesPerSecond > 0) {
        parts += stringResource(R.string.models_speed, formatBytes(progress.bytesPerSecond))
    }
    progress.secondsLeft?.let { seconds ->
        parts += if (seconds < SECONDS_PER_MINUTE) {
            stringResource(R.string.models_left_seconds, seconds.toInt())
        } else {
            stringResource(R.string.models_left_minutes, ((seconds + SECONDS_PER_MINUTE - 1) / SECONDS_PER_MINUTE).toInt())
        }
    }
    return parts.joinToString(" · ")
}

private const val SECONDS_PER_MINUTE = 60L

/**
 * The row both lists use: a tile, a title with up to two short lines under it, and what the player
 * can do with the model at the end. The row can be tapped when [onClick] is given.
 */
@Composable
private fun ModelRow(
    id: String,
    title: String,
    lines: List<String>,
    onClick: (() -> Unit)? = null,
    picturePath: String? = null,
    below: @Composable () -> Unit = {},
    trailing: @Composable () -> Unit,
) {
    val clickable = if (onClick != null) Modifier.clickable(role = Role.Button, onClick = onClick) else Modifier
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .then(clickable)
            .heightIn(min = Space.xxl + Space.m)
            .padding(horizontal = Space.l, vertical = Space.m),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ModelIcon(id = id, title = title, picturePath = picturePath)
        Spacer(Modifier.width(Space.m))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium,
                color = AppTheme.palette.text,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            // The first line (what the model is for) may take three lines of text, so a description is not cut
            // short; the ones after it, such as the details, stay on one.
            lines.forEachIndexed { index, line ->
                Text(
                    text = line,
                    style = MaterialTheme.typography.bodySmall,
                    color = AppTheme.palette.textDim,
                    maxLines = if (index == 0) 3 else 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            below()
        }
        Spacer(Modifier.width(Space.m))
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(Space.s)) {
            trailing()
        }
    }
}

/** A short centered sentence for a list with nothing in it. */
@Composable
private fun EmptyNote(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodyMedium,
        color = AppTheme.palette.textDim,
        textAlign = TextAlign.Center,
        modifier = Modifier.fillMaxWidth().padding(vertical = Space.l),
    )
}
