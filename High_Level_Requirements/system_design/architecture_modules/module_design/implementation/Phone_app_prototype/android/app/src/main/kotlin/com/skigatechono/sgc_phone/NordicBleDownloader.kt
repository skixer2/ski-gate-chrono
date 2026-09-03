package com.skigatechono.sgc_phone

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.util.Log
import no.nordicsemi.android.ble.BleManager
import no.nordicsemi.android.ble.response.ReadResponse
import java.io.ByteArrayOutputStream
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.zip.CRC32

/**
 * Native SGC downloader built on Nordic's hardened Android BLE Library.
 *
 * The previous hand-rolled BluetoothGatt engine proved the protocol works but
 * kept re-implementing Android queue/reconnect edge cases that Nordic already
 * solved. BleManager serializes GATT operations on its own worker thread and
 * provides synchronous await() calls, so the FT protocol stays simple here.
 */
@SuppressLint("MissingPermission")
class NordicBleDownloader(private val context: Context) {

    companion object {
        private const val TAG = "SGC_NORDIC"
        private val SERVICE_UUID: UUID = UUID.fromString("53470000-0000-1000-8000-00805F9B34FB")
        private val CHAR_STATE: UUID = UUID.fromString("5347ABC4-0000-1000-8000-00805F9B34FB")
        private val CHAR_BATTERY: UUID = UUID.fromString("5347ABC5-0000-1000-8000-00805F9B34FB")
        private val CHAR_FLASH_USED: UUID = UUID.fromString("5347ABC7-0000-1000-8000-00805F9B34FB")
        private val CHAR_RUN_INFO: UUID = UUID.fromString("5347ABC8-0000-1000-8000-00805F9B34FB")
        private val CHAR_RUN_LIST: UUID = UUID.fromString("5347ABC9-0000-1000-8000-00805F9B34FB")
        private val CHAR_FT_REQUEST: UUID = UUID.fromString("5347ABCA-0000-1000-8000-00805F9B34FB")
        private val CHAR_FT_STREAM: UUID = UUID.fromString("5347ABCD-0000-1000-8000-00805F9B34FB")
        private val CHAR_CAL: UUID = UUID.fromString("5347ABD0-0000-1000-8000-00805F9B34FB")

        private const val FT_IDLE_TIMEOUT_MS = 20_000L
        private const val FT_TOTAL_TIMEOUT_MS = 90_000L
        private const val MAX_RESUME_ATTEMPTS = 10  /* V1.34: 6 → 10 — resume
            reconnects hit the device's post-abort recovery window (status 147)
            or transient phone-stack throttling; both heal in seconds. */
        /* V1.39: 12 s -> 4 s post-connect settle.
           V1.39 CRITICAL BUG FIX: data.value is the raw ByteArray from
           Android's shared buffer. Without clone(), the buffer is overwritten
           by the next incoming notification before our consumer thread polls
           it. This caused silent packet corruption, random "device FT error"
           type parsing exceptions, and payload CRC failures, explaining
           exactly why FBP and 1.31-1.38 wedged constantly while nRF Connect
           (which clones the bytes) never did. Settle reduced back to 4 s
           since the link no longer corrupts mid-stream. */
        private const val CONNECT_SETTLE_MS = 4_000L
    }

    data class DownloadedRun(val id: Int, val timestamp: Int, val data: ByteArray)
    data class FailedRun(val id: Int, val reason: String)
    data class BatchResult(val runs: List<DownloadedRun>, val failed: List<FailedRun>, val log: List<String>)

    private val logs = mutableListOf<String>()
    @Volatile private var cancelled = false

    /** V1.32: live event sink (wired to Flutter via sgc_native_ble_events).
        The batch call only returns at the END — without this the UI shows
        nothing for tens of seconds during connect/FT ("pressed, nothing
        happened"). Set from MainActivity on the main thread. */
    @Volatile var onEvent: ((Map<String, Any>) -> Unit)? = null

    private fun emit(ev: Map<String, Any>) {
        try { onEvent?.invoke(ev) } catch (_: Exception) {}
    }

    private fun log(msg: String) {
        Log.d(TAG, msg)
        synchronized(logs) { logs.add(msg) }
        emit(mapOf("type" to "log", "msg" to msg))
    }

    fun cancel() {
        cancelled = true
        log("cancel requested")
    }

