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
 * SGC downloader — FW 5.76 PHONE-PULL protocol ("serial replacement").
 *
 * PULL_TRANSFER_REDESIGN.md (App 1.43): the phone drives every byte. It
 * writes ASCII requests to the console characteristic (ABCA) and receives
 * text frames back on the same characteristic:
 *   "D"                  → {"run":id,"ts":...,"sz":total,"side":...} lines
 *                          terminated by {"d_end":n}
 *   "r <id> <off> <len>" → "[<seq> <n> <HEX>]" frames, then "OK <sent>"
 *                          or "E <code>"
 *
 * The blob layout is unchanged (16B header + payload + 6B CRC trailer), so
 * the Dart decompressor/storage pipeline is untouched. The device is
 * stateless: a disconnect at any moment costs one reconnect and the next
 * request continues from the last received offset.
 *
 * Kept from 1.40–1.42: CONNECTION_PRIORITY_HIGH, notification byte clone()
 * (V1.39 — Android overwrites the shared callback buffer), connect retry
 * 5/500, ADV-wait before reconnect, incremental per-run streaming to Dart.
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

        /* Per-request idle timeout: the device answers a request in ~150 ms
           (5 paced frames + OK). 8 s covers reconnect-class stalls before the
           attempt loop takes over. Device-side watchdog is 2 s (pull_wdt). */
        private const val PULL_REQ_TIMEOUT_MS = 8_000L
        private const val PULL_TOTAL_TIMEOUT_MS = 120_000L
        private const val PULL_CHUNK = 512
        /* 5.76 bench 14:40: wedges cluster inside the first ~15 s post-connect
           (churn window). 8 attempts × stateless continue = batch completes. */
        private const val MAX_FRESH_ATTEMPTS = 8
        /* 1.37 A/B finally running: 4 s → 12 s settle — start pulling outside
           Android's connection-parameter churn window (nRF Connect profile). */
        private const val CONNECT_SETTLE_MS = 12_000L
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

        val consoleLines = LinkedBlockingQueue<ByteArray>()

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

            setNotificationCallback(stateChar).with { _, _ -> }
            setNotificationCallback(batteryChar).with { _, _ -> }
            setNotificationCallback(flashChar).with { _, _ -> }
            setNotificationCallback(runInfoChar).with { _, _ -> }
            setNotificationCallback(calChar).with { _, _ -> }
            setNotificationCallback(consoleChar).with { _, data ->
                // V1.39 discipline: clone the shared Android buffer immediately.
                data.value?.clone()?.let { consoleLines.offer(it) }
            }

            enableNotifications(stateChar).enqueue()
            enableNotifications(batteryChar).enqueue()
            enableNotifications(flashChar).enqueue()
            enableNotifications(runInfoChar).enqueue()
            enableNotifications(consoleChar).enqueue()
            enableNotifications(calChar).enqueue()
            // F38: request the largest MTU; device caps at 247 (parsing is
            // line-based, so any negotiated value works).
            requestMtu(517).enqueue()
        }

        override fun onServicesInvalidated() {
            stateChar = null
            batteryChar = null
            flashChar = null
            runInfoChar = null
            consoleChar = null
            calChar = null
            consoleLines.clear()
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
            log("pull: connecting to $address")
            connect(device)
                .retry(5, 500)
                .timeout(15_000)
                .useAutoConnect(false)
                .await()
            log("pull: connected; settling ${CONNECT_SETTLE_MS} ms")
            Thread.sleep(CONNECT_SETTLE_MS)
        }

        fun sendRequest(line: String) {
            val c = consoleChar ?: throw Exception("console characteristic unavailable")
            writeCharacteristic(c, line.toByteArray(Charsets.US_ASCII), BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT).await()
        }

        /** Await one text line from the console (or null on timeout). */
        fun readLine(timeoutMs: Long): String? {
            val pkt = consoleLines.poll(timeoutMs, TimeUnit.MILLISECONDS) ?: return null
            return String(pkt, Charsets.US_ASCII).trim()
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

    private fun parseTimestamp(data: ByteArray): Int {
        if (data.size < 6) return 0
        return le32(data, 2).toInt()
    }

    /* ── Pull protocol helpers ─────────────────────────────────────── */

    /** Fetch the run directory via "D". */
    private fun pullDirectory(m: SgcBleManager, timeoutPerLineMs: Long = 3_000L): List<PullDirEntry> {
        m.consoleLines.clear()
        m.sendRequest("D")
        val out = mutableListOf<PullDirEntry>()
        val deadline = System.currentTimeMillis() + 20_000L
        while (System.currentTimeMillis() < deadline && !cancelled) {
            val line = m.readLine(timeoutPerLineMs) ?: throw Exception("D: no reply (device on <5.76?)")
            if (line.startsWith("{\"run\"")) {
                val id = Regex("\"run\":(\\d+)").find(line)?.groupValues?.get(1)?.toIntOrNull()
                val ts = Regex("\"ts\":(\\d+)").find(line)?.groupValues?.get(1)?.toIntOrNull()
                val sz = Regex("\"sz\":(\\d+)").find(line)?.groupValues?.get(1)?.toIntOrNull()
                val side = Regex("\"side\":(\\d+)").find(line)?.groupValues?.get(1)?.toIntOrNull()
                if (id != null && sz != null) {
                    out.add(PullDirEntry(id, ts ?: 0, sz, side ?: 0))
                }
            } else if (line.startsWith("{\"d_end\"")) {
                return out
            } else {
                log("D: unexpected line: $line")
            }
        }
        throw Exception("D: directory timeout")
    }

    /** Verify the blob trailer: [0xC3 0x32][CRC32 LE] over payload bytes. */
    private fun verifyTrailerCrc(blob: ByteArray) {
        if (blob.size < 22) throw Exception("blob too short (${blob.size} B)")
        val cs = blob.size - 22
        /* Trailer sits at the END of the on-disk run:
           [header 16B][payload cs B][0xC3 0x32][CRC32 LE over payload] */
        if ((blob[blob.size - 6].toInt() and 0xFF) != 0xC3 ||
            (blob[blob.size - 5].toInt() and 0xFF) != 0x32) {
            throw Exception("CRC trailer magic mismatch")
        }
        val expected = le32(blob, blob.size - 4)
        val crc = CRC32()
        crc.update(blob, 16, cs)
        if (crc.value != expected) {
            throw Exception("blob CRC mismatch device=0x${java.lang.Long.toHexString(expected)} local=0x${java.lang.Long.toHexString(crc.value)}")
        }
    }

    private fun hexNybble(c: Char): Int = when (c) {
        in '0'..'9' -> c - '0'
        in 'A'..'F' -> c - 'A' + 10
        in 'a'..'f' -> c - 'a' + 10
        else -> throw Exception("bad hex char '$c'")
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

    /**
     * Download one run via stateless pull. On failure: teardown → ADV-wait →
     * reconnect → continue from the last received offset (free in a
     * stateless protocol — no restart-from-scratch needed).
     */
    private fun downloadOne(address: String, runId: Int, dirCache: MutableMap<Int, PullDirEntry>): DownloadedRun {
        val buffer = ByteArrayOutputStream()
        var expected = 0
        var lastError: Exception? = null
        var manager: SgcBleManager? = null
        var lastProgressMs = 0L

        for (attempt in 1..MAX_FRESH_ATTEMPTS) {
            if (cancelled) throw Exception("cancelled")
            try {
                if (manager == null || !manager.isConnected) {
                    try { manager?.shutdown() } catch (_: Exception) {}
                    manager = newConnectedManager(address)
                }
                val m = manager!!

                if (expected == 0) {
                    val entry = dirCache[runId] ?: pullDirectory(m).also {
                        it.forEach { e -> dirCache[e.id] = e }
                    }.firstOrNull { it.id == runId }
                        ?: throw Exception("run #$runId not in device directory")
                    expected = entry.sz
                    log("pull run #$runId total=$expected B")
                }

                val totalDeadline = System.currentTimeMillis() + PULL_TOTAL_TIMEOUT_MS

                while (buffer.size() < expected && System.currentTimeMillis() < totalDeadline && !cancelled) {
                    if (!m.isConnected) throw Exception("link lost at ${buffer.size()} B")
                    val off = buffer.size()
                    val want = minOf(PULL_CHUNK, expected - off)
                    m.consoleLines.clear()
                    m.sendRequest("r $runId $off $want")
                    var got = 0
                    var seq = 0
                    val reqDeadline = System.currentTimeMillis() + PULL_REQ_TIMEOUT_MS
                    while (System.currentTimeMillis() < reqDeadline) {
                        if (!m.isConnected) throw Exception("link lost at ${buffer.size()} B")
                        val line = m.readLine(1_000) ?: continue
                        when {
                            line.startsWith("[") -> {
                                val close = line.lastIndexOf(']')
                                if (close < 0) throw Exception("bad frame at $off")
                                val sp1 = line.indexOf(' ')
                                val sp2 = line.indexOf(' ', sp1 + 1)
                                if (sp1 < 0 || sp2 < 0) throw Exception("bad frame header: $line")
                                val fseq = line.substring(1, sp1).toIntOrNull() ?: -1
                                val flen = line.substring(sp1 + 1, sp2).toIntOrNull() ?: -1
                                if (fseq != seq) throw Exception("frame seq gap: got $fseq want $seq")
                                val hex = line.substring(sp2 + 1, close)
                                if (hex.length != flen * 2) throw Exception("frame len mismatch")
                                val bytes = ByteArray(flen)
                                for (i in 0 until flen) {
                                    bytes[i] = ((hexNybble(hex[2 * i]) shl 4) or hexNybble(hex[2 * i + 1])).toByte()
                                }
                                buffer.write(bytes)
                                got += flen
                                seq++
                            }
                            line.startsWith("OK") -> {
                                if (got <= 0) throw Exception("OK with no frames at $off")
                                break
                            }
                            line.startsWith("E ") -> throw Exception("device error ${line.substring(2)} at $off")
                            else -> log("unexpected line: $line")
                        }
                    }
                    if (got <= 0) throw Exception("request timeout at $off/${expected} B")

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

                if (buffer.size() < expected) throw Exception("short pull (${buffer.size()}/$expected B)")

                val data = buffer.toByteArray()
                verifyTrailerCrc(data)
                log("pull DONE run #$runId bytes=${data.size} attempts=$attempt")
                return DownloadedRun(runId, parseTimestamp(data), data)
            } catch (e: Exception) {
                lastError = e
                log("pull run #$runId attempt $attempt failed at ${buffer.size()} B: ${e.message}")
                try { manager?.shutdown() } catch (_: Exception) {}
                manager = null
                if (attempt < MAX_FRESH_ATTEMPTS) {
                    Thread.sleep((2_000L * attempt).coerceAtMost(6_000L))
                    if (waitForAdvertisement(address, 10_000L)) {
                        Thread.sleep(1_000)
                    }
                }
            }
        }
        try { manager?.shutdown() } catch (_: Exception) {}
        throw lastError ?: Exception("pull failed")
    }

    fun downloadRuns(address: String, runIds: List<Int>): BatchResult {
        cancelled = false
        synchronized(logs) { logs.clear() }
        val done = mutableListOf<DownloadedRun>()
        val failed = mutableListOf<FailedRun>()
        val dirCache = mutableMapOf<Int, PullDirEntry>()

        log("pull batch start: address=$address runs=${runIds.joinToString(",") { "#$it" }}")
        for ((index, runId) in runIds.withIndex()) {
            if (cancelled) break
            if (index > 0) Thread.sleep(750)
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

        log("pull batch end: ok=${done.size} failed=${failed.size}")
        return BatchResult(done, failed, synchronized(logs) { logs.toList() })
    }

    /**
     * Same JSON shape the legacy run-list characteristic produced, so the
     * Dart side is unchanged: [{"id":..,"ts":..,"size":..,"side":..},..]
     */
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
