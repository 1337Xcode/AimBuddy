package com.aimbuddy.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.aimbuddy.ui.theme.AppTheme
import com.aimbuddy.ui.theme.Space

/** A row with a switch at the end. Tapping anywhere on the row flips it. */
@Composable
fun SwitchRow(title: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit, subtitle: String? = null) {
    val palette = AppTheme.palette
    ListRow(
        title = title,
        subtitle = subtitle,
        onClick = { onCheckedChange(!checked) },
        trailing = {
            Switch(
                checked = checked,
                onCheckedChange = null,   // the row handles the tap, so the switch is only drawn
                colors = SwitchDefaults.colors(
                    checkedThumbColor = Color.White,
                    checkedTrackColor = palette.success,
                    checkedBorderColor = Color.Transparent,
                    uncheckedThumbColor = Color.White,
                    uncheckedTrackColor = palette.raised,
                    uncheckedBorderColor = Color.Transparent,
                ),
            )
        },
    )
}

/** A row with a title, its value at the end, and a slider under them. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SliderRow(
    title: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    valueText: String,
    onValueChange: (Float) -> Unit,
    steps: Int = 0,
) {
    val palette = AppTheme.palette
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = Space.l, vertical = Space.s),
    ) {
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium,
                color = palette.text,
                modifier = Modifier.weight(1f),
            )
            RowValue(valueText)
        }
        val colors = SliderDefaults.colors(
            thumbColor = Color.White,
            activeTrackColor = palette.accent,
            inactiveTrackColor = palette.raised,
            activeTickColor = Color.Transparent,
            inactiveTickColor = Color.Transparent,
        )
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = range,
            steps = steps,
            colors = colors,
            thumb = {
                Box(
                    modifier = Modifier
                        .size(Space.xl - Space.xs)
                        .shadow(Space.xs, CircleShape)
                        .background(Color.White, CircleShape),
                )
            },
            track = { state ->
                SliderDefaults.Track(
                    sliderState = state,
                    colors = colors,
                    enabled = true,
                    thumbTrackGapSize = 0.dp,
                    trackInsideCornerSize = 0.dp,
                    drawStopIndicator = null,
                    drawTick = { _, _ -> },
                    modifier = Modifier.height(Space.s - 2.dp),
                )
            },
        )
    }
}

/** A row with a title and a segmented control under it. Exactly one option is selected. */
@Composable
fun SegmentedRow(title: String, options: List<String>, selected: Int, onSelect: (Int) -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = Space.l, vertical = Space.m),
        verticalArrangement = Arrangement.spacedBy(Space.s),
    ) {
        Text(text = title, style = MaterialTheme.typography.titleMedium, color = AppTheme.palette.text)
        Segmented(options = options, selected = selected, onSelect = onSelect)
    }
}

/** The control itself: a track with one raised segment under the selected option. */
@Composable
fun Segmented(options: List<String>, selected: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier) {
    val palette = AppTheme.palette
    val selectedColor = if (palette.isDark) Color(0xFF636366) else Color.White
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .background(palette.raised)
            .padding(2.dp),
    ) {
        options.forEachIndexed { index, label ->
            val isSelected = index == selected
            Box(
                modifier = Modifier
                    .weight(1f)
                    .heightIn(min = Space.xl + Space.xs)
                    .clip(RoundedCornerShape(8.dp))
                    .background(if (isSelected) selectedColor else Color.Transparent)
                    .clickable(role = Role.RadioButton) { onSelect(index) }
                    .semantics { this.selected = isSelected },
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = label,
                    style = MaterialTheme.typography.titleSmall,
                    color = palette.text,
                    modifier = Modifier.padding(horizontal = Space.s),
                )
            }
        }
    }
}

/** One color the player can pick. */
data class Swatch(val name: String, val color: Color)

/** How many color choices stand in one line, so ten of them make two even lines. */
private const val SwatchesPerLine = 5

/** A row with a title and round color choices in even lines. The selected one has a ring and a check. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SwatchRow(title: String, swatches: List<Swatch>, selected: Int, onSelect: (Int) -> Unit) {
    val palette = AppTheme.palette
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = Space.l, vertical = Space.m),
        verticalArrangement = Arrangement.spacedBy(Space.m),
    ) {
        Text(text = title, style = MaterialTheme.typography.titleMedium, color = palette.text)
        FlowRow(
            maxItemsInEachRow = SwatchesPerLine,
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalArrangement = Arrangement.spacedBy(Space.m),
            modifier = Modifier.fillMaxWidth(),
        ) {
            swatches.forEachIndexed { index, swatch ->
                val isSelected = index == selected
                Box(
                    modifier = Modifier
                        .size(Space.xl + Space.s)
                        .clip(CircleShape)
                        .background(swatch.color)
                        // A thin outline keeps a color close to the card's own (white on white) in view.
                        .border(
                            if (isSelected) BorderStroke(3.dp, palette.text) else BorderStroke(Space.hairline, palette.separator),
                            CircleShape,
                        )
                        .clickable(role = Role.RadioButton) { onSelect(index) }
                        .semantics {
                            contentDescription = swatch.name
                            this.selected = isSelected
                        },
                    contentAlignment = Alignment.Center,
                ) {
                    if (isSelected) {
                        Icon(
                            imageVector = Icons.Rounded.Check,
                            contentDescription = null,
                            tint = if (swatch.color.luminance() > 0.6f) Color.Black else Color.White,
                            modifier = Modifier.size(Space.l),
                        )
                    }
                }
            }
        }
    }
}

private fun Color.luminance(): Float = 0.2126f * red + 0.7152f * green + 0.0722f * blue

/** A wide pill button: filled for the main action, or tinted for a quiet one. */
@Composable
fun PillButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    container: Color = AppTheme.palette.accent,
    content: Color = AppTheme.palette.onAccent,
    enabled: Boolean = true,
    loading: Boolean = false,
) {
    Box(
        modifier = modifier
            .heightIn(min = Space.xxl)
            .clip(MaterialTheme.shapes.extraLarge)
            .background(if (enabled) container else container.copy(alpha = 0.4f))
            .clickable(enabled = enabled && !loading, role = Role.Button, onClick = onClick)
            .padding(horizontal = Space.xl),
        contentAlignment = Alignment.Center,
    ) {
        if (loading) {
            CircularProgressIndicator(color = content, strokeWidth = 2.dp, modifier = Modifier.size(Space.l))
        } else {
            Text(text = text, style = MaterialTheme.typography.labelLarge, color = content)
        }
    }
}

/** A small tinted pill for an action at the end of a row, such as Get or Use. */
@Composable
fun SmallPill(text: String, onClick: () -> Unit, enabled: Boolean = true, destructive: Boolean = false) {
    val palette = AppTheme.palette
    val tint = if (destructive) palette.danger else palette.accent
    Box(
        modifier = Modifier
            .height(Space.xl)
            .clip(MaterialTheme.shapes.extraLarge)
            .background(tint.copy(alpha = if (palette.isDark) 0.22f else 0.14f))
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            .padding(horizontal = Space.l),
        contentAlignment = Alignment.Center,
    ) {
        Text(text = text, style = MaterialTheme.typography.titleSmall, color = tint)
    }
}
