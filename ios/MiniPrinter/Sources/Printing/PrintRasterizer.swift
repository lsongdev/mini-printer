import CoreGraphics
import UIKit

enum PrintRasterizer {
    enum Error: LocalizedError {
        case invalidImage
        case renderingFailed
        case emptyText
        case textTooLong

        var errorDescription: String? {
            switch self {
            case .invalidImage: "The selected image cannot be printed."
            case .renderingFailed: "The image could not be converted for the printer."
            case .emptyText: "Enter some text before printing."
            case .textTooLong: "The text is too long for one print job. Shorten it and try again."
            }
        }
    }

    private static let maximumLines = 2_048
    private static let bayer: [[Int]] = [
        [0, 8, 2, 10],
        [12, 4, 14, 6],
        [3, 11, 1, 9],
        [15, 7, 13, 5],
    ]

    static func photoLines(from image: UIImage) throws -> [Data] {
        guard image.size.width > 0, image.size.height > 0 else { throw Error.invalidImage }
        let width = PrinterWireProtocol.width
        let scaledHeight = Int((CGFloat(width) * image.size.height / image.size.width).rounded())
        let height = min(max(scaledHeight, 32), maximumLines)
        let size = CGSize(width: width, height: height)
        let drawWidth = min(CGFloat(width), CGFloat(height) * image.size.width / image.size.height)
        let imageRect = CGRect(x: (CGFloat(width) - drawWidth) / 2, y: 0, width: drawWidth, height: CGFloat(height))

        // UIKit draws the selected photo in its display orientation.
        let format = UIGraphicsImageRendererFormat()
        format.scale = 1
        format.opaque = true
        let normalized = UIGraphicsImageRenderer(size: size, format: format).image { context in
            UIColor.white.setFill()
            context.fill(CGRect(origin: .zero, size: size))
            image.draw(in: imageRect)
        }
        guard let cgImage = normalized.cgImage else { throw Error.renderingFailed }
        return try rasterLines(from: cgImage, height: height)
    }

    static func textLines(from text: NSAttributedString) throws -> [Data] {
        guard !text.string.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { throw Error.emptyText }

        let width = PrinterWireProtocol.width
        let inset: CGFloat = 16
        let contentWidth = CGFloat(width) - inset * 2
        let attributed = NSMutableAttributedString(attributedString: text)
        attributed.addAttribute(.foregroundColor, value: UIColor.black, range: NSRange(location: 0, length: attributed.length))
        let options: NSStringDrawingOptions = [.usesLineFragmentOrigin, .usesFontLeading]
        let bounds = attributed.boundingRect(
            with: CGSize(width: contentWidth, height: .greatestFiniteMagnitude),
            options: options,
            context: nil
        )
        let height = Int(ceil(bounds.height + inset * 2))
        guard height <= maximumLines else { throw Error.textTooLong }

        let size = CGSize(width: width, height: height)
        let format = UIGraphicsImageRendererFormat()
        format.scale = 1
        format.opaque = true
        let image = UIGraphicsImageRenderer(size: size, format: format).image { context in
            UIColor.white.setFill()
            context.fill(CGRect(origin: .zero, size: size))
            attributed.draw(
                with: CGRect(x: inset, y: inset, width: contentWidth, height: bounds.height),
                options: options,
                context: nil
            )
        }
        guard let cgImage = image.cgImage else { throw Error.renderingFailed }
        return try rasterLines(from: cgImage, height: height)
    }

    private static func rasterLines(from cgImage: CGImage, height: Int) throws -> [Data] {
        let width = PrinterWireProtocol.width
        let size = CGSize(width: width, height: height)
        var gray = [UInt8](repeating: 255, count: width * height)
        let rendered = gray.withUnsafeMutableBytes { buffer in
            guard let context = CGContext(
                data: buffer.baseAddress,
                width: width,
                height: height,
                bitsPerComponent: 8,
                bytesPerRow: width,
                space: CGColorSpaceCreateDeviceGray(),
                bitmapInfo: CGImageAlphaInfo.none.rawValue
            ) else { return false }
            context.draw(cgImage, in: CGRect(origin: .zero, size: size))
            return true
        }
        guard rendered else { throw Error.renderingFailed }

        return (0..<height).map { y in
            var line = Data(repeating: 0, count: PrinterWireProtocol.bytesPerLine)
            for x in 0..<width {
                let offset = (bayer[y & 3][x & 3] - 8) * 8
                if Int(gray[y * width + x]) + offset < 128 {
                    line[x / 8] |= 0x80 >> (x & 7)
                }
            }
            return line
        }
    }

    static func testPageLines() -> [Data] {
        let width = PrinterWireProtocol.width
        return (0..<96).map { y in
            var line = Data(repeating: 0, count: PrinterWireProtocol.bytesPerLine)
            func dot(_ x: Int) { line[x / 8] |= 0x80 >> (x & 7) }

            if y == 4 || y == 91 {
                for x in 8..<(width - 8) { dot(x) }
            } else if y > 4 && y < 91 {
                dot(8)
                dot(width - 9)
            }
            if (10..<40).contains(y) {
                let group = (y - 10) / 5
                for x in (group * 64 + 12)..<(group * 64 + 52) where x & 1 == 1 {
                    dot(x)
                }
            }
            if (54..<86).contains(y) {
                for x in 16..<(width - 16)
                    where ((x - 16) / 8 + (y - 54) / 8).isMultiple(of: 2) {
                    dot(x)
                }
            }
            return line
        }
    }
}
