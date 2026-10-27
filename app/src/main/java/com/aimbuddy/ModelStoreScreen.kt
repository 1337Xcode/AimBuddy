package com.aimbuddy

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import java.util.Locale

/** What the model store shows: the catalog from the project, the models on the phone, and progress. */
data class ModelStoreState(
    val catalog: List<StoreModelDefinition>,
    val installed: List<InstalledModel>,
    val activeModelId: String?,
    val downloadingModelId: String?,
    val isFetching: Boolean,
)

/** What the model store can ask for. */
class ModelStoreActions(
    val onClose: () -> Unit,
    val onImportModel: () -> Unit,
    val onRefresh: () -> Unit,
    val onDownload: (StoreModelDefinition) -> Unit,
    val onUse: (String) -> Unit,
    val onDelete: (InstalledModel) -> Unit,
)

/** Id of the model that ships inside the app; it cannot be deleted. */
private const val BUNDLED_MODEL_ID = "asset-default"

/** The full-screen model store: a catalog tab to download from and a tab with the models on the phone. */
@Composable
fun ModelStoreScreen(state: ModelStoreState, actions: ModelStoreActions) {
    var selectedTab by remember { mutableIntStateOf(0) }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 24.dp, vertical = 16.dp),
        ) {
            StoreHeader(
                isCatalogTab = selectedTab == 0,
                isFetching = state.isFetching,
                actions = actions,
            )
            Spacer(Modifier.height(12.dp))
            TabRow(
                selectedTabIndex = selectedTab,
                containerColor = Color.Transparent,
                divider = { HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.08f)) },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Tab(
                    selected = selectedTab == 0,
                    onClick = { selectedTab = 0 },
                    text = { Text("Store Catalog", fontWeight = FontWeight.Bold) },
                )
                Tab(
                    selected = selectedTab == 1,
                    onClick = { selectedTab = 1 },
                    text = { Text("My Models (${state.installed.size})", fontWeight = FontWeight.Bold) },
                )
            }
            Spacer(Modifier.height(16.dp))
            Box(modifier = Modifier.weight(1f)) {
                if (selectedTab == 0) {
                    CatalogTab(state, actions)
                } else {
                    InstalledTab(state, actions)
                }
            }
        }
    }
}

