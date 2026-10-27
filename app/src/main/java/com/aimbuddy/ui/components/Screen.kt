package com.aimbuddy.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.staggeredgrid.LazyStaggeredGridScope
import androidx.compose.foundation.lazy.staggeredgrid.LazyVerticalStaggeredGrid
import androidx.compose.foundation.lazy.staggeredgrid.StaggeredGridCells
import androidx.compose.foundation.lazy.staggeredgrid.StaggeredGridItemSpan
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowLeft
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aimbuddy.R
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.ui.theme.Space

/** The widest a single column of rows gets, so lines stay readable on a tablet or a phone turned sideways. */
val ContentMaxWidth = 640.dp

// A screen shorter than this (a phone held sideways) puts the title in the top row instead of under it.
private val CompactHeight = 480.dp

/** The room the system bars and a camera cutout take, plus the screen's own margin. */
@Composable
fun screenPadding(): PaddingValues {
    val insets = WindowInsets.safeDrawing.asPaddingValues()
    val direction = LocalLayoutDirection.current
    return PaddingValues.Absolute(
        left = insets.calculateLeftPadding(direction) + Space.l,
        top = insets.calculateTopPadding(),
        right = insets.calculateRightPadding(direction) + Space.l,
        bottom = insets.calculateBottomPadding() + Space.xl,
    )
}

/**
 * A full screen in the app's style: a back arrow when there is a screen to go back to, actions at
 * the end of that top row, then a large title and the content. Everything scrolls together.
 *
 * The content is laid out in columns at least [columnWidth] wide, as many as fit, up to [maxWidth]
 * across. A phone held upright gets one column and the same phone held sideways gets two. On a
 * short screen the title moves up into the top row so the content starts higher.
 */
@Composable
fun ScreenScaffold(
    title: String,
    onBack: (() -> Unit)?,
    modifier: Modifier = Modifier,
    columnWidth: Dp = ContentMaxWidth,
    maxWidth: Dp = ContentMaxWidth,
    actions: @Composable RowScope.() -> Unit = {},
    content: LazyStaggeredGridScope.() -> Unit,
) {
    BoxWithConstraints(
        modifier = modifier.fillMaxSize().background(AppTheme.palette.background),
        contentAlignment = Alignment.TopCenter,
    ) {
        val compact = maxHeight < CompactHeight
        LazyVerticalStaggeredGrid(
            columns = StaggeredGridCells.Adaptive(columnWidth),
            modifier = Modifier.fillMaxHeight().widthIn(max = maxWidth),
            contentPadding = screenPadding(),
            verticalItemSpacing = Space.xl,
            horizontalArrangement = Arrangement.spacedBy(Space.l),
        ) {
            item(span = StaggeredGridItemSpan.FullLine) {
                Column {
                    Row(
                        modifier = Modifier.fillMaxWidth().heightIn(min = Space.xxl),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        if (onBack != null) {
                            BackButton(onBack)
                        }
                        if (compact) {
                            Text(
                                text = title,
                                style = MaterialTheme.typography.headlineSmall,
                                color = AppTheme.palette.text,
                                modifier = Modifier.weight(1f),
                            )
                        } else {
                            Box(modifier = Modifier.weight(1f))
                        }
                        actions()
                    }
                    if (!compact) {
                        Text(
                            text = title,
                            style = MaterialTheme.typography.headlineLarge,
                            color = AppTheme.palette.text,
                            modifier = Modifier.padding(top = Space.xs),
                        )
                    }
                }
            }
            content()
        }
        BarScrims()
    }
}

/**
 * Soft fades under the status bar and the navigation bar, so their icons stay readable when content
 * scrolls beneath them.
 */
@Composable
private fun BarScrims() {
    val color = AppTheme.palette.background
    val insets = WindowInsets.safeDrawing.asPaddingValues()
    Box(modifier = Modifier.fillMaxSize()) {
        Box(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .fillMaxWidth()
                .height(insets.calculateTopPadding() + Space.s)
                .background(Brush.verticalGradient(listOf(color, color.copy(alpha = 0f)))),
        )
        Box(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .height(insets.calculateBottomPadding() + Space.s)
                .background(Brush.verticalGradient(listOf(color.copy(alpha = 0f), color))),
        )
    }
}

/** The arrow at the start of a screen's top row. */
@Composable
fun BackButton(onBack: () -> Unit) {
    Box(
        modifier = Modifier
            .size(Space.xxl - Space.s)
            .clip(CircleShape)
            .clickable(role = Role.Button, onClick = onBack),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            imageVector = Icons.AutoMirrored.Rounded.KeyboardArrowLeft,
            contentDescription = stringResource(R.string.back),
            tint = AppTheme.palette.accent,
            modifier = Modifier.size(Space.xl),
        )
    }
}

/** A round icon button for an action in the top row. */
@Composable
fun TopAction(onClick: () -> Unit, content: @Composable () -> Unit) {
    Box(
        modifier = Modifier
            .size(Space.xxl - Space.s)
            .clip(CircleShape)
            .clickable(role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        content()
    }
}
