package com.skigatechono.sgc_phone

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.view.WindowManager
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private val channelName = "sgc_native_ble"
    private val eventsChannelName = "sgc_native_ble_events"
    private var downloader: NordicBleDownloader? = null
    private var wakeLock: PowerManager.WakeLock? = null

    /** V1.34: during a native batch, keep the screen on and hold a partial
        wake lock. Bench 2026-09-02: resume reconnect attempts 3–5 died with
        GATT_TIMEOUT/147 because the app was backgrounded mid-batch and
        Android throttled its BLE stack. */
    private fun enterDownloadMode() {
        runOnUiThread { window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
        try {
            val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
            val lock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "sgc:native_download")
            lock.acquire(15 * 60 * 1000L)  // hard cap 15 min
            wakeLock = lock
        } catch (_: Exception) {}
    }

    private fun exitDownloadMode() {
        runOnUiThread { window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
        try { wakeLock?.let { if (it.isHeld) it.release() } } catch (_: Exception) {}
        wakeLock = null
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        downloader = NordicBleDownloader(applicationContext)

        // V1.32: native -> Dart live events (log lines + FT byte progress).
        val eventsChannel = MethodChannel(flutterEngine.dartExecutor.binaryMessenger, eventsChannelName)
        downloader?.onEvent = { ev ->
            Handler(Looper.getMainLooper()).post { eventsChannel.invokeMethod("onEvent", ev) }
        }

        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, channelName)
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "downloadRuns" -> {
                        val address = call.argument<String>("address")
                        val runIds = call.argument<List<Int>>("runIds") ?: emptyList()
                        if (address.isNullOrBlank() || runIds.isEmpty()) {
                            result.error("bad_args", "address and runIds are required", null)
                            return@setMethodCallHandler
                        }
                        val engine = downloader ?: run {
                            result.error("not_ready", "native BLE downloader not initialized", null)
                            return@setMethodCallHandler
                        }
                        Thread {
                            enterDownloadMode()
                            try {
                                val batch = engine.downloadRuns(address, runIds)
                                val response = mapOf(
                                    "runs" to batch.runs.map {
                                        mapOf(
                                            "id" to it.id,
                                            "timestamp" to it.timestamp,
                                            "size" to it.data.size,
                                            "data" to it.data,
                                        )
                                    },
                                    "failed" to batch.failed.map {
                                        mapOf("id" to it.id, "reason" to it.reason)
                                    },
                                    "log" to batch.log,
                                )
                                postSuccess(result, response)
                            } catch (e: Exception) {
                                postError(result, "native_ble", e.message ?: e.javaClass.simpleName, null)
                            } finally {
                                exitDownloadMode()
                            }
                        }.start()
                    }
                    "readRunList" -> {
                        val address = call.argument<String>("address")
                        if (address.isNullOrBlank()) {
                            result.error("bad_args", "address is required", null)
                            return@setMethodCallHandler
                        }
                        val engine = downloader ?: run {
                            result.error("not_ready", "native BLE downloader not initialized", null)
                            return@setMethodCallHandler
                        }
                        Thread {
                            try {
                                postSuccess(result, engine.readRunListJson(address))
                            } catch (e: Exception) {
                                postError(result, "native_ble", e.message ?: e.javaClass.simpleName, null)
                            }
                        }.start()
                    }
                    "cancel" -> {
                        downloader?.cancel()
                        result.success(null)
                    }
                    else -> result.notImplemented()
                }
            }
    }

    private fun postSuccess(result: MethodChannel.Result, value: Any?) {
        Handler(Looper.getMainLooper()).post { result.success(value) }
    }

    private fun postError(result: MethodChannel.Result, code: String, message: String, details: Any?) {
        Handler(Looper.getMainLooper()).post { result.error(code, message, details) }
    }
}
