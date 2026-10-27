package com.aimbuddy

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** What the status card says for one reading of the assist's state. */
internal data class StatusDisplay(val title: String, val description: String, val accent: Color)

/** Turns the shared state into the words and color of the status card. */
internal fun describeStatus(snapshot: AssistState.Snapshot): StatusDisplay = when (snapshot.phase) {
    AssistState.Phase.IDLE ->
        if (snapshot.failed) {
            StatusDisplay("Could not start", snapshot.message, StatusColors.Red)
        } else {
            StatusDisplay("Ready", "Press Start. A floating button then appears over your game.", StatusColors.Grey)
        }
    AssistState.Phase.STARTING ->
        StatusDisplay("Starting", "Loading the detector and the screen capture.", StatusColors.Amber)
    AssistState.Phase.STOPPING ->
        StatusDisplay("Stopping", "Giving the touchscreen back.", StatusColors.Amber)
    AssistState.Phase.RUNNING -> describeRunning(snapshot.touch)
}

/** The status card while running: detection is on, and aim assist depends on the touch helper. */
private fun describeRunning(touch: AssistState.Touch): StatusDisplay = when (touch) {
    AssistState.Touch.Off ->
        StatusDisplay("Running", "Targets are highlighted on the screen.", StatusColors.Green)
    AssistState.Touch.Starting ->
        StatusDisplay("Running", "Targets are highlighted. Connecting aim assist...", StatusColors.Amber)
    is AssistState.Touch.Connected -> {
        val route = if (touch.route == TouchHelper.Route.ROOT) "root" else "Shizuku"
        StatusDisplay("Running", "Targets are highlighted and aim assist is on, through $route.", StatusColors.Green)
    }
    is AssistState.Touch.Unavailable ->
        StatusDisplay("Running without aim assist", touch.detail.lineSequence().first(), StatusColors.Amber)
}

/**
 * The first screen: shows what the assist is doing, the active model, and the Start / Stop button.
 * The corner menu opens the model store and the project page.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LauncherScreen(
    snapshot: AssistState.Snapshot,
    activeModelText: String,
    rootReady: Boolean,
    shizukuReady: Boolean,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onImportModel: () -> Unit,
    onOpenStore: () -> Unit,
    onOpenGithub: () -> Unit,
    onOpenCreator: () -> Unit,
) {
    var menuOpen by remember { mutableStateOf(false) }
    val status = describeStatus(snapshot)

    Box(modifier = Modifier.fillMaxSize()) {
        Scaffold(
            containerColor = MaterialTheme.colorScheme.background,
            topBar = {
                CenterAlignedTopAppBar(
                    title = {
                        Text(
                            text = "AimBuddy",
                            style = MaterialTheme.typography.titleLarge,
                            fontWeight = FontWeight.SemiBold,
                        )
                    },
                    actions = {
                        IconButton(onClick = { menuOpen = true }) {
                            Icon(Icons.Filled.MoreVert, contentDescription = "More")
                        }
                    },
                    colors = TopAppBarDefaults.centerAlignedTopAppBarColors(
                        containerColor = MaterialTheme.colorScheme.background,
                    ),
                )
            },
        ) { inner ->
            Row(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(inner)
                    .padding(horizontal = 24.dp, vertical = 12.dp),
                horizontalArrangement = Arrangement.spacedBy(24.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(
                    modifier = Modifier
                        .weight(1.2f)
                        .fillMaxHeight(),
                    verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.Start,
                ) {
                    StatusCard(status)
                    Spacer(Modifier.height(12.dp))
                    ActiveModelCard(activeModelText = activeModelText, onSelectModel = onOpenStore)
                }

                Column(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight(),
                    verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    PrimaryActionButton(
                        isRunning = snapshot.isActive,
                        enabled = snapshot.phase != AssistState.Phase.STOPPING,
                        onClick = if (snapshot.isActive) onStop else onStart,
                    )
                    Spacer(Modifier.height(14.dp))
                    Text(
                        text = "Touch Input Backends",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier.padding(bottom = 6.dp),
                    )
                    BackendChips(rootReady = rootReady, shizukuReady = shizukuReady)
                    Spacer(Modifier.height(14.dp))
                    Text(
                        text = "Open source - created by 1337XCode",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                        modifier = Modifier
                            .clickable { onOpenCreator() }
                            .padding(vertical = 4.dp),
                    )
                }
            }
        }

        if (menuOpen) {
            CornerMenu(
                onDismiss = { menuOpen = false },
                onImportModel = { menuOpen = false; onImportModel() },
                onOpenStore = { menuOpen = false; onOpenStore() },
                onOpenGithub = { menuOpen = false; onOpenGithub() },
            )
        }
    }
}

/** The small menu in the top corner, drawn inside the window so it never needs a popup permission. */
@Composable
private fun BoxScope.CornerMenu(
    onDismiss: () -> Unit,
    onImportModel: () -> Unit,
    onOpenStore: () -> Unit,
    onOpenGithub: () -> Unit,
) {
    // A full-window layer that closes the menu when the player taps anywhere outside it.
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = 0.2f))
            .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) { onDismiss() },
    )
    Card(
        modifier = Modifier
            .align(Alignment.TopEnd)
            .padding(top = 56.dp, end = 16.dp)
            .width(220.dp)
            .shadow(12.dp, RoundedCornerShape(14.dp)),
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.15f)),
    ) {
        Column(modifier = Modifier.padding(vertical = 4.dp)) {
            CornerMenuItem("Import model files", Icons.Filled.FolderOpen, onImportModel)
            HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.08f))
            CornerMenuItem("Model store", Icons.Filled.Download, onOpenStore)
            HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.08f))
            CornerMenuItem("View on GitHub", Icons.Filled.Info, onOpenGithub)
        }
    }
}

