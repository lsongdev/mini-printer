package com.embeded.miniprinter.bluetooth

import android.Manifest
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
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import com.embeded.miniprinter.protocol.PrinterProtocol
import com.embeded.miniprinter.protocol.PrinterStatus
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import java.util.UUID

data class DiscoveredPrinter(val address: String, val name: String, val rssi: Int)

/** Owns one foreground BLE connection and serializes acknowledged GATT writes. */
@SuppressLint("MissingPermission") // The activity requests Bluetooth permissions before calling this transport.
class BlePrinterTransport(context: Context) {
    private val appContext = context.applicationContext
    private val handler = Handler(Looper.getMainLooper())
    private val adapter = (appContext.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    private val knownDevices = mutableMapOf<String, BluetoothDevice>()
    private val writeMutex = Mutex()

    val devices = MutableStateFlow<List<DiscoveredPrinter>>(emptyList())
    val scanning = MutableStateFlow(false)
    val connectedName = MutableStateFlow<String?>(null)
    val status = MutableStateFlow<PrinterStatus?>(null)
    val errors = MutableSharedFlow<String>(extraBufferCapacity = 4)

    private var activeGatt: BluetoothGatt? = null
    private var activeCharacteristic: BluetoothGattCharacteristic? = null
    private var selectedName: String? = null
    private var mtu = 23
    private var pendingConnection: CompletableDeferred<Unit>? = null
    private var pendingWrite: CompletableDeferred<Unit>? = null

    val bluetoothAvailable: Boolean
        get() = try { adapter?.isEnabled == true } catch (_: SecurityException) { false }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.scanRecord?.deviceName ?: result.device.name ?: return
            if (!name.startsWith("Mini-Printer")) return
            handler.post {
                val address = result.device.address
                knownDevices[address] = result.device
                val item = DiscoveredPrinter(address, name, result.rssi)
                devices.value = (devices.value.filterNot { it.address == address } + item)
                    .sortedByDescending { it.rssi }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            handler.post {
                scanning.value = false
                errors.tryEmit("Bluetooth scan failed ($errorCode).")
            }
        }
    }

    fun startScan() {
        check(bluetoothAvailable) { "Turn on Bluetooth to discover printers." }
        stopScan()
        knownDevices.clear()
        devices.value = emptyList()
        val scanner = adapter?.bluetoothLeScanner ?: error("Bluetooth scanner is unavailable.")
        scanner.startScan(null, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCallback)
        scanning.value = true
    }

    fun stopScan() {
        if (!scanning.value) return
        try { adapter?.bluetoothLeScanner?.stopScan(scanCallback) } catch (_: SecurityException) { }
        scanning.value = false
    }

    suspend fun connect(address: String) = withContext(Dispatchers.Main.immediate) {
        val device = knownDevices[address] ?: error("Select a discovered printer.")
        stopScan()
        disconnect()
        selectedName = devices.value.firstOrNull { it.address == address }?.name ?: "Mini-Printer"
        val ready = CompletableDeferred<Unit>()
        pendingConnection = ready
        try {
            activeGatt = device.connectGatt(appContext, false, callback, BluetoothDevice.TRANSPORT_LE)
                ?: error("Bluetooth connection could not start.")
            withTimeout(20_000) { ready.await() }
        } catch (failure: Exception) {
            disconnect()
            throw failure
        }
    }

    fun disconnect() {
        val gatt = activeGatt
        activeGatt = null
        activeCharacteristic = null
        selectedName = null
        connectedName.value = null
        status.value = null
        mtu = 23
        pendingConnection?.completeExceptionally(IllegalStateException("Printer disconnected."))
        pendingConnection = null
        pendingWrite?.completeExceptionally(IllegalStateException("Printer disconnected."))
        pendingWrite = null
        try { gatt?.disconnect() } catch (_: SecurityException) { }
        gatt?.close()
    }

    suspend fun write(data: ByteArray) = withContext(Dispatchers.Main.immediate) {
        writeMutex.withLock {
            try {
                var offset = 0
                while (offset < data.size) {
                    val gatt = activeGatt ?: error("Connect to a printer first.")
                    val characteristic = activeCharacteristic ?: error("Printer is not ready.")
                    val length = minOf(512, mtu - 3, data.size - offset)
                    check(length > 0) { "Invalid Bluetooth MTU." }
                    val chunk = data.copyOfRange(offset, offset + length)
                    val completed = CompletableDeferred<Unit>()
                    pendingWrite = completed
                    val started = if (Build.VERSION.SDK_INT >= 33) {
                        gatt.writeCharacteristic(
                            characteristic, chunk, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
                        ) == BluetoothGatt.GATT_SUCCESS
                    } else {
                        @Suppress("DEPRECATION")
                        characteristic.value = chunk
                        @Suppress("DEPRECATION")
                        gatt.writeCharacteristic(characteristic)
                    }
                    if (!started) error("Bluetooth write could not start.")
                    withTimeout(8_000) { completed.await() }
                    pendingWrite = null
                    offset += length
                }
            } catch (failure: Exception) {
                disconnect()
                throw failure
            }
        }
    }

