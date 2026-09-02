package com.skigatechono.sgc_phone

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder

/**
 * V1.35: foreground priority for native BLE downloads.
 *
 * Bench evidence 2026-09-02: nRF Connect (mcp) downloaded a full run on the
 * same phone/FW/params with blocks:0 while our app wedged mid-transfer.
 * mcp's own earlier link ALSO died with status=8, so the wedge is stochastic
 * — but Samsung's Freecess power manager (seen in the same logcat) treats
 * our app as throttle-able while mcp runs as a privileged-by-usage foreground
 * tool. When our process is CPU-deprioritized, the host drains HCI slower,
 * the phone's controller RX buffers back up, and the LL stalls → wedge.
 *
 * This service does no BLE work itself. It simply exists in foreground state
 * (with an ongoing notification) for the duration of a native batch, which
 * gives our process foreground scheduling priority and keeps Samsung's
 * freezer away. It is also the foundation for Phase 3 background Auto-Sync.
 */
class NativeDownloadService : Service() {

    companion object {
        private const val CHANNEL_ID = "sgc_download"
        private const val NOTIF_ID = 42

        fun start(context: Context) {
            try {
                context.startForegroundService(Intent(context, NativeDownloadService::class.java))
            } catch (_: Exception) {}
        }

        fun stop(context: Context) {
            try { context.stopService(Intent(context, NativeDownloadService::class.java)) } catch (_: Exception) {}
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "SGC downloads", NotificationManager.IMPORTANCE_LOW)
            )
        }
        val builder = if (Build.VERSION.SDK_INT >= 26) Notification.Builder(this, CHANNEL_ID)
                      else Notification.Builder(this)
        val notification = builder
            .setContentTitle("Ski Gate Chrono")
            .setContentText("Downloading runs from device…")
            .setSmallIcon(android.R.drawable.stat_sys_download)
            .setOngoing(true)
            .build()
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIF_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIF_ID, notification)
        }
        return START_STICKY
    }
}
