package com.embeded.miniprinter.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class PrinterProtocolTest {
    @Test fun beginAndLineBatchUseTheDocumentedFrame() {
        assertArrayEquals(
            byteArrayOf(0x4D, 0x50, 1, 1, 1, 0, 64),
            PrinterProtocol.begin(64),
        )
        val first = ByteArray(48) { 0x55 }
        val second = ByteArray(48) { 0x33 }
        val batch = PrinterProtocol.lineBatch(listOf(first, second))
        assertEquals(108, batch.size)
        assertArrayEquals(PrinterProtocol.line(first), batch.copyOfRange(0, 54))
        assertArrayEquals(PrinterProtocol.line(second), batch.copyOfRange(54, 108))
    }

    @Test fun statusRejectsMalformedFramesAndDecodesPaperAndTemperature() {
        val frame = byteArrayOf(0x4D, 0x50, 1, 0x80.toByte(), 5, 0, 73, 0xFF.toByte(), 1, 3, 1)
        val status = PrinterProtocol.decodeStatus(frame)!!
        assertEquals(73, status.battery)
        assertNull(status.temperature)
        assertEquals(false, status.paperPresent)
        assertEquals(PrinterState.ERROR, status.state)
        assertEquals("Out of paper", status.errorMessage)
        assertNull(PrinterProtocol.decodeStatus(frame.copyOfRange(0, 10)))
        frame[6] = 101.toByte()
        assertNull(PrinterProtocol.decodeStatus(frame))
    }
}