    fun close() {
        stopScan()
        disconnect()
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, result: Int, newState: Int) {
            handler.post {
                if (gatt !== activeGatt) { gatt.close(); return@post }
                if (result != BluetoothGatt.GATT_SUCCESS || newState == BluetoothProfile.STATE_DISCONNECTED) {
                    val wasReady = connectedName.value != null
                    disconnect()
                    if (wasReady) errors.tryEmit("Printer disconnected.")
                } else if (newState == BluetoothProfile.STATE_CONNECTED) {
                    if (!gatt.requestMtu(517)) gatt.discoverServices()
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, value: Int, result: Int) {
            handler.post {
                if (gatt !== activeGatt) return@post
                if (result == BluetoothGatt.GATT_SUCCESS) mtu = value
                gatt.discoverServices()
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, result: Int) {
            handler.post {
                if (gatt !== activeGatt) return@post
                val characteristic = gatt.getService(SERVICE_UUID)?.getCharacteristic(CHARACTERISTIC_UUID)
                val descriptor = characteristic?.getDescriptor(CCCD_UUID)
                if (result != BluetoothGatt.GATT_SUCCESS || characteristic == null || descriptor == null ||
                    characteristic.properties and BluetoothGattCharacteristic.PROPERTY_WRITE == 0 ||
                    characteristic.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY == 0 ||
                    !gatt.setCharacteristicNotification(characteristic, true)
                ) {
                    fail("This device does not provide the Mini Printer service.")
                    return@post
                }
                activeCharacteristic = characteristic
                val subscribed = if (Build.VERSION.SDK_INT >= 33) {
                    gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothGatt.GATT_SUCCESS
                } else {
                    @Suppress("DEPRECATION")
                    descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    @Suppress("DEPRECATION")
                    gatt.writeDescriptor(descriptor)
                }
                if (!subscribed) fail("Could not subscribe to printer status.")
            }
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, result: Int) {
            handler.post {
                if (gatt !== activeGatt) return@post
                if (result != BluetoothGatt.GATT_SUCCESS) {
                    fail("Could not subscribe to printer status.")
                    return@post
                }
                connectedName.value = selectedName
                pendingConnection?.complete(Unit)
                pendingConnection = null
                activeCharacteristic?.let(gatt::readCharacteristic)
            }
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray, result: Int,
        ) {
            if (result == BluetoothGatt.GATT_SUCCESS) acceptStatus(gatt, value)
        }

        @Deprecated("Used for Android 12 and earlier")
        override fun onCharacteristicRead(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, result: Int) {
            if (Build.VERSION.SDK_INT < 33 && result == BluetoothGatt.GATT_SUCCESS) {
                @Suppress("DEPRECATION")
                acceptStatus(gatt, characteristic.value)
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray,
        ) = acceptStatus(gatt, value)

        @Deprecated("Used for Android 12 and earlier")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < 33) {
                @Suppress("DEPRECATION")
                acceptStatus(gatt, characteristic.value)
            }
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, result: Int) {
            handler.post {
                if (gatt !== activeGatt) return@post
                val completed = pendingWrite ?: return@post
                if (result == BluetoothGatt.GATT_SUCCESS) completed.complete(Unit)
                else completed.completeExceptionally(IllegalStateException("Bluetooth write failed ($result)."))
            }
        }
    }

    private fun acceptStatus(gatt: BluetoothGatt, bytes: ByteArray?) {
        val decoded = bytes?.let(PrinterProtocol::decodeStatus) ?: return
        handler.post { if (gatt === activeGatt) status.value = decoded }
    }

    private fun fail(message: String) {
        val wasConnecting = pendingConnection != null
        pendingConnection?.completeExceptionally(IllegalStateException(message))
        disconnect()
        if (!wasConnecting) errors.tryEmit(message)
    }

    companion object {
        private val SERVICE_UUID = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
        private val CHARACTERISTIC_UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8")
        private val CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