    private inner class SgcBleManager(context: Context) : BleManager(context) {
        var stateChar: BluetoothGattCharacteristic? = null
        var batteryChar: BluetoothGattCharacteristic? = null
        var flashChar: BluetoothGattCharacteristic? = null
        var runInfoChar: BluetoothGattCharacteristic? = null
        var runListChar: BluetoothGattCharacteristic? = null
        var ftRequestChar: BluetoothGattCharacteristic? = null
        var ftStreamChar: BluetoothGattCharacteristic? = null
        var calChar: BluetoothGattCharacteristic? = null

        val ftPackets = LinkedBlockingQueue<ByteArray>()

        override fun getMinLogPriority(): Int = Log.DEBUG

        override fun log(priority: Int, message: String) {
            Log.println(priority, TAG, message)
        }

        override fun isRequiredServiceSupported(gatt: BluetoothGatt): Boolean {
            val svc = gatt.getService(SERVICE_UUID) ?: return false
            stateChar = svc.getCharacteristic(CHAR_STATE)
            batteryChar = svc.getCharacteristic(CHAR_BATTERY)
            flashChar = svc.getCharacteristic(CHAR_FLASH_USED)
            runInfoChar = svc.getCharacteristic(CHAR_RUN_INFO)
            runListChar = svc.getCharacteristic(CHAR_RUN_LIST)
            ftRequestChar = svc.getCharacteristic(CHAR_FT_REQUEST)
            ftStreamChar = svc.getCharacteristic(CHAR_FT_STREAM)
            calChar = svc.getCharacteristic(CHAR_CAL)
            return ftRequestChar != null && ftStreamChar != null
        }

        override fun initialize() {
            // V1.40: Force High Connection Priority to get a 11.25-15 ms interval.
            // On the S22, default/balanced intervals (~60 ms) running at 60 ms SGC packet
            // cadence operate at 100% duty cycle, meaning any single RF/antenna miss immediately
            // exhausts the queue and collapses the link (status=8). High priority gives the link
            // 4x retransmission headroom per packet, making it incredibly robust.
            requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH).enqueue()

            // Match the known-good nRF Connect GATT state: subscribe to the
            // full SGC notify set, then request MTU. BleManager serializes
            // all of these operations on its proven queue.
            setNotificationCallback(stateChar).with { _, _ -> }
            setNotificationCallback(batteryChar).with { _, _ -> }
            setNotificationCallback(flashChar).with { _, _ -> }
            setNotificationCallback(runInfoChar).with { _, _ -> }
            setNotificationCallback(calChar).with { _, _ -> }
            setNotificationCallback(ftStreamChar).with { _, data ->
                data.value?.clone()?.let { ftPackets.offer(it) }
            }

            enableNotifications(stateChar).enqueue()
            enableNotifications(batteryChar).enqueue()
            enableNotifications(flashChar).enqueue()
            enableNotifications(runInfoChar).enqueue()
            enableNotifications(ftStreamChar).enqueue()
            enableNotifications(calChar).enqueue()
            requestMtu(247).enqueue()
        }

        override fun onServicesInvalidated() {
            stateChar = null
            batteryChar = null
            flashChar = null
            runInfoChar = null
            runListChar = null
            ftRequestChar = null
            ftStreamChar = null
            calChar = null
            ftPackets.clear()
        }

        fun connectToSgc(address: String) {
            val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
                ?: throw Exception("BluetoothAdapter unavailable")
            if (!adapter.isEnabled) throw Exception("Bluetooth is off")
            val device = try {
                adapter.getRemoteDevice(address)
            } catch (e: Exception) {
                throw Exception("bad BLE address '$address': ${e.message}")
            }
            log("Nordic BleManager connecting to $address")
            connect(device)
                .retry(5, 500)  /* V1.34: 3/250 → 5/500 — first connect after
                    the device's post-abort BLE recover often fails fast (147) */
                .timeout(15_000)
                .useAutoConnect(false)
                .await()
            log("Nordic BleManager ready; settling ${CONNECT_SETTLE_MS} ms")
            Thread.sleep(CONNECT_SETTLE_MS)
        }