/** The top row of the store: back, the title, refresh (catalog tab) and import. */
@Composable
private fun StoreHeader(isCatalogTab: Boolean, isFetching: Boolean, actions: ModelStoreActions) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = actions.onClose) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
            }
            Spacer(Modifier.width(8.dp))
            Text(
                text = "Model Store",
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.onSurface,
            )
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (isCatalogTab) {
                IconButton(onClick = actions.onRefresh, enabled = !isFetching) {
                    if (isFetching) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(20.dp),
                            strokeWidth = 2.dp,
                            color = MaterialTheme.colorScheme.primary,
                        )
                    } else {
                        Icon(Icons.Filled.Refresh, contentDescription = "Refresh")
                    }
                }
            }
            Spacer(Modifier.width(12.dp))
            OutlinedButton(
                onClick = actions.onImportModel,
                contentPadding = PaddingValues(horizontal = 14.dp, vertical = 8.dp),
                shape = RoundedCornerShape(12.dp),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.5f)),
            ) {
                Icon(Icons.Filled.FolderOpen, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text("Import Local", style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

/** The catalog tab: models listed by the project that can be downloaded. */
@Composable
private fun CatalogTab(state: ModelStoreState, actions: ModelStoreActions) {
    if (state.isFetching && state.catalog.isEmpty()) {
        CenteredMessage {
            CircularProgressIndicator(color = MaterialTheme.colorScheme.primary)
            Spacer(Modifier.height(12.dp))
            Text("Fetching catalog from GitHub...", color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        return
    }
    if (state.catalog.isEmpty()) {
        CenteredMessage {
            Text("No models available in store.", color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(12.dp))
            OutlinedButton(onClick = actions.onRefresh) { Text("Retry Fetch") }
        }
        return
    }
    LazyColumn(verticalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxSize()) {
        items(state.catalog) { model ->
            val isInstalled = state.installed.any { it.id == model.id }
            ModelCard(
                title = model.title,
                description = model.description,
                isActive = state.activeModelId == model.id,
                details = listOf(
                    "Size: " + if (model.totalSizeBytes > 0L) formatBytes(model.totalSizeBytes) else "metadata only",
                    if (model.isDownloadable) "Downloadable" else "Metadata only",
                ),
            ) {
                when {
                    state.downloadingModelId == model.id -> CircularProgressIndicator(
                        modifier = Modifier.size(24.dp),
                        color = MaterialTheme.colorScheme.primary,
                        strokeWidth = 2.5.dp,
                    )
                    state.activeModelId == model.id -> Unit
                    isInstalled -> SmallOutlinedButton("Use") { actions.onUse(model.id) }
                    model.isDownloadable -> FilledTonalButton(
                        onClick = { actions.onDownload(model) },
                        shape = RoundedCornerShape(10.dp),
                        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp),
                    ) {
                        Text("Download", style = MaterialTheme.typography.labelMedium)
                    }
                    else -> Text(
                        text = "Demo",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontWeight = FontWeight.Bold,
                    )
                }
            }
        }
    }
}

/** The tab with the models already on the phone: switch to one, or delete it. */
@Composable
private fun InstalledTab(state: ModelStoreState, actions: ModelStoreActions) {
    if (state.installed.isEmpty()) {
        CenteredMessage {
            Text(
                text = "No models installed. Import model files or download from store.",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        return
    }
    LazyColumn(verticalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxSize()) {
        items(state.installed) { model ->
            val isActive = state.activeModelId == model.id
            ModelCard(
                title = model.title,
                description = model.description,
                isActive = isActive,
                details = listOf(
                    "Size: " + if (model.totalSizeBytes > 0L) formatBytes(model.totalSizeBytes) else "n/a",
                    "Source: ${model.source.name.lowercase()}",
                ),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (!isActive) {
                        SmallOutlinedButton("Use") { actions.onUse(model.id) }
                        Spacer(Modifier.width(8.dp))
                    }
                    if (model.id != BUNDLED_MODEL_ID) {
                        IconButton(onClick = { actions.onDelete(model) }) {
                            Icon(
                                imageVector = Icons.Filled.Delete,
                                contentDescription = "Delete Model",
                                tint = MaterialTheme.colorScheme.error,
                            )
                        }
                    }
                }
            }
        }
    }
}

/** One model in a list: its name, a line about it, small facts below, and the controls on the right. */
@Composable
private fun ModelCard(
    title: String,
    description: String,
    isActive: Boolean,
    details: List<String>,
    controls: @Composable () -> Unit,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
        ),
        border = BorderStroke(
            1.dp,
            if (isActive) {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.4f)
            } else {
                MaterialTheme.colorScheme.outline.copy(alpha = 0.08f)
            },
        ),
    ) {
        Column(modifier = Modifier.fillMaxWidth().padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = title,
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface,
                        )
                        if (isActive) {
                            Spacer(Modifier.width(8.dp))
                            ActiveBadge()
                        }
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        text = description,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Spacer(Modifier.width(16.dp))
                Box { controls() }
            }
            Spacer(Modifier.height(10.dp))
            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                details.forEachIndexed { index, detail ->
                    if (index > 0) {
                        Spacer(Modifier.width(16.dp))
                    }
                    Text(
                        text = detail,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                    )
                }
            }
        }
    }
}

/** The "ACTIVE" tag shown next to the model in use. */
@Composable
private fun ActiveBadge() {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(4.dp))
            .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.15f))
            .padding(horizontal = 6.dp, vertical = 2.dp),
    ) {
        Text(
            text = "ACTIVE",
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.primary,
            fontWeight = FontWeight.Bold,
        )
    }
}

/** A compact outlined button used for "Use". */
@Composable
private fun SmallOutlinedButton(label: String, onClick: () -> Unit) {
    OutlinedButton(
        onClick = onClick,
        shape = RoundedCornerShape(10.dp),
        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp),
    ) {
        Text(label, style = MaterialTheme.typography.labelMedium)
    }
}

/** Centers a message (and what goes with it) in the free space of a tab. */
@Composable
private fun CenteredMessage(content: @Composable () -> Unit) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) { content() }
    }
}

/** A size in bytes as the player reads it: "512 B", "3.4 KB", "12.50 MB", "1.20 GB". */
internal fun formatBytes(bytes: Long): String {
    if (bytes < 1024L) {
        return "$bytes B"
    }
    val kb = bytes / 1024.0
    if (kb < 1024.0) {
        return String.format(Locale.US, "%.1f KB", kb)
    }
    val mb = kb / 1024.0
    if (mb < 1024.0) {
        return String.format(Locale.US, "%.2f MB", mb)
    }
    return String.format(Locale.US, "%.2f GB", mb / 1024.0)
}
