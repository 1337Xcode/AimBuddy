package com.aimbuddy.ui.home

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.SystemUpdate
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aimbuddy.R
import com.aimbuddy.ui.components.Group
import com.aimbuddy.ui.components.IconBadge
import com.aimbuddy.ui.components.ListRow
import com.aimbuddy.ui.components.SmallPill
import com.aimbuddy.ui.formatBytes
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.ui.theme.Space
import com.aimbuddy.update.UpdateProblem
import com.aimbuddy.update.UpdateStatus

/** Lines the banner gives its second line: what to do about an update signed with another key must not be cut short. */
private const val SUBTITLE_LINES = 4

/** The sentence for a problem with an update. */
@StringRes
fun updateProblemText(problem: UpdateProblem): Int = when (problem) {
    UpdateProblem.UNREACHABLE -> R.string.update_failed_unreachable
    UpdateProblem.NOT_VERIFIED -> R.string.update_failed_not_verified
    UpdateProblem.DIFFERENT_SIGNER -> R.string.update_failed_different_signer
    UpdateProblem.NOT_INSTALLED -> R.string.update_failed_not_installed
}

/**
 * The row above the list when there is something to do about an update: take it, wait for it, install
 * it, or try again. It is not drawn while the app is up to date or has not looked yet.
 */
@Composable
fun UpdateBanner(status: UpdateStatus, onAction: () -> Unit) {
    val title: String
    val subtitle: String?
    val trailing: @Composable () -> Unit
    when (status) {
        is UpdateStatus.Available -> {
            title = stringResource(R.string.update_available_title)
            subtitle = stringResource(R.string.update_available_detail, status.info.version, formatBytes(status.info.sizeBytes))
            trailing = { SmallPill(text = stringResource(R.string.update_action_update), onClick = onAction) }
        }
        is UpdateStatus.Downloading -> {
            title = stringResource(R.string.update_downloading)
            subtitle = "${(status.progress * 100).toInt()}%"
            trailing = { Spinner(progress = status.progress) }
        }
        is UpdateStatus.ReadyToInstall -> {
            title = stringResource(R.string.update_ready)
            subtitle = status.info.version
            trailing = { SmallPill(text = stringResource(R.string.update_action_install), onClick = onAction) }
        }
        is UpdateStatus.Installing -> {
            title = stringResource(R.string.update_installing)
            subtitle = status.info.version
            trailing = { Spinner(progress = null) }
        }
        is UpdateStatus.Failed -> {
            // An update signed with another key cannot be retried into working: the player is sent to the releases page.
            val differentSigner = status.problem == UpdateProblem.DIFFERENT_SIGNER
            title = stringResource(updateProblemText(status.problem))
            subtitle = if (differentSigner) stringResource(R.string.update_failed_different_signer_hint) else null
            val action = if (differentSigner) R.string.update_action_releases else R.string.update_action_retry
            trailing = { SmallPill(text = stringResource(action), onClick = onAction) }
        }
        UpdateStatus.Unknown, UpdateStatus.Checking, UpdateStatus.UpToDate -> return
    }
    Group {
        row {
            ListRow(
                title = title,
                subtitle = subtitle,
                subtitleMaxLines = SUBTITLE_LINES,
                leading = { IconBadge(Icons.Rounded.SystemUpdate, AppTheme.palette.accent) },
                trailing = { trailing() },
            )
        }
    }
}

/** A ring that turns, or fills as [progress] (0 to 1) grows. */
@Composable
private fun Spinner(progress: Float?) {
    val modifier = Modifier.size(Space.l + Space.xs)
    if (progress == null) {
        CircularProgressIndicator(color = AppTheme.palette.accent, strokeWidth = 3.dp, modifier = modifier)
    } else {
        CircularProgressIndicator(progress = { progress }, color = AppTheme.palette.accent, strokeWidth = 3.dp, modifier = modifier)
    }
}