        fun writeFtStart(runId: Int, offset: Int) {
            val c = ftRequestChar ?: throw Exception("FT request characteristic unavailable")
            val cmd = ByteArray(if (offset > 0) 7 else 3)
            cmd[0] = 0
            cmd[1] = (runId and 0xFF).toByte()
            cmd[2] = ((runId shr 8) and 0xFF).toByte()
            if (offset > 0) {
                cmd[3] = (offset and 0xFF).toByte()
                cmd[4] = ((offset shr 8) and 0xFF).toByte()
                cmd[5] = ((offset shr 16) and 0xFF).toByte()
                cmd[6] = ((offset shr 24) and 0xFF).toByte()
            }
            writeCharacteristic(c, cmd, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT).await()
        }

        fun readRunList(): String {
            val c = runListChar ?: throw Exception("run-list characteristic unavailable")
            // The no-arg await() is void (TimeoutableRequest); the typed await(Class)
            // returns a filled ReadResponse, whose rawData (Data) carries the bytes.
            val response = readCharacteristic(c).await(ReadResponse::class.java)
            val data = response.rawData?.value ?: ByteArray(0)
            return String(data, Charsets.UTF_8)
        }

        fun shutdown() {
            try {
                if (isConnected) {
                    disconnect().timeout(3_000).await()
                }
            } catch (e: Exception) {
                log("disconnect during shutdown failed: ${e.message}")
            }
            try { close() } catch (_: Exception) {}
        }
    }

    private fun le32(data: ByteArray, offset: Int): Long {
        return (data[offset].toLong() and 0xFF) or
            ((data[offset + 1].toLong() and 0xFF) shl 8) or
            ((data[offset + 2].toLong() and 0xFF) shl 16) or
            ((data[offset + 3].toLong() and 0xFF) shl 24)
    }

    private fun streamCrc32(data: ByteArray): Long {
        val crc = CRC32()
        crc.update(data)
        return crc.value
    }

    private fun parseTimestamp(data: ByteArray): Int {
        if (data.size < 6) return 0
        return le32(data, 2).toInt()
    }

    private fun waitForAdvertisement(address: String, timeoutMs: Long): Boolean {
        val adapter = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        val scanner = adapter?.bluetoothLeScanner ?: return false
        val found = CountDownLatch(1)
        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                if (result.device.address.equals(address, ignoreCase = true)) found.countDown()
            }

            override fun onBatchScanResults(results: MutableList<ScanResult>) {
                if (results.any { it.device.address.equals(address, ignoreCase = true) }) found.countDown()
            }
        }
        return try {
            val filter = ScanFilter.Builder().setDeviceAddress(address).build()
            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build()
            scanner.startScan(listOf(filter), settings, callback)
            val seen = found.await(timeoutMs, TimeUnit.MILLISECONDS)
            if (seen) log("advertisement seen for $address")
            seen
        } catch (e: Exception) {
            log("advertisement wait failed: ${e.message}")
            false
        } finally {
            try { scanner.stopScan(callback) } catch (_: Exception) {}
        }
    }

    private fun newConnectedManager(address: String): SgcBleManager {
        val manager = SgcBleManager(context)
        try {
            manager.connectToSgc(address)
            return manager
        } catch (e: Exception) {
            try { manager.shutdown() } catch (_: Exception) {}
            throw e
        }
    }

    private fun downloadOne(address: String, runId: Int): DownloadedRun {
        val buffer = ByteArrayOutputStream()
        var expected = 0
        var lastError: Exception? = null
        var manager: SgcBleManager? = null
        var lastProgressMs = 0L

        for (attempt in 1..MAX_RESUME_ATTEMPTS) {
            if (cancelled) throw Exception("cancelled")
            try {
                if (manager == null || !manager.isConnected) {
                    try { manager?.shutdown() } catch (_: Exception) {}
                    manager = newConnectedManager(address)
                }
                val m = manager!!
                m.ftPackets.clear()

                val offset = buffer.size()
                log("FT run #$runId attempt $attempt offset=$offset")
                m.writeFtStart(runId, offset)

                val totalDeadline = System.currentTimeMillis() + FT_TOTAL_TIMEOUT_MS
                var progressDeadline = System.currentTimeMillis() + FT_IDLE_TIMEOUT_MS

                while (System.currentTimeMillis() < totalDeadline && !cancelled) {
                    val waitMs = minOf(1_000L, progressDeadline - System.currentTimeMillis())
                    if (waitMs <= 0) throw Exception("FT idle timeout at ${buffer.size()} B")
                    if (!m.isConnected) throw Exception("FT link lost at ${buffer.size()} B")
                    val pkt = m.ftPackets.poll(waitMs, TimeUnit.MILLISECONDS) ?: continue
                    if (pkt.isEmpty()) continue
                    when (pkt[0].toInt() and 0xFF) {
                        0x01 -> {
                            if (pkt.size >= 7) {
                                expected = le32(pkt, 3).toInt()
                                log("FT START run #$runId total=$expected")
                            }
                        }
                        0x02 -> {
                            if (pkt.size >= 2) {
                                buffer.write(pkt, 2, pkt.size - 2)
                                progressDeadline = System.currentTimeMillis() + FT_IDLE_TIMEOUT_MS
                                val nowMs = System.currentTimeMillis()
                                if (nowMs - lastProgressMs >= 250) {
                                    lastProgressMs = nowMs
                                    emit(mapOf(
                                        "type" to "ft_progress",
                                        "runId" to runId,
                                        "bytes" to buffer.size(),
                                        "expected" to expected,
                                    ))
                                }
                            }
                        }
                        0x03 -> {
                            val deviceCrc = if (pkt.size >= 5) le32(pkt, 1) else null
                            val data = buffer.toByteArray()
                            if (expected > 0 && data.size < expected) {
                                throw Exception("short transfer (${data.size}/$expected B)")
                            }
                            if (deviceCrc != null) {
                                val local = streamCrc32(data)
                                if (local != deviceCrc) {
                                    throw Exception("stream CRC mismatch device=0x${java.lang.Long.toHexString(deviceCrc)} local=0x${java.lang.Long.toHexString(local)}")
                                }
                            }
                            log("FT DONE run #$runId bytes=${data.size} attempts=$attempt")
                            return DownloadedRun(runId, parseTimestamp(data), data)
                        }
                        0x04 -> {
                            val code = if (pkt.size > 1) pkt[1].toInt() and 0xFF else 0
                            throw Exception("device FT error 0x${code.toString(16)} at ${buffer.size()} B")
                        }
                        else -> log("unknown FT packet 0x${(pkt[0].toInt() and 0xFF).toString(16)}")
                    }
                }
                throw Exception("FT total timeout at ${buffer.size()} B")
            } catch (e: Exception) {
                lastError = e
                log("FT run #$runId attempt $attempt failed at ${buffer.size()} B: ${e.message}")
                try { manager?.shutdown() } catch (_: Exception) {}
                manager = null
                if (attempt < MAX_RESUME_ATTEMPTS) {
                    Thread.sleep((2_000L * attempt).coerceAtMost(8_000L))
                    if (waitForAdvertisement(address, 10_000L)) {
                        /* V1.34: the device JUST recovered its BLE stack
                           (post-abort force-recover). Give it 1 s before
                           connectGatt or the connect hits status 147. */
                        Thread.sleep(1_000)
                    }
                }
            }
        }
        try { manager?.shutdown() } catch (_: Exception) {}
        throw lastError ?: Exception("FT failed")
    }

    fun downloadRuns(address: String, runIds: List<Int>): BatchResult {
        cancelled = false
        synchronized(logs) { logs.clear() }
        val done = mutableListOf<DownloadedRun>()
        val failed = mutableListOf<FailedRun>()

        log("nordic batch start: address=$address runs=${runIds.joinToString(",") { "#$it" }}")
        for ((index, runId) in runIds.withIndex()) {
            if (cancelled) break
            if (index > 0) Thread.sleep(750)
            try {
                val run = downloadOne(address, runId)
                done.add(run)
                // V1.41: Stream completed run bytes back to Dart immediately so the
                // UI can save them incrementally without connection-churn disconnects!
                emit(mapOf(
                    "type" to "ft_run_complete",
                    "runId" to run.id,
                    "timestamp" to run.timestamp,
                    "data" to run.data
                ))
            } catch (e: Exception) {
                log("run #$runId failed: ${e.message}")
                failed.add(FailedRun(runId, e.message ?: e.javaClass.simpleName))
            }
        }

        log("nordic batch end: ok=${done.size} failed=${failed.size}")
        return BatchResult(done, failed, synchronized(logs) { logs.toList() })
    }

    fun readRunListJson(address: String): String {
        val manager = newConnectedManager(address)
        return try {
            manager.readRunList()
        } finally {
            manager.shutdown()
        }
    }
}
