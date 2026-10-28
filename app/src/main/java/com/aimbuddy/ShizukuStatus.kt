package com.aimbuddy

import android.content.pm.PackageManager
import android.util.Log
import rikka.shizuku.Shizuku

private const val TAG = "ShizukuStatus"

/** The package of the Shizuku app. */
const val SHIZUKU_PACKAGE = "moe.shizuku.privileged.api"

/** Where a phone stands with Shizuku, the route to touch access that needs no root. */
enum class ShizukuStatus {
    /** Running, and AimBuddy has been allowed to use it. */
    READY,

    /** Running, but AimBuddy has not been allowed yet. */
    NEEDS_PERMISSION,

    /** Installed, but its service is not running. */
    NOT_RUNNING,

    /** Not on the phone. */
    NOT_INSTALLED,
}

/** Reads Shizuku's state from three facts the phone gives without starting anything. A running service means it is installed, whatever the launcher says. */
fun shizukuStatus(installed: Boolean, running: Boolean, allowed: Boolean): ShizukuStatus = when {
    running && allowed -> ShizukuStatus.READY
    running -> ShizukuStatus.NEEDS_PERMISSION
    installed -> ShizukuStatus.NOT_RUNNING
    else -> ShizukuStatus.NOT_INSTALLED
}

/** Asks the phone about Shizuku: is it installed, is its service running, and is AimBuddy allowed. Changes nothing. */
fun probeShizuku(packageManager: PackageManager): ShizukuStatus {
    val installed = packageManager.getLaunchIntentForPackage(SHIZUKU_PACKAGE) != null
    val running = Shizuku.pingBinder()
    val allowed = running && isAllowedByShizuku()
    return shizukuStatus(installed, running, allowed)
}

/** Whether a running Shizuku has allowed AimBuddy. Shizuku throws when its service goes away while it is asked, which means not allowed. */
private fun isAllowedByShizuku(): Boolean = try {
    Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
} catch (e: RuntimeException) {
    Log.w(TAG, "Shizuku could not say whether AimBuddy is allowed: ${e.message}")
    false
}
