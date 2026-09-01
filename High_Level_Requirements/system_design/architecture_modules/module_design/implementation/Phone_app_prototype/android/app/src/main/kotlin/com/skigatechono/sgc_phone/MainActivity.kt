package com.skigatechono.sgc_phone

import android.os.Handler
import android.os.Looper
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private val channelName = "sgc_native_ble"
    private var downloader: NordicBleDownloader? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        downloader = NordicBleDownloader(applicationContext)

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
