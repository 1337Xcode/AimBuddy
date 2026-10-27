package com.aimbuddy.ui

import java.util.Locale

/**
 * A size in bytes as the player reads it: "512 B", "3.4 KB", "12.5 MB", "1.2 GB". The number and
 * the unit are joined by a no-break space, so a line never ends between them.
 */
fun formatBytes(bytes: Long): String {
    if (bytes < 1024L) {
        return "$bytes\u00A0B"
    }
    val kilobytes = bytes / 1024.0
    if (kilobytes < 1024.0) {
        return String.format(Locale.US, "%.0f\u00A0KB", kilobytes)
    }
    val megabytes = kilobytes / 1024.0
    if (megabytes < 1024.0) {
        return String.format(Locale.US, "%.1f\u00A0MB", megabytes)
    }
    return String.format(Locale.US, "%.1f\u00A0GB", megabytes / 1024.0)
}
