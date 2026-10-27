package com.aimbuddy.update

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import android.util.Log

/**
 * Hears back from the system's installer. When the installer needs the player to confirm, it hands
 * over the screen to ask with; when it is done it says whether the update went in. A successful
 * update replaces this app, so the answer to it is the system's own "App updated" screen.
 */
class InstallResultReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        val message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)
        Log.i(TAG, "The installer answered $status: $message")
        when (status) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> askThePlayer(context, intent)
            PackageInstaller.STATUS_SUCCESS -> Unit   // the system replaces this app now
            PackageInstaller.STATUS_FAILURE_ABORTED -> returnToAvailable()   // the player said no: the update is still on offer
            else -> fail()
        }
    }

    /** Shows the system's confirmation screen. */
    private fun askThePlayer(context: Context, intent: Intent) {
        val confirm = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(Intent.EXTRA_INTENT)
        }
        if (confirm == null) {
            fail()
            return
        }
        try {
            context.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        } catch (e: RuntimeException) {
            Log.w(TAG, "The confirmation screen could not be opened: ${e.message}")
            fail()
        }
    }

    private fun returnToAvailable() {
        val current = UpdateState.status.value
        if (current is UpdateStatus.Installing) {
            UpdateState.set(UpdateStatus.Available(current.info))
        }
    }

    private fun fail() {
        val current = UpdateState.status.value
        val info = (current as? UpdateStatus.Installing)?.info
        UpdateState.set(UpdateStatus.Failed(UpdateProblem.NOT_INSTALLED, info))
    }

    private companion object {
        const val TAG = "InstallResult"
    }
}
