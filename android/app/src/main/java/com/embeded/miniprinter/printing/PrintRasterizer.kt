package com.embeded.miniprinter.printing

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import com.embeded.miniprinter.protocol.PrinterProtocol
import kotlin.math.roundToInt

object PrintRasterizer {
    private const val maximumLines = 2048
    private val bayer = arrayOf(
        intArrayOf(0, 8, 2, 10),
        intArrayOf(12, 4, 14, 6),
        intArrayOf(3, 11, 1, 9),
        intArrayOf(15, 7, 13, 5),
    )

    fun photoLines(photo: Bitmap): List<ByteArray> {
        require(photo.width > 0 && photo.height > 0) { "The selected image cannot be printed." }
        val width = PrinterProtocol.width
        val height = (width.toDouble() * photo.height / photo.width)
            .roundToInt().coerceIn(32, maximumLines)
        val drawWidth = minOf(width.toFloat(), height.toFloat() * photo.width / photo.height)
        val rendered = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(rendered)
        canvas.drawColor(Color.WHITE)
        canvas.drawBitmap(
            photo, null,
            RectF((width - drawWidth) / 2f, 0f, (width + drawWidth) / 2f, height.toFloat()),
            Paint(Paint.FILTER_BITMAP_FLAG),
        )

        val pixels = IntArray(width * height)
        rendered.getPixels(pixels, 0, width, 0, 0, width, height)
        rendered.recycle()

        return List(height) { y ->
            ByteArray(PrinterProtocol.bytesPerLine).also { line ->
                for (x in 0 until width) {
                    val pixel = pixels[y * width + x]
                    val gray = (Color.red(pixel) * 299 + Color.green(pixel) * 587 +
                        Color.blue(pixel) * 114) / 1000
                    if (gray + (bayer[y and 3][x and 3] - 8) * 8 < 128) {
                        line[x / 8] = (line[x / 8].toInt() or (0x80 shr (x and 7))).toByte()
                    }
                }
            }
        }
    }

    fun testPageLines(): List<ByteArray> = List(96) { y ->
        ByteArray(PrinterProtocol.bytesPerLine).also { line ->
            fun dot(x: Int) {
                line[x / 8] = (line[x / 8].toInt() or (0x80 shr (x and 7))).toByte()
            }
            if (y == 4 || y == 91) {
                for (x in 8 until PrinterProtocol.width - 8) dot(x)
            } else if (y in 5..90) {
                dot(8)
                dot(PrinterProtocol.width - 9)
            }
            if (y in 10 until 40) {
                val group = (y - 10) / 5
                for (x in group * 64 + 12 until group * 64 + 52) {
                    if (x and 1 == 1) dot(x)
                }
            }
            if (y in 54 until 86) {
                for (x in 16 until PrinterProtocol.width - 16) {
                    if (((x - 16) / 8 + (y - 54) / 8) % 2 == 0) dot(x)
                }
            }
        }
    }
}
