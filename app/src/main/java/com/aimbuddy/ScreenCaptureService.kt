package com.aimbuddy

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.hardware.HardwareBuffer
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat

/**
 * ScreenCaptureService - Foreground Service for MediaProjection
 *
 * Manages the MediaProjection foreground service requirement.
 * Also acts as the JNI bridge for frame data.
 */
class ScreenCaptureService : Service() {

    companion object {
        private const val CHANNEL_ID = "ESP_Capture_Channel"
        private const val NOTIFICATION_ID = 12345
        
        // JNI bridge method
        @JvmStatic
        external fun nativeOnFrame(hardwareBuffer: HardwareBuffer, timestamp: Long)
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, createNotification())
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Service runs until explicitly stopped
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(
                CHANNEL_ID,
                "ESP Capture Service",
                NotificationManager.IMPORTANCE_DEFAULT
            )
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(serviceChannel)
        }
    }

    private fun createNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("ESP Active")
            .setContentText("Screen capture in progress")
            .setSmallIcon(R.mipmap.ic_launcher)
            .build()
    }
}
