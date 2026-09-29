package com.embeded.miniprinter.protocol

object PrinterProtocol {
    const val width = 384
    const val bytesPerLine = width / 8
    const val linesPerBatch = 8

    private fun frame(command: Int, payload: ByteArray = byteArrayOf()): ByteArray {
        require(payload.size <= 48) { "The protocol payload cannot exceed 48 bytes." }
        return byteArrayOf(
            0x4D, 0x50, 0x01, command.toByte(),
            payload.size.toByte(), (payload.size shr 8).toByte(),
        ) + payload
    }

    fun begin(density: Int): ByteArray {
        require(density in 1..100)
        return frame(0x01, byteArrayOf(density.toByte()))
    }

    fun line(raster: ByteArray): ByteArray {
        require(raster.size == bytesPerLine) { "A print line must contain 48 bytes." }
        return frame(0x02, raster)
    }

    val end: ByteArray get() = frame(0x03)
    val cancel: ByteArray get() = frame(0x04)

    fun lineBatch(lines: List<ByteArray>): ByteArray {
        require(lines.size <= linesPerBatch)
        val batch = ByteArray(lines.size * (6 + bytesPerLine))
        lines.forEachIndexed { index, raster ->
            line(raster).copyInto(batch, index * (6 + bytesPerLine))
        }
        return batch
    }

    fun decodeStatus(frame: ByteArray): PrinterStatus? {
        if (frame.size != 11 || frame[0] != 0x4D.toByte() || frame[1] != 0x50.toByte() ||
            frame[2] != 1.toByte() || frame[3] != 0x80.toByte() ||
            frame[4] != 5.toByte() || frame[5] != 0.toByte()
        ) return null

        val battery = frame[6].toInt() and 0xFF
        val paper = frame[8].toInt() and 0xFF
        val state = PrinterState.entries.getOrNull(frame[9].toInt() and 0xFF)
        if (battery > 100 || paper > 1 || state == null) return null

        val temperature = frame[7].toInt() and 0xFF
        return PrinterStatus(
            battery = battery,
            temperature = temperature.takeUnless { it == 255 },
            paperPresent = paper == 0,
            state = state,
            errorCode = frame[10].toInt() and 0xFF,
        )
    }
}

enum class PrinterState(val title: String) {
    IDLE("Ready"), PRINTING("Printing"), FEEDING("Feeding"), ERROR("Needs attention")
}

data class PrinterStatus(
    val battery: Int,
    val temperature: Int?,
    val paperPresent: Boolean,
    val state: PrinterState,
    val errorCode: Int,
) {
    val errorMessage: String?
        get() = when (errorCode) {
            0 -> null
            1 -> "Out of paper"
            2 -> "Print head overheated"
            3 -> "Temperature sensor failed"
            4 -> "Print job timed out"
            5 -> "Print job cancelled"
            6 -> "Printer rejected the protocol frame"
            else -> "Printer error $errorCode"
        }
}
