package com.aimbuddy.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.ui.theme.Space

/** The height of one row in a group: tall enough for a thumb. */
val RowHeight = Space.xxl

/** The height of a row where rows are kept close together (see [LocalTightRows]): still about a thumb. */
val RowHeightTight = Space.xl + Space.m

/** The most of a row's width that what sits at its end may take, so a long value never squeezes the title. */
private const val TRAILING_SHARE = 0.45f

/** Collects the rows of a [Group] so the group can put a hairline between them. */
class GroupScope {
    internal val rows = mutableListOf<@Composable () -> Unit>()

    /** Adds one row. */
    fun row(content: @Composable () -> Unit) {
        rows += content
    }
}

/**
 * A rounded card of rows with an optional small title above it and a footnote below it, the shape
 * settings take on iOS. Rows are separated by a hairline that starts where the text starts.
 */
@Composable
fun Group(
    modifier: Modifier = Modifier,
    title: String? = null,
    footer: String? = null,
    content: GroupScope.() -> Unit,
) {
    val rows = GroupScope().apply(content).rows
    Column(modifier = modifier.fillMaxWidth()) {
        if (title != null) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodySmall,
                color = AppTheme.palette.textDim,
                modifier = Modifier.padding(start = Space.l, bottom = Space.s),
            )
        }
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .clip(MaterialTheme.shapes.medium)
                .background(AppTheme.palette.surface),
        ) {
            rows.forEachIndexed { index, row ->
                row()
                if (index < rows.lastIndex) {
                    Hairline(modifier = Modifier.padding(start = Space.l))
                }
            }
        }
        if (footer != null) {
            Text(
                text = footer,
                style = MaterialTheme.typography.bodySmall,
                color = AppTheme.palette.textDim,
                modifier = Modifier.padding(start = Space.l, end = Space.l, top = Space.s),
            )
        }
    }
}

/** A one pixel line in the separator color. */
@Composable
fun Hairline(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(Space.hairline)
            .background(AppTheme.palette.separator),
    )
}

/**
 * One row: an optional leading badge, a title with an optional line under it, and something at the
 * end (a value, a switch, a chevron). It is tappable when `onClick` is given.
 */
@Composable
fun ListRow(
    title: String,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    leading: (@Composable () -> Unit)? = null,
    trailing: (@Composable RowScope.() -> Unit)? = null,
    titleColor: Color = AppTheme.palette.text,
    enabled: Boolean = true,
    subtitleMaxLines: Int = 2,
    onClick: (() -> Unit)? = null,
) {
    val clickable = if (onClick != null) Modifier.clickable(enabled = enabled, role = Role.Button, onClick = onClick) else Modifier
    // A row is about as wide as the screen on a phone, which is where a long value is a problem.
    val trailingMax = (LocalConfiguration.current.screenWidthDp * TRAILING_SHARE).dp
    Row(
        modifier = modifier
            .fillMaxWidth()
            .then(clickable)
            .heightIn(min = if (LocalTightRows.current) RowHeightTight else RowHeight)
            .padding(horizontal = Space.l, vertical = Space.s),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (leading != null) {
            leading()
            Spacer(Modifier.width(Space.m))
        }
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium,
                color = if (enabled) titleColor else AppTheme.palette.textDim,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            if (subtitle != null) {
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = AppTheme.palette.textDim,
                    maxLines = subtitleMaxLines,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        if (trailing != null) {
            Spacer(Modifier.width(Space.m))
            Row(
                modifier = Modifier.widthIn(max = trailingMax),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(Space.s),
            ) {
                trailing()
            }
        }
    }
}

/**
 * The small text at the end of a row, such as the current value. When something follows it there,
 * give it `Modifier.weight(1f, fill = false)` so that it is the one that gives way.
 */
@Composable
fun RowValue(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        modifier = modifier,
        style = MaterialTheme.typography.bodyMedium,
        color = AppTheme.palette.textDim,
        textAlign = TextAlign.End,
        maxLines = 2,
        overflow = TextOverflow.Ellipsis,
    )
}

/** The arrow that says a row opens another screen. */
@Composable
fun Chevron() {
    Icon(
        imageVector = Icons.AutoMirrored.Rounded.KeyboardArrowRight,
        contentDescription = null,
        tint = AppTheme.palette.textDim,
        modifier = Modifier.size(Space.l),
    )
}

/** A colored rounded square with an icon in it, the way Settings marks its rows. */
@Composable
fun IconBadge(icon: ImageVector, tint: Color, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .size(Space.xl - Space.xs)
            .clip(RoundedCornerShape(8.dp))
            .background(tint),
        contentAlignment = Alignment.Center,
    ) {
        Icon(imageVector = icon, contentDescription = null, tint = Color.White, modifier = Modifier.size(Space.l - Space.xs))
    }
}
