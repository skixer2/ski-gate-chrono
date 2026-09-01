package com.skigatechono.sgc_phone

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.util.Log
import java.io.ByteArrayOutputStream
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.zip.CRC32

/**
 * Native Android BLE downloader for the SGC FT protocol.
 *
 * This is deliberately modeled after the old working BlueSTSDK approach:
 * one dedicated native GATT client, one operation at a time, explicit waits,
 * and fresh GATT sessions between runs. Flutter owns UI/storage; this class
 * owns only the BLE-critical path.
 */
@SuppressLint("MissingPermission")
class NativeBleDownloader(private val context: Context) {

    companion object {
        private const val TAG = "SGC_NATIVE"
        private val SERVICE_UUID: UUID = UUID.fromString("53470000-0000-1000-8000-00805F9B34FB")
        private val GATT_SERVICE_UUID: UUID = UUID.fromString("00001801-0000-1000-8000-00805F9B34FB")
        private val CHAR_SERVICE_CHANGED: UUID = UUID.fromString("00002A05-0000-1000-8000-00805F9B34FB")
        private val CHAR_STATE: UUID = UUID.fromString("5347ABC4-0000-1000-8000-00805F9B34FB")
        private val CHAR_BATTERY: UUID = UUID.fromString("5347ABC5-0000-1000-8000-00805F9B34FB")
        private val CHAR_FLASH_USED: UUID = UUID.fromString("5347ABC7-0000-1000-8000-00805F9B34FB")
        private val CHAR_RUN_INFO: UUID = UUID.fromString("5347ABC8-0000-1000-8000-00805F9B34FB")
        private val CHAR_RUN_LIST: UUID = UUID.fromString("5347ABC9-0000-1000-8000-00805F9B34FB")
        private val CHAR_FT_REQUEST: UUID = UUID.fromString("5347ABCA-0000-1000-8000-00805F9B34FB")
        private val CHAR_FT_STREAM: UUID = UUID.fromString("5347ABCD-0000-1000-8000-00805F9B34FB")
        private val CHAR_CAL: UUID = UUID.fromString("5347ABD0-0000-1000-8000-00805F9B34FB")
        private val CCC_DESCRIPTOR: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

        private const val CONNECT_TIMEOUT_MS = 15_000L
        private const val OP_TIMEOUT_MS = 8_000L
        private const val FT_IDLE_TIMEOUT_MS = 20_000L
        private const val FT_TOTAL_TIMEOUT_MS = 90_000L
        private const val MAX_RESUME_ATTEMPTS = 6
    }

    private val adapter: BluetoothAdapter?
        get() = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private val lock = Object()
    private val notifyQueue = LinkedBlockingQueue<ByteArray>()

    private var gatt: BluetoothGatt? = null
    private var connected = false
    private var connEvent = false
    private var connStatus = BluetoothGatt.GATT_SUCCESS

    private var servicesEvent = false
    private var servicesOk = false

    private var mtuEvent = false
    private var mtuOk = false
    private var mtu = 23

    private var readEvent = false
    private var readOk = false
    private var readValue: ByteArray? = null

    private var writeEvent = false
    private var writeOk = false

    private var descEvent = false
    private var descOk = false

    @Volatile
    private var cancelled = false

    data class DownloadedRun(
        val id: Int,
        val timestamp: Int,
        val data: ByteArray,
    )

    data class FailedRun(
        val id: Int,
        val reason: String,
    )

    data class BatchResult(
        val runs: List<DownloadedRun>,
        val failed: List<FailedRun>,
        val log: List<String>,
    )

    private val logs = mutableListOf<String>()

    private fun log(msg: String) {
        Log.d(TAG, msg)
        synchronized(logs) { logs.add(msg) }
    }

    fun cancel() {
        cancelled = true
        log("cancel requested")
        Thread { closeGatt("cancel") }.start()
    }

    private fun resetEvents() {
        synchronized(lock) {
            connEvent = false
            servicesEvent = false
            mtuEvent = false
            readEvent = false
            writeEvent = false
            descEvent = false
            readValue = null
        }
    }

