package com.aimbuddy.update

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

/**
 * After an update has replaced the app, opens it again, so the player lands back in AimBuddy. The
 * system may refuse to open a screen from the background; then the player opens the app as usual.
 */
class AppUpdatedReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) {
            return
        }
        val launch = context.packageManager.getLaunchIntentForPackage(context.packageName) ?: return
        try {
            context.startActivity(launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        } catch (e: RuntimeException) {
            Log.w(TAG, "The app could not be opened after the update: ${e.message}")
        }
    }

    private companion object {
        const val TAG = "AppUpdated"
    }
}
