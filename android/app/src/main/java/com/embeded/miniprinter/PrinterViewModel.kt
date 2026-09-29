package com.embeded.miniprinter

import android.app.Application
import android.graphics.Bitmap
import android.graphics.ImageDecoder
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.embeded.miniprinter.bluetooth.BlePrinterTransport
import com.embeded.miniprinter.bluetooth.DiscoveredPrinter
import com.embeded.miniprinter.printing.PrintRasterizer
import com.embeded.miniprinter.protocol.PrinterProtocol
import com.embeded.miniprinter.protocol.PrinterState
import com.embeded.miniprinter.protocol.PrinterStatus
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlin.math.roundToInt

data class PrinterUiState(
    val devices: List<DiscoveredPrinter> = emptyList(),
    val bluetoothAvailable: Boolean = false,
    val scanning: Boolean = false,
    val connectingAddress: String? = null,
    val connectedName: String? = null,
    val status: PrinterStatus? = null,
    val photo: Bitmap? = null,
    val density: Int = 64,
    val printing: Boolean = false,
    val submittedLines: Int = 0,
    val error: String? = null,
)

class PrinterViewModel(application: Application) : AndroidViewModel(application) {
    private val transport = BlePrinterTransport(application)
    private val mutableState = MutableStateFlow(PrinterUiState())
    val state: StateFlow<PrinterUiState> = mutableState.asStateFlow()

    private var scanJob: Job? = null
    private var sawPrinting = false

    init {
        viewModelScope.launch { transport.devices.collect { value -> mutableState.update { it.copy(devices = value) } } }
        viewModelScope.launch { transport.scanning.collect { value -> mutableState.update { it.copy(scanning = value) } } }
        viewModelScope.launch {
            transport.connectedName.collect { value ->
                mutableState.update { it.copy(connectedName = value, status = if (value == null) null else it.status) }
            }
        }
        viewModelScope.launch {
            transport.status.collect { value ->
                if (mutableState.value.printing && value?.state == PrinterState.PRINTING) sawPrinting = true
                mutableState.update { it.copy(status = value) }
            }
        }
        viewModelScope.launch { transport.errors.collect { value -> showError(value) } }
    }

    fun scan() {
        scanJob?.cancel()
        transport.stopScan()
        scanJob = viewModelScope.launch {
            try {
                mutableState.update { it.copy(bluetoothAvailable = transport.bluetoothAvailable) }
                transport.startScan()
                delay(12_000)
            } catch (failure: Exception) {
                if (failure !is CancellationException) showError(failure.message ?: "Could not scan for printers.")
            }
            if (currentCoroutineContext().isActive) transport.stopScan()
        }
    }

    fun connect(printer: DiscoveredPrinter) {
        if (mutableState.value.connectingAddress != null) return
        scanJob?.cancel()
        transport.stopScan()
        mutableState.update { it.copy(connectingAddress = printer.address, error = null) }
        viewModelScope.launch {
            try { transport.connect(printer.address) }
            catch (failure: Exception) { showError(failure.message ?: "Could not connect to printer.") }
            finally { mutableState.update { it.copy(connectingAddress = null) } }
        }
    }

    fun disconnect() { transport.disconnect() }

    fun setDensity(value: Int) { mutableState.update { it.copy(density = value.coerceIn(1, 100)) } }

    fun loadPhoto(uri: Uri) {
        viewModelScope.launch {
            try {
                val bitmap = withContext(Dispatchers.IO) {
                    val source = ImageDecoder.createSource(getApplication<Application>().contentResolver, uri)
                    ImageDecoder.decodeBitmap(source) { decoder, info, _ ->
                        val scale = minOf(
                            384.0 / info.size.width,
                            2048.0 / info.size.height,
                            1.0,
                        )
                        val width = (info.size.width * scale).roundToInt().coerceAtLeast(1)
                        val height = (info.size.height * scale).roundToInt().coerceAtLeast(1)
                        decoder.setTargetSize(width, height)
                        decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
                    }
                }
                mutableState.update { it.copy(photo = bitmap) }
            } catch (failure: Exception) {
                showError(failure.message ?: "The selected photo could not be opened.")
            }
        }
    }

    fun printPhoto() {
        val photo = mutableState.value.photo ?: return
        startPrint { withContext(Dispatchers.Default) { PrintRasterizer.photoLines(photo) } }
    }

    fun printTestPage() {
        startPrint { PrintRasterizer.testPageLines() }
    }

    private fun startPrint(makeLines: suspend () -> List<ByteArray>) {
        val current = mutableState.value
        if (current.printing) return
        if (current.connectedName == null) { showError("Connect to a printer first."); return }
        if (current.status?.paperPresent == false) { showError("Load paper before printing."); return }

        mutableState.update { it.copy(printing = true, submittedLines = 0, error = null) }
        sawPrinting = false
        viewModelScope.launch {
            var started = false
            try {
                val lines = makeLines()
                transport.write(PrinterProtocol.begin(current.density))
                started = true
                lines.chunked(PrinterProtocol.linesPerBatch).forEachIndexed { index, batch ->
                    transport.write(PrinterProtocol.lineBatch(batch))
                    mutableState.update {
                        it.copy(submittedLines = minOf((index + 1) * PrinterProtocol.linesPerBatch, lines.size))
                    }
                    val status = mutableState.value.status
                    if (status?.state == PrinterState.ERROR || (sawPrinting && status?.errorCode != 0)) {
                        error(status?.errorMessage ?: "Printer stopped unexpectedly.")
                    }
                }
                transport.write(PrinterProtocol.end)
                withTimeout(90_000) {
                    transport.status.filterNotNull().first { status ->
                        if (status.state == PrinterState.ERROR || (sawPrinting && status.errorCode != 0)) {
                            error(status.errorMessage ?: "Printer stopped unexpectedly.")
                        }
                        sawPrinting && status.state == PrinterState.IDLE
                    }
                }
            } catch (failure: Exception) {
                if (started) withContext(NonCancellable) { runCatching { transport.write(PrinterProtocol.cancel) } }
                if (failure is CancellationException) throw failure
                showError(failure.message ?: "Printing failed.")
            } finally {
                mutableState.update { it.copy(printing = false) }
            }
        }
    }

    fun showError(message: String) { mutableState.update { it.copy(error = message) } }
    fun clearError() { mutableState.update { it.copy(error = null) } }

    override fun onCleared() {
        scanJob?.cancel()
        transport.close()
        super.onCleared()
    }
}