/** One row of the corner menu: an icon and a label. */
@Composable
private fun CornerMenuItem(text: String, icon: ImageVector, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() }
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier.size(20.dp),
        )
        Spacer(modifier = Modifier.width(12.dp))
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface,
            fontWeight = FontWeight.Medium,
        )
    }
}

/** The card that says, in words and a colored dot, what the assist is doing. */
@Composable
private fun StatusCard(status: StatusDisplay) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
        ),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.12f)),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp, vertical = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Start,
        ) {
            Box(
                modifier = Modifier
                    .size(12.dp)
                    .clip(CircleShape)
                    .background(status.accent)
                    .border(2.dp, status.accent.copy(alpha = 0.4f), CircleShape),
            )
            Spacer(Modifier.width(16.dp))
            Column {
                Text(
                    text = status.title,
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface,
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    text = status.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 3,
                )
            }
        }
    }
}

/** The card that names the detection model in use; tapping it opens the model store. */
@Composable
private fun ActiveModelCard(activeModelText: String, onSelectModel: () -> Unit) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onSelectModel() },
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f),
        ),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.08f)),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "ACTIVE MODEL",
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.8f),
                )
                Spacer(Modifier.height(2.dp))
                Text(
                    text = activeModelText.removePrefix("Model: "),
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                )
            }
            Icon(
                imageVector = Icons.Filled.FolderOpen,
                contentDescription = "Manage Models",
                tint = MaterialTheme.colorScheme.secondary,
                modifier = Modifier.size(18.dp),
            )
        }
    }
}

/** The big Start / Stop button. It reads STOP while the assist is starting or running. */
@Composable
private fun PrimaryActionButton(isRunning: Boolean, enabled: Boolean, onClick: () -> Unit) {
    val containerColor = if (isRunning) Color(0xFFDC2626) else MaterialTheme.colorScheme.primary
    val contentColor = if (isRunning) Color.White else Color(0xFF070B13)
    val icon = if (isRunning) Icons.Filled.Stop else Icons.Filled.PlayArrow
    val label = if (isRunning) "STOP" else "START"

    Card(
        modifier = Modifier
            .width(220.dp)
            .height(52.dp)
            .shadow(6.dp, RoundedCornerShape(26.dp))
            .clip(RoundedCornerShape(26.dp))
            .clickable(enabled = enabled) { onClick() },
        shape = RoundedCornerShape(26.dp),
        colors = CardDefaults.cardColors(
            containerColor = if (enabled) containerColor else containerColor.copy(alpha = 0.4f),
            contentColor = contentColor,
        ),
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(imageVector = icon, contentDescription = null, modifier = Modifier.size(22.dp))
            Spacer(Modifier.width(8.dp))
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold,
                fontSize = 14.sp,
                letterSpacing = 0.8.sp,
            )
        }
    }
}

/** Which ways of reaching the touchscreen are ready right now. */
@Composable
private fun BackendChips(rootReady: Boolean, shizukuReady: Boolean) {
    Row(horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
        BackendChip(label = "Root", ready = rootReady)
        Spacer(Modifier.width(12.dp))
        BackendChip(label = "Shizuku", ready = shizukuReady)
    }
}

/** One chip: the name of a touch route, with a dot that is green when the route is ready. */
@Composable
private fun BackendChip(label: String, ready: Boolean) {
    val color = if (ready) StatusColors.Green else MaterialTheme.colorScheme.onSurfaceVariant
    AssistChip(
        onClick = {},
        label = { Text(if (ready) "$label: ready" else label, fontWeight = FontWeight.SemiBold, fontSize = 12.sp) },
        leadingIcon = {
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(color),
            )
        },
        colors = AssistChipDefaults.assistChipColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.2f),
        ),
        shape = RoundedCornerShape(10.dp),
    )
}
