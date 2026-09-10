package com.skigatechono.sgc_phone

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.util.Log
import no.nordicsemi.android.ble.BleManager
import java.io.ByteArrayOutputStream
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.zip.CRC32

/**
 * SGC downloader — FW 5.77 V2 STREAMING PULL (PULL_TRANSFER_REDESIGN.md V2).
 *
 * The simplest possible protocol (JP's 15:51 simplification):
 *   Phone: "s <runId> 0 <total>"  (one command, whole run)
 *   Device: streams binary frames [seq:2B][len:1B][≤240B] back-to-back
 *           (no timer pacing — queue capacity paces), then [0xFF 0xFF 0x00]
 *   Phone: assembles blob, verifies trailer CRC, done.
 *
 * On ANY failure → teardown → reconnect → restart from scratch (2 s).
 * No resume, no partial buffers, no offset tracking.
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
        private val CHAR_CONSOLE: UUID = UUID.fromString("5347ABCA-0000-1000-8000-00805F9B34FB")
        private val CHAR_CAL: UUID = UUID.fromString("5347ABD0-0000-1000-8000-00805F9B34FB")

        /* Per-attempt timeout: clean 39 KB at 20+ KB/s ≈ 2 s. 10 s covers
           slow starts. On failure → restart from scratch. */
        private const val STREAM_TIMEOUT_MS = 10_000L
        private const val MAX_ATTEMPTS = 8
        private const val CONNECT_SETTLE_MS = 4_000L   /* V2: 12 s reverted — no benefit measured */
    }

    data class DownloadedRun(val id: Int, val timestamp: Int, val data: ByteArray)
    data class FailedRun(val id: Int, val reason: String)
    data class BatchResult(val runs: List<DownloadedRun>, val failed: List<FailedRun>, val log: List<String>)

    data class PullDirEntry(val id: Int, val ts: Int, val sz: Int, val side: Int)

    private val logs = mutableListOf<String>()
    @Volatile private var cancelled = false

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
        var consoleChar: BluetoothGattCharacteristic? = null
        var calChar: BluetoothGattCharacteristic? = null

        /** Binary stream frames + ASCII lines from the console char. */
        val consolePackets = LinkedBlockingQueue<ByteArray>()

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
            consoleChar = svc.getCharacteristic(CHAR_CONSOLE)
            calChar = svc.getCharacteristic(CHAR_CAL)
            return consoleChar != null
        }

        override fun initialize() {
            // V1.40: High priority → 11.25–15 ms interval (link headroom).
            requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH).enqueue()

            // V2: Request LE 2M PHY for maximum throughput.
            try {
                setPreferredPhy(2, 2, 1).enqueue()   // LE_2M, LE_2M, NO_PACKET_COMPRESSION
                log("V2: 2M PHY requested")
            } catch (e: Exception) {
                log("V2: 2M PHY not supported: ${e.message}")
            }

            setNotificationCallback(stateChar).with { _, _ -> }
            setNotificationCallback(batteryChar).with { _, _ -> }
            setNotificationCallback(flashChar).with { _, _ -> }
            setNotificationCallback(runInfoChar).with { _, _ -> }
            setNotificationCallback(calChar).with { _, _ -> }
            setNotificationCallback(consoleChar).with { _, data ->
                // V1.39 discipline: clone the shared Android buffer immediately.
                data.value?.clone()?.let { consolePackets.offer(it) }
            }

            enableNotifications(stateChar).enqueue()
            enableNotifications(batteryChar).enqueue()
            enableNotifications(flashChar).enqueue()
            enableNotifications(runInfoChar).enqueue()
            enableNotifications(consoleChar).enqueue()
            enableNotifications(calChar).enqueue()
            requestMtu(517).enqueue()
        }

        override fun onServicesInvalidated() {
            stateChar = null; batteryChar = null; flashChar = null
            runInfoChar = null; consoleChar = null; calChar = null
            consolePackets.clear()
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
            log("V2: connecting to $address")
            connect(device)
                .retry(5, 500)
                .timeout(15_000)
                .useAutoConnect(false)
                .await()
            log("V2: connected; settling ${CONNECT_SETTLE_MS} ms")
            Thread.sleep(CONNECT_SETTLE_MS)
        }

        fun sendRequest(line: String) {
            val c = consoleChar ?: throw Exception("console characteristic unavailable")
            writeCharacteristic(c, line.toByteArray(Charsets.US_ASCII),
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT).await()
        }

        fun shutdown() {
            try {
                if (isConnected) disconnect().timeout(3_000).await()
            } catch (e: Exception) {
                log("disconnect during shutdown failed: ${e.message}")
            }
            try { close() } catch (_: Exception) {}
        }
    }

    private fun le16(d: ByteArray, o: Int): Int =
        (d[o].toInt() and 0xFF) or ((d[o + 1].toInt() and 0xFF) shl 8)

    private fun le32(d: ByteArray, o: Int): Long =
        (d[o].toLong() and 0xFF) or ((d[o + 1].toLong() and 0xFF) shl 8) or
        ((d[o + 2].toLong() and 0xFF) shl 16) or ((d[o + 3].toLong() and 0xFF) shl 24)

    private fun parseTimestamp(data: ByteArray): Int {
        if (data.size < 6) return 0
        return le32(data, 2).toInt()
    }

    private fun verifyTrailerCrc(blob: ByteArray) {
        if (blob.size < 22) throw Exception("blob too short (${blob.size} B)")
        if ((blob[blob.size - 6].toInt() and 0xFF) != 0xC3 ||
            (blob[blob.size - 5].toInt() and 0xFF) != 0x32) {
            throw Exception("CRC trailer magic mismatch")
        }
        val expected = le32(blob, blob.size - 4)
        val crc = CRC32()
        crc.update(blob, 16, blob.size - 22)
        if (crc.value != expected) {
            throw Exception("blob CRC mismatch device=0x${java.lang.Long.toHexString(expected)} local=0x${java.lang.Long.toHexString(crc.value)}")
        }
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
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
            scanner.startScan(listOf(filter), settings, callback)
            val seen = found.await(timeoutMs, TimeUnit.MILLISECONDS)
            if (seen) log("advertisement seen for $address")
            seen
        } catch (e: Exception) {
            log("advertisement wait failed: ${e.message}"); false
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

    /**
     * Fetch the run directory via "D" (ASCII lines on console char).
     */
    private fun pullDirectory(m: SgcBleManager): List<PullDirEntry> {
        m.consolePackets.clear()
        m.sendRequest("D")
        val out = mutableListOf<PullDirEntry>()
        val deadline = System.currentTimeMillis() + 10_000L
        while (System.currentTimeMillis() < deadline && !cancelled) {
            val pkt = m.consolePackets.poll(3, TimeUnit.MILLISECONDS) ?: continue
            val text = String(pkt, Charsets.US_ASCII).trim()
            if (text.startsWith("{\"run\"")) {
                val id = Regex("\"run\":(\\d+)").find(text)?.groupValues?.get(1)?.toIntOrNull()
                val ts = Regex("\"ts\":(\\d+)").find(text)?.groupValues?.get(1)?.toIntOrNull()
                val sz = Regex("\"sz\":(\\d+)").find(text)?.groupValues?.get(1)?.toIntOrNull()
                val side = Regex("\"side\":(\\d+)").find(text)?.groupValues?.get(1)?.toIntOrNull()
                if (id != null && sz != null) out.add(PullDirEntry(id, ts ?: 0, sz, side ?: 0))
            } else if (text.startsWith("{\"d_end\"")) {
                return out
            }
            /* V2: silently ignore echo lines ("D", "s ...") — no more
               "unexpected line" noise in the UI (JP's 12:57 complaint). */
        }
        throw Exception("D: directory timeout")
    }

    /**
     * Download one run via V2 streaming pull. On ANY failure: teardown →
     * reconnect → restart from scratch (JP: "2 s is so short there probably
     * won't even be a disconnection").
     */
    private fun downloadOne(address: String, runId: Int,
                            dirCache: MutableMap<Int, PullDirEntry>): DownloadedRun {
        var lastError: Exception? = null
        var manager: SgcBleManager? = null

        for (attempt in 1..MAX_ATTEMPTS) {
            if (cancelled) throw Exception("cancelled")
            try {
                if (manager == null || !manager.isConnected) {
                    try { manager?.shutdown() } catch (_: Exception) {}
                    manager = newConnectedManager(address)
                }
                val m = manager!!

                if (runId !in dirCache) {
                    pullDirectory(m).forEach { dirCache[it.id] = it }
                }
                val entry = dirCache[runId]
                    ?: throw Exception("run #$runId not in device directory")
                val total = entry.sz

                log("V2 stream run #$runId total=$total attempt $attempt")
                m.consolePackets.clear()

                /* ONE command — the whole run from offset 0 (no resume). */
                m.sendRequest("s $runId 0 $total")

                /* Receive binary frames until [0xFF 0xFF 0x00] end marker. */
                val buffer = ByteArrayOutputStream()
                val deadline = System.currentTimeMillis() + STREAM_TIMEOUT_MS
                var lastProgressMs = 0L

                while (System.currentTimeMillis() < deadline && !cancelled) {
                    if (!m.isConnected) throw Exception("link lost at ${buffer.size()} B")
                    val pkt = m.consolePackets.poll(200, TimeUnit.MILLISECONDS) ?: continue
                    if (pkt.size < 3) continue

                    val seq = le16(pkt, 0)
                    if (seq == 0xFFFF) {
                        /* End marker — verify and return */
                        val data = buffer.toByteArray()
                        if (data.size < total) {
                            throw Exception("short stream (${data.size}/$total B)")
                        }
                        verifyTrailerCrc(data)
                        log("V2 stream DONE run #$runId bytes=${data.size} attempt=$attempt")
                        return DownloadedRun(runId, parseTimestamp(data), data)
                    }

                    val len = pkt[2].toInt() and 0xFF
                    if (pkt.size < 3 + len) continue   /* truncated — skip */
                    buffer.write(pkt, 3, len)

                    val nowMs = System.currentTimeMillis()
                    if (nowMs - lastProgressMs >= 250) {
                        lastProgressMs = nowMs
                        emit(mapOf(
                            "type" to "ft_progress",
                            "runId" to runId,
                            "bytes" to buffer.size(),
                            "expected" to total,
                        ))
                    }
                }
                throw Exception("stream timeout at ${buffer.size()} B")

            } catch (e: Exception) {
                lastError = e
                log("V2 run #$runId attempt $attempt failed: ${e.message}")
                try { manager?.shutdown() } catch (_: Exception) {}
                manager = null
                if (attempt < MAX_ATTEMPTS) {
                    Thread.sleep((1_500L * attempt).coerceAtMost(6_000L))
                    if (waitForAdvertisement(address, 10_000L)) {
                        Thread.sleep(500)
                    }
                }
            }
        }
        try { manager?.shutdown() } catch (_: Exception) {}
        throw lastError ?: Exception("stream pull failed")
    }

    fun downloadRuns(address: String, runIds: List<Int>): BatchResult {
        cancelled = false
        synchronized(logs) { logs.clear() }
        val done = mutableListOf<DownloadedRun>()
        val failed = mutableListOf<FailedRun>()
        val dirCache = mutableMapOf<Int, PullDirEntry>()

        log("V2 batch start: address=$address runs=${runIds.joinToString(",") { "#$it" }}")
        for ((index, runId) in runIds.withIndex()) {
            if (cancelled) break
            if (index > 0) Thread.sleep(500)
            try {
                val run = downloadOne(address, runId, dirCache)
                done.add(run)
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

        log("V2 batch end: ok=${done.size} failed=${failed.size}")
        return BatchResult(done, failed, synchronized(logs) { logs.toList() })
    }

    fun readRunListJson(address: String): String {
        val manager = newConnectedManager(address)
        return try {
            val entries = pullDirectory(manager)
            entries.joinToString(",", "[", "]") { e ->
                "{\"id\":${e.id},\"ts\":${e.ts},\"size\":${e.sz - 22},\"side\":${e.side}}"
            }
        } finally {
            manager.shutdown()
        }
    }
}