    private fun waitFor(flag: () -> Boolean, timeoutMs: Long, what: String) {
        val deadline = System.currentTimeMillis() + timeoutMs
        synchronized(lock) {
            while (!flag() && System.currentTimeMillis() < deadline && !cancelled) {
                val remain = deadline - System.currentTimeMillis()
                if (remain <= 0) break
                lock.wait(remain)
            }
        }
        if (cancelled) throw Exception("cancelled")
        if (!flag()) throw Exception("$what timeout")
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            synchronized(lock) {
                connStatus = status
                connEvent = true
                connected = (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED)
                if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    connected = false
                    notifyQueue.clear()
                }
                lock.notifyAll()
            }
            log("connection state: status=$status newState=$newState")
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            synchronized(lock) {
                servicesEvent = true
                servicesOk = (status == BluetoothGatt.GATT_SUCCESS)
                lock.notifyAll()
            }
            log("services discovered: status=$status")
        }

        override fun onMtuChanged(g: BluetoothGatt, mtuValue: Int, status: Int) {
            synchronized(lock) {
                mtuEvent = true
                mtuOk = (status == BluetoothGatt.GATT_SUCCESS)
                if (mtuOk) mtu = mtuValue
                lock.notifyAll()
            }
            log("MTU changed: mtu=$mtuValue status=$status")
        }

        private fun handleRead(uuid: UUID, value: ByteArray?, status: Int) {
            synchronized(lock) {
                readEvent = true
                readOk = (status == BluetoothGatt.GATT_SUCCESS)
                readValue = value
                lock.notifyAll()
            }
            log("read ${uuid}: status=$status bytes=${value?.size ?: 0}")
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            handleRead(characteristic.uuid, characteristic.value, status)
        }

        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int,
        ) {
            handleRead(characteristic.uuid, value, status)
        }

        private fun handleWrite(uuid: UUID, status: Int) {
            synchronized(lock) {
                writeEvent = true
                writeOk = (status == BluetoothGatt.GATT_SUCCESS)
                lock.notifyAll()
            }
            log("write ${uuid}: status=$status")
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            handleWrite(characteristic.uuid, status)
        }

        private fun handleChanged(uuid: UUID, value: ByteArray) {
            // During nRF-parity mode we subscribe to all SGC notify chars.
            // Only ABCD belongs to the FT byte stream; the others are enabled
            // to reproduce the known-good nRF Connect GATT state and are
            // deliberately ignored by the FT parser.
            if (uuid == CHAR_FT_STREAM) {
                notifyQueue.offer(value)
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            val v = characteristic.value
            if (v != null) handleChanged(characteristic.uuid, v)
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            handleChanged(characteristic.uuid, value)
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            synchronized(lock) {
                descEvent = true
                descOk = (status == BluetoothGatt.GATT_SUCCESS)
                lock.notifyAll()
            }
            log("descriptor write ${descriptor.uuid}: status=$status")
        }
    }

    private fun closeGatt(reason: String) {
        val g = gatt ?: return
        log("closing GATT ($reason)")
        try { g.disconnect() } catch (_: Exception) {}
        // Android BLE is callback-driven; closing immediately after disconnect()
        // can leave the next connectGatt racing a half-torn-down link.
        val deadline = System.currentTimeMillis() + 1_500L
        synchronized(lock) {
            while (connected && System.currentTimeMillis() < deadline) {
                lock.wait(100)
            }
        }
        try { g.close() } catch (_: Exception) {}
        synchronized(lock) {
            gatt = null
            connected = false
            notifyQueue.clear()
        }
    }

    private fun connect(address: String) {
        closeGatt("fresh connect")
        resetEvents()
        val bt = adapter ?: throw Exception("BluetoothAdapter unavailable")
        if (!bt.isEnabled) throw Exception("Bluetooth is off")
        val device: BluetoothDevice = try {
            bt.getRemoteDevice(address)
        } catch (e: Exception) {
            throw Exception("bad BLE address '$address': ${e.message}")
        }

        log("connecting to $address")
        val g = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
        } else {
            @Suppress("DEPRECATION")
            device.connectGatt(context, false, callback)
        } ?: throw Exception("connectGatt returned null")
        gatt = g

        waitFor({ connEvent }, CONNECT_TIMEOUT_MS, "connect")
        if (!connected) throw Exception("connect failed status=$connStatus")

        log("connected; discovering services")
        if (!g.discoverServices()) throw Exception("discoverServices start failed")
        waitFor({ servicesEvent }, OP_TIMEOUT_MS, "service discovery")
        if (!servicesOk) throw Exception("service discovery failed")

        mtuEvent = false
        mtuOk = false
        if (g.requestMtu(247)) {
            try {
                waitFor({ mtuEvent }, OP_TIMEOUT_MS, "MTU request")
            } catch (e: Exception) {
                log("MTU request failed; continuing at mtu=$mtu")
            }
        }
        // Do NOT request a connection-priority change here. The nRF Connect
        // control test completed the full 39,372-byte stream on the same S22;
        // the native downloader's extra priority request was the main visible
        // setup difference and triggers Samsung connection-update churn.
        log("leaving connection priority at Android default")

        val svc = g.getService(SERVICE_UUID) ?: throw Exception("SGC service not found")
        if (svc.getCharacteristic(CHAR_FT_REQUEST) == null || svc.getCharacteristic(CHAR_FT_STREAM) == null) {
            throw Exception("SGC FT characteristics missing")
        }
        enableNrfParityNotifications()
        log("GATT ready (mtu=$mtu)")
    }

    private fun enableNrfParityNotifications() {
        // nRF Connect completed the same 39,372 B stream after enabling the
        // full SGC notify set. Reproduce that state exactly; FT parsing still
        // consumes only CHAR_FT_STREAM.
        setNotify(CHAR_STATE, true)
        setNotify(CHAR_BATTERY, true)
        setNotify(CHAR_FLASH_USED, true)
        setNotify(CHAR_RUN_INFO, true)
        setNotify(CHAR_FT_STREAM, true)
        setNotify(CHAR_CAL, true)
        try {
            val g = gatt
            val c = g?.getService(GATT_SERVICE_UUID)?.getCharacteristic(CHAR_SERVICE_CHANGED)
            if (g != null && c != null) {
                val d = c.getDescriptor(CCC_DESCRIPTOR)
                if (d != null) {
                    g.setCharacteristicNotification(c, true)
                    val value = if ((c.properties and BluetoothGattCharacteristic.PROPERTY_INDICATE) != 0) {
                        BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
                    } else {
                        BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    }
                    synchronized(lock) {
                        descEvent = false
                        descOk = false
                    }
                    val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        g.writeDescriptor(d, value) == BluetoothGatt.GATT_SUCCESS
                    } else {
                        @Suppress("DEPRECATION")
                        d.value = value
                        @Suppress("DEPRECATION")
                        g.writeDescriptor(d)
                    }
                    if (started) {
                        waitFor({ descEvent }, OP_TIMEOUT_MS, "descriptor write service-changed")
                    }
                }
            }
        } catch (e: Exception) {
            log("service-changed subscription skipped: ${e.message}")
        }
        log("nRF-parity notifications enabled")
    }

    private fun characteristic(uuid: UUID): BluetoothGattCharacteristic {
        val g = gatt ?: throw Exception("not connected")
        val svc = g.getService(SERVICE_UUID) ?: throw Exception("SGC service not found")
        return svc.getCharacteristic(uuid) ?: throw Exception("characteristic not found: $uuid")
    }

    private fun readChar(uuid: UUID): ByteArray {
        val g = gatt ?: throw Exception("not connected")
        val c = characteristic(uuid)
        synchronized(lock) {
            readEvent = false
            readOk = false
            readValue = null
        }
        if (!g.readCharacteristic(c)) throw Exception("read start failed: $uuid")
        waitFor({ readEvent }, OP_TIMEOUT_MS, "read $uuid")
        if (!readOk) throw Exception("read failed: $uuid")
        return readValue ?: throw Exception("empty read: $uuid")
    }

    private fun writeChar(uuid: UUID, data: ByteArray) {
        val g = gatt ?: throw Exception("not connected")
        val c = characteristic(uuid)
        c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        synchronized(lock) {
            writeEvent = false
            writeOk = false
        }
        val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(c, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            c.value = data
            @Suppress("DEPRECATION")
            g.writeCharacteristic(c)
        }
        if (!started) throw Exception("write start failed: $uuid")
        waitFor({ writeEvent }, OP_TIMEOUT_MS, "write $uuid")
        if (!writeOk) throw Exception("write failed: $uuid")
    }

    private fun setNotify(uuid: UUID, enabled: Boolean) {
        val g = gatt ?: throw Exception("not connected")
        val c = characteristic(uuid)
        val d = c.getDescriptor(CCC_DESCRIPTOR) ?: throw Exception("CCC descriptor missing: $uuid")
        if (!g.setCharacteristicNotification(c, enabled)) {
            throw Exception("setCharacteristicNotification failed: $uuid")
        }
        val value = if (enabled) {
            if ((c.properties and BluetoothGattCharacteristic.PROPERTY_INDICATE) != 0 &&
                (c.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY) == 0) {
                BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
            } else {
                BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            }
        } else {
            BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE
        }
        synchronized(lock) {
            descEvent = false
            descOk = false
        }
        val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeDescriptor(d, value) == BluetoothGatt.GATT_SUCCESS
        } else {
            @Suppress("DEPRECATION")
            d.value = value
            @Suppress("DEPRECATION")
            g.writeDescriptor(d)
        }
        if (!started) throw Exception("descriptor write start failed: $uuid")
        waitFor({ descEvent }, OP_TIMEOUT_MS, "descriptor write $uuid")
        if (!descOk) throw Exception("descriptor write failed: $uuid")
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

    private fun waitForAdvertisement(address: String, timeoutMs: Long): Boolean {
        val scanner = adapter?.bluetoothLeScanner ?: return false
        val found = CountDownLatch(1)
        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                if (result.device.address.equals(address, ignoreCase = true)) {
                    found.countDown()
                }
            }

            override fun onBatchScanResults(results: MutableList<ScanResult>) {
                if (results.any { it.device.address.equals(address, ignoreCase = true) }) {
                    found.countDown()
                }
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

    private fun parseTimestamp(data: ByteArray): Int {
        if (data.size < 6) return 0
        return le32(data, 2).toInt()
    }

    private fun startCommand(runId: Int, offset: Int): ByteArray {
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
        return cmd
    }

    private fun downloadOne(address: String, runId: Int): DownloadedRun {
        val buffer = ByteArrayOutputStream()
        var expected = 0
        var deviceCrc: Long? = null
        var lastError: Exception? = null

        for (attempt in 1..MAX_RESUME_ATTEMPTS) {
            if (cancelled) throw Exception("cancelled")
            try {
                deviceCrc = null
                if (!connected) connect(address)
                setNotify(CHAR_FT_STREAM, true)
                notifyQueue.clear()

                val offset = buffer.size()
                log("FT run #$runId attempt $attempt offset=$offset")
                writeChar(CHAR_FT_REQUEST, startCommand(runId, offset))

                val totalDeadline = System.currentTimeMillis() + FT_TOTAL_TIMEOUT_MS
                var progressDeadline = System.currentTimeMillis() + FT_IDLE_TIMEOUT_MS

                while (System.currentTimeMillis() < totalDeadline && !cancelled) {
                    val waitMs = minOf(1_000L, progressDeadline - System.currentTimeMillis())
                    if (waitMs <= 0) throw Exception("FT idle timeout at ${buffer.size()} B")
                    if (!connected) {
                        throw Exception("FT link lost at ${buffer.size()} B")
                    }
                    val pkt = notifyQueue.poll(waitMs, TimeUnit.MILLISECONDS) ?: continue
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
                            }
                        }
                        0x03 -> {
                            if (pkt.size >= 5) deviceCrc = le32(pkt, 1)
                            val data = buffer.toByteArray()
                            if (expected > 0 && data.size < expected) {
                                throw Exception("short transfer (${data.size}/$expected B)")
                            }
                            if (deviceCrc != null) {
                                val local = streamCrc32(data)
                                if (local != deviceCrc) {
                                    throw Exception("stream CRC mismatch device=0x${java.lang.Long.toHexString(deviceCrc!!)} local=0x${java.lang.Long.toHexString(local)}")
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
                try { setNotify(CHAR_FT_STREAM, false) } catch (_: Exception) {}
                if (attempt < MAX_RESUME_ATTEMPTS) {
                    Thread.sleep((2_000L * attempt).coerceAtMost(8_000L))
                    // A status=8 drop at the end of FT races the device's own
                    // disconnect handler + re-advertise. Waiting for ADV avoids
                    // the Android 147/GATT_CONNECTION_TIMEOUT reconnect storm.
                    waitForAdvertisement(address, 10_000L)
                    try { connect(address) } catch (ce: Exception) {
                        log("reconnect for resume failed: ${ce.message}")
                    }
                }
            }
        }
        throw lastError ?: Exception("FT failed")
    }

    fun downloadRuns(address: String, runIds: List<Int>): BatchResult {
        cancelled = false
        synchronized(logs) { logs.clear() }
        val done = mutableListOf<DownloadedRun>()
        val failed = mutableListOf<FailedRun>()

        log("native batch start: address=$address runs=${runIds.joinToString(",") { "#$it" }}")
        try {
            for ((index, runId) in runIds.withIndex()) {
                if (cancelled) break
                if (index > 0) {
                    closeGatt("one run per connection")
                    Thread.sleep(750)
                }
                try {
                    connect(address)
                    done.add(downloadOne(address, runId))
                } catch (e: Exception) {
                    log("run #$runId failed: ${e.message}")
                    failed.add(FailedRun(runId, e.message ?: e.javaClass.simpleName))
                }
            }
        } finally {
            closeGatt("batch end")
        }

        log("native batch end: ok=${done.size} failed=${failed.size}")
        return BatchResult(done, failed, synchronized(logs) { logs.toList() })
    }

    fun readRunListJson(address: String): String {
        cancelled = false
        connect(address)
        return try {
            String(readChar(CHAR_RUN_LIST), Charsets.UTF_8)
        } finally {
            closeGatt("readRunList")
        }
    }
}
