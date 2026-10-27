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
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.FileOpen
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aimbuddy.InstalledModel
import com.aimbuddy.ModelSource
import com.aimbuddy.R
import com.aimbuddy.StoreModelDefinition
import com.aimbuddy.ui.ModelsState
import com.aimbuddy.ui.StoreState
import com.aimbuddy.ui.components.Group
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
    onLoadStore: () -> Unit,
) {
    var tab by rememberSaveable { mutableIntStateOf(0) }
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
            storeTab(state, onGet, onLoadStore)
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
        ModelSource.ASSET -> stringResource(R.string.models_built_in)
        ModelSource.STORE -> stringResource(R.string.models_from_store)
        ModelSource.LOCAL -> stringResource(R.string.models_from_file)
    }
    val subtitle = if (model.totalSizeBytes > 0) "${formatBytes(model.totalSizeBytes)} · $source" else source
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
        if (model.source != ModelSource.ASSET) {
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
}

/** The models the project offers. */
private fun LazyStaggeredGridScope.storeTab(state: ModelsState, onGet: (StoreModelDefinition) -> Unit, onRetry: () -> Unit) {
    when (val store = state.store) {
        StoreState.Idle, StoreState.Loading -> item {
            Box(modifier = Modifier.fillMaxWidth().padding(vertical = Space.xl), contentAlignment = Alignment.Center) {
                CircularProgressIndicator(color = AppTheme.palette.accent, strokeWidth = 3.dp)
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
            if (store.models.isEmpty()) {
                item { EmptyNote(stringResource(R.string.models_store_empty)) }
            } else {
                item {
                    Group {
                        store.models.forEach { model ->
                            row {
                                StoreRow(
                                    model = model,
                                    installed = state.installed.any { it.id == model.id },
                                    busy = state.busyId == model.id,
                                    onGet = onGet,
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

/** One model in the store: its tile, its name, what it is for and how big it is, and a way to get it. */
@Composable
private fun StoreRow(model: StoreModelDefinition, installed: Boolean, busy: Boolean, onGet: (StoreModelDefinition) -> Unit) {
    val size = if (model.totalSizeBytes > 0) formatBytes(model.totalSizeBytes) else null
    ModelRow(
        id = model.id,
        title = model.title,
        lines = listOfNotNull(model.description.takeIf { it.isNotBlank() }, size),
    ) {
        when {
            busy -> CircularProgressIndicator(color = AppTheme.palette.accent, strokeWidth = 3.dp, modifier = Modifier.size(MarkSize))
            installed -> Icon(
                imageVector = Icons.Rounded.Check,
                contentDescription = stringResource(R.string.models_installed_label),
                tint = AppTheme.palette.textDim,
                modifier = Modifier.size(MarkSize),
            )
            model.isDownloadable -> SmallPill(text = stringResource(R.string.models_get), onClick = { onGet(model) })
        }
    }
}

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
        ModelIcon(id = id, title = title)
        Spacer(Modifier.width(Space.m))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium,
                color = AppTheme.palette.text,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            lines.forEach { line ->
                Text(
                    text = line,
                    style = MaterialTheme.typography.bodySmall,
                    color = AppTheme.palette.textDim,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
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
