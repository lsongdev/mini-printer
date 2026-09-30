import XCTest
import UIKit
@testable import MiniPrinter

final class PrinterTests: XCTestCase {
    func testFramesMatchFirmwareProtocol() throws {
        XCTAssertEqual(PrinterWireProtocol.begin(density: 64), Data([0x4D, 0x50, 1, 1, 1, 0, 64]))
        XCTAssertEqual(PrinterWireProtocol.end, Data([0x4D, 0x50, 1, 3, 0, 0]))
        XCTAssertThrowsError(try PrinterWireProtocol.line(Data(repeating: 0, count: 47)))

        let lines = Array(repeating: Data(repeating: 0xA5, count: 48), count: 9)
        let batches = try PrinterWireProtocol.lineBatches(lines)
        XCTAssertEqual(batches.map(\.count), [432, 54])
        XCTAssertEqual(batches[0].prefix(6), Data([0x4D, 0x50, 1, 2, 48, 0]))
    }

    func testStatusValidation() throws {
        let frame = Data([0x4D, 0x50, 1, 0x80, 5, 0, 92, 255, 0, 1, 0])
        let status = try PrinterStatus(frame: frame)
        XCTAssertEqual(status.battery, 92)
        XCTAssertNil(status.temperature)
        XCTAssertTrue(status.paperPresent)
        XCTAssertEqual(status.state, .printing)
        XCTAssertThrowsError(try PrinterStatus(frame: Data(frame.dropLast())))
    }

    func testTestPageHasVisibleLines() {
        let lines = PrintRasterizer.testPageLines()
        XCTAssertEqual(lines.count, 96)
        XCTAssertTrue(lines.allSatisfy { $0.count == 48 })
        XCTAssertTrue(lines.contains { $0.contains { $0 != 0 } })
    }

    func testPhotoRasterKeepsTopAtTop() throws {
        let format = UIGraphicsImageRendererFormat()
        format.scale = 1
        let image = UIGraphicsImageRenderer(size: CGSize(width: 32, height: 32), format: format).image { context in
            UIColor.white.setFill()
            context.fill(CGRect(x: 0, y: 0, width: 32, height: 32))
            UIColor.black.setFill()
            context.fill(CGRect(x: 0, y: 0, width: 32, height: 16))
        }
        let lines = try PrintRasterizer.photoLines(from: image)
        XCTAssertEqual(lines.count, 384)
        XCTAssertEqual(lines[0], Data(repeating: 0xFF, count: 48), "first: \(Array(lines[0].prefix(8)))")
        XCTAssertEqual(lines[383], Data(repeating: 0, count: 48), "last: \(Array(lines[383].prefix(8)))")
    }

    func testTextRasterWrapsAndKeepsMargins() throws {
        let text = NSAttributedString(
            string: String(repeating: "Hello, printer! 你好。", count: 8),
            attributes: [.font: UIFont.systemFont(ofSize: 24)]
        )
        let lines = try PrintRasterizer.textLines(
            from: text
        )
        XCTAssertGreaterThan(lines.count, 100)
        XCTAssertTrue(lines.allSatisfy { $0.count == 48 })
        XCTAssertTrue(lines.prefix(12).allSatisfy { $0.allSatisfy { $0 == 0 } })
        XCTAssertTrue(lines.contains { $0.contains { $0 != 0 } })
    }

    func testTextRasterRejectsEmptyAndOversizedJobs() {
        XCTAssertThrowsError(try PrintRasterizer.textLines(from: NSAttributedString(string: "  \n")))
        XCTAssertThrowsError(try PrintRasterizer.textLines(
            from: NSAttributedString(
                string: Array(repeating: "long line", count: 200).joined(separator: "\n"),
                attributes: [.font: UIFont.boldSystemFont(ofSize: 32)]
            )
        ))
    }

    @MainActor
    func testRichTextStylesOnlySelectedRangeAndPersists() throws {
        let suite = "printer-tests-\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { defaults.removePersistentDomain(forName: suite) }
        let draft = RichTextDraft(defaults: defaults)
        let view = UITextView()
        draft.attach(view)
        view.attributedText = NSAttributedString(
            string: "Hello\nworld",
            attributes: [.font: UIFont.systemFont(ofSize: 24)]
        )
        draft.contentChanged(in: view)
        view.selectedRange = NSRange(location: 0, length: 5)
        draft.updateSelection(from: view)
        draft.toggleBold()
        draft.setFontSize(32)
        draft.setAlignment(.center)

        let first = try XCTUnwrap(view.attributedText.attribute(.font, at: 0, effectiveRange: nil) as? UIFont)
        let second = try XCTUnwrap(view.attributedText.attribute(.font, at: 6, effectiveRange: nil) as? UIFont)
        XCTAssertTrue(first.fontDescriptor.symbolicTraits.contains(.traitBold))
        XCTAssertEqual(first.pointSize, 32)
        XCTAssertFalse(second.fontDescriptor.symbolicTraits.contains(.traitBold))
        XCTAssertEqual(second.pointSize, 24)
        let firstParagraph = try XCTUnwrap(view.attributedText.attribute(.paragraphStyle, at: 0, effectiveRange: nil) as? NSParagraphStyle)
        let secondParagraph = view.attributedText.attribute(.paragraphStyle, at: 6, effectiveRange: nil) as? NSParagraphStyle
        XCTAssertEqual(firstParagraph.alignment, .center)
        XCTAssertNotEqual(secondParagraph?.alignment, .center)

        let restored = RichTextDraft(defaults: defaults)
        let restoredFont = try XCTUnwrap(restored.document.attribute(.font, at: 0, effectiveRange: nil) as? UIFont)
        XCTAssertEqual(restoredFont.pointSize, 32)
        XCTAssertTrue(restoredFont.fontDescriptor.symbolicTraits.contains(.traitBold))
    }
}
