package com.aimbuddy.ui

import java.util.Locale

/** A size in bytes as the player reads it: "512 B", "3.4 KB", "12.5 MB", "1.2 GB". */
fun formatBytes(bytes: Long): String {
    if (bytes < 1024L) {
        return "$bytes B"
    }
    val kilobytes = bytes / 1024.0
    if (kilobytes < 1024.0) {
        return String.format(Locale.US, "%.0f KB", kilobytes)
    }
    val megabytes = kilobytes / 1024.0
    if (megabytes < 1024.0) {
        return String.format(Locale.US, "%.1f MB", megabytes)
    }
    return String.format(Locale.US, "%.1f GB", megabytes / 1024.0)
}
