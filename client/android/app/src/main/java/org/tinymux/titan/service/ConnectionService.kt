package org.tinymux.titan.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder
import androidx.core.app.NotificationCompat
import org.tinymux.titan.MainActivity

class ConnectionService : Service() {
    private val binder = LocalBinder()
    private var activeCount = 0

    inner class LocalBinder : Binder() {
        fun getService(): ConnectionService = this@ConnectionService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                startForeground(NOTIFICATION_ID, buildNotification())
            }
            ACTION_UPDATE -> {
                activeCount = intent.getIntExtra(EXTRA_COUNT, 0)
                val nm = getSystemService(NotificationManager::class.java)
                nm.notify(NOTIFICATION_ID, buildNotification())
                if (activeCount == 0) {
                    stopForeground(STOP_FOREGROUND_REMOVE)
                    stopSelf()
                }
            }
            ACTION_STOP -> {
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
        }
        return START_STICKY
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            "Titan Connections",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "Active MUD connections"
            setShowBadge(false)
        }
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pending = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val text = if (activeCount == 1) "1 active connection"
                   else "$activeCount active connections"

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Titan")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_share)
            .setContentIntent(pending)
            .setOngoing(true)
            .setSilent(true)
            .build()
    }

    companion object {
        const val CHANNEL_ID = "titan_connections"
        const val NOTIFICATION_ID = 1
        const val ACTION_START = "org.tinymux.titan.START"
        const val ACTION_UPDATE = "org.tinymux.titan.UPDATE"
        const val ACTION_STOP = "org.tinymux.titan.STOP"
        const val EXTRA_COUNT = "count"
    }
}
