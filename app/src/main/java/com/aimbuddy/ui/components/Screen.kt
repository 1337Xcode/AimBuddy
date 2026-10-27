package com.aimbuddy.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.staggeredgrid.LazyStaggeredGridScope
import androidx.compose.foundation.lazy.staggeredgrid.LazyVerticalStaggeredGrid
import androidx.compose.foundation.lazy.staggeredgrid.StaggeredGridCells
import androidx.compose.foundation.lazy.staggeredgrid.rememberLazyStaggeredGridState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowLeft
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aimbuddy.R
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.ui.theme.Space

/** The widest a single column of rows gets, so lines stay readable on a tablet or a phone turned sideways. */
val ContentMaxWidth = 640.dp

/** The height of the bar a screen's title sits in, under the status bar. */
private val TopBarHeight = Space.xxl

/** The size of a round button in the bar: big enough for a thumb. */
private val TopButtonSize = Space.xxl - Space.s

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
 * A full screen in the app's style: a bar at the top with a back arrow when there is a screen to go
 * back to, the title in the middle and actions at the end, and the content scrolling under it. A
 * hairline appears under the bar once the content has moved beneath it.
 *
 * The content is laid out in columns at least [columnWidth] wide, as many as fit, up to [maxWidth]
 * across. A phone held upright gets one column and the same phone held sideways gets two.
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
    val insets = WindowInsets.safeDrawing.asPaddingValues()
    val direction = LocalLayoutDirection.current
    val gridState = rememberLazyStaggeredGridState()
    val scrolled by remember { derivedStateOf { gridState.canScrollBackward } }

    Column(modifier = modifier.fillMaxSize().background(AppTheme.palette.background)) {
        TopBar(title = title, onBack = onBack, maxWidth = maxWidth, showDivider = scrolled, actions = actions)
        Box(modifier = Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.TopCenter) {
            LazyVerticalStaggeredGrid(
                state = gridState,
                columns = StaggeredGridCells.Adaptive(columnWidth),
                modifier = Modifier.fillMaxHeight().widthIn(max = maxWidth),
                contentPadding = PaddingValues.Absolute(
                    left = insets.calculateLeftPadding(direction) + Space.l,
                    top = Space.m,
                    right = insets.calculateRightPadding(direction) + Space.l,
                    bottom = insets.calculateBottomPadding() + Space.xl,
                ),
                verticalItemSpacing = Space.xl,
                horizontalArrangement = Arrangement.spacedBy(Space.l),
                content = content,
            )
            NavigationBarScrim()
        }
    }
}

/**
 * The bar at the top of a screen: the back arrow at the start, the title in the middle, actions at
 * the end. It reaches under the status bar so the bar and the page behind it are one color.
 */
@Composable
private fun TopBar(
    title: String,
    onBack: (() -> Unit)?,
    maxWidth: Dp,
    showDivider: Boolean,
    actions: @Composable RowScope.() -> Unit,
) {
    val insets = WindowInsets.safeDrawing.asPaddingValues()
    val direction = LocalLayoutDirection.current
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(AppTheme.palette.background)
            .padding(top = insets.calculateTopPadding()),
    ) {
        Box(modifier = Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
            Box(
                modifier = Modifier
                    .widthIn(max = maxWidth)
                    .fillMaxWidth()
                    .height(TopBarHeight)
                    .padding(
                        start = insets.calculateLeftPadding(direction) + Space.s,
                        end = insets.calculateRightPadding(direction) + Space.s,
                    ),
            ) {
                if (onBack != null) {
                    Box(modifier = Modifier.align(Alignment.CenterStart)) { BackButton(onBack) }
                }
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                    color = AppTheme.palette.text,
                    textAlign = TextAlign.Center,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    // Room for a button on each side keeps the title in the middle and clear of them.
                    modifier = Modifier
                        .align(Alignment.Center)
                        .padding(horizontal = TopButtonSize + Space.xs)
                        .semantics { heading() },
                )
                Row(
                    modifier = Modifier.align(Alignment.CenterEnd),
                    verticalAlignment = Alignment.CenterVertically,
                    content = actions,
                )
            }
        }
        if (showDivider) {
            Hairline()
        } else {
            Box(modifier = Modifier.height(Space.hairline))
        }
    }
}

/** A soft fade at the bottom edge of the box it is placed in, so the navigation bar's icons stay readable over scrolling content. */
@Composable
private fun BoxScope.NavigationBarScrim() {
    val color = AppTheme.palette.background
    val insets = WindowInsets.safeDrawing.asPaddingValues()
    Box(
        modifier = Modifier
            .align(Alignment.BottomCenter)
            .fillMaxWidth()
            .height(insets.calculateBottomPadding() + Space.s)
            .background(Brush.verticalGradient(listOf(color.copy(alpha = 0f), color))),
    )
}

/** The arrow at the start of a screen's top bar. */
@Composable
fun BackButton(onBack: () -> Unit) {
    Box(
        modifier = Modifier
            .size(TopButtonSize)
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

/** A round icon button for an action at the end of the top bar. */
@Composable
fun TopAction(onClick: () -> Unit, content: @Composable () -> Unit) {
    Box(
        modifier = Modifier
            .size(TopButtonSize)
            .clip(CircleShape)
            .clickable(role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        content()
    }
}
