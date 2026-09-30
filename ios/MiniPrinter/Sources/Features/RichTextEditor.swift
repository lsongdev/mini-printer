import Observation
import SwiftUI
import UIKit

@MainActor
@Observable
final class RichTextDraft {
    private static let storageKey = "printRichDraft"
    private static let defaultSize: CGFloat = 24

    var document: NSAttributedString
    var selectedFontSize = 24
    var selectedBold = false
    var selectedAlignment: NSTextAlignment = .left

    @ObservationIgnored private weak var textView: UITextView?
    @ObservationIgnored private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        if let data = defaults.data(forKey: Self.storageKey),
           let restored = try? NSAttributedString(
               data: data,
               options: [.documentType: NSAttributedString.DocumentType.rtf],
               documentAttributes: nil
           ) {
            document = restored
        } else {
            let oldText = defaults.string(forKey: "printDraft") ?? ""
            let size = defaults.integer(forKey: "printTextSize")
            let fontSize = size > 0 ? CGFloat(size) : Self.defaultSize
            let alignment: NSTextAlignment = switch defaults.integer(forKey: "printTextAlignment") {
            case 1: .center
            case 2: .right
            default: .left
            }
            document = NSAttributedString(
                string: oldText,
                attributes: Self.attributes(
                    size: fontSize,
                    bold: defaults.bool(forKey: "printTextBold"),
                    alignment: alignment
                )
            )
        }
    }

    func attach(_ view: UITextView) {
        textView = view
        view.attributedText = document
        if document.length > 0 {
            view.typingAttributes = document.attributes(at: document.length - 1, effectiveRange: nil)
        } else {
            view.typingAttributes = Self.attributes(size: Self.defaultSize, bold: false, alignment: .left)
        }
        updateSelection(from: view)
    }

    func contentChanged(in view: UITextView) {
        document = NSAttributedString(attributedString: view.attributedText)
        persist()
        updateSelection(from: view)
    }

    func dismissKeyboard() {
        textView?.resignFirstResponder()
    }

    func snapshot() -> NSAttributedString {
        guard let textView else { return document }
        contentChanged(in: textView)
        return document
    }

    func updateSelection(from view: UITextView) {
        let attributes: [NSAttributedString.Key: Any]
        if view.selectedRange.length > 0, view.selectedRange.location < view.attributedText.length {
            attributes = view.attributedText.attributes(at: view.selectedRange.location, effectiveRange: nil)
        } else {
            attributes = view.typingAttributes
        }
        let font = attributes[.font] as? UIFont ?? UIFont.systemFont(ofSize: Self.defaultSize)
        selectedFontSize = Int(font.pointSize.rounded())
        selectedBold = font.fontDescriptor.symbolicTraits.contains(.traitBold)
        selectedAlignment = (attributes[.paragraphStyle] as? NSParagraphStyle)?.alignment ?? .left
    }

    func toggleBold() {
        let shouldBold = !selectedBold
        changeFonts { font in Self.font(size: font.pointSize, bold: shouldBold) }
    }

    func setFontSize(_ size: Int) {
        changeFonts { font in
            Self.font(size: CGFloat(size), bold: font.fontDescriptor.symbolicTraits.contains(.traitBold))
        }
    }

    func setAlignment(_ alignment: NSTextAlignment) {
        guard let view = textView else { return }
        let selection = view.selectedRange
        guard view.textStorage.length > 0 else {
            var attributes = view.typingAttributes
            let paragraph = Self.paragraph(alignment: alignment)
            attributes[.paragraphStyle] = paragraph
            view.typingAttributes = attributes
            updateSelection(from: view)
            return
        }

        let text = view.text as NSString
        let location = min(selection.location, text.length)
        let selected = NSRange(location: location, length: min(selection.length, text.length - location))
        let paragraphs = text.paragraphRange(for: selected)
        guard paragraphs.length > 0 else {
            var attributes = view.typingAttributes
            attributes[.paragraphStyle] = Self.paragraph(alignment: alignment)
            view.typingAttributes = attributes
            updateSelection(from: view)
            return
        }

        var changes: [(NSRange, NSMutableParagraphStyle)] = []
        view.textStorage.enumerateAttribute(.paragraphStyle, in: paragraphs) { value, range, _ in
            let paragraph = (value as? NSParagraphStyle)?.mutableCopy() as? NSMutableParagraphStyle
                ?? Self.paragraph(alignment: alignment)
            paragraph.alignment = alignment
            changes.append((range, paragraph))
        }
        view.textStorage.beginEditing()
        for (range, paragraph) in changes {
            view.textStorage.addAttribute(.paragraphStyle, value: paragraph, range: range)
        }
        view.textStorage.endEditing()
        view.selectedRange = selection
        contentChanged(in: view)
    }

    private func changeFonts(_ transform: (UIFont) -> UIFont) {
        guard let view = textView else { return }
        let selection = view.selectedRange
        guard selection.length > 0 else {
            var attributes = view.typingAttributes
            let font = attributes[.font] as? UIFont ?? UIFont.systemFont(ofSize: Self.defaultSize)
            attributes[.font] = transform(font)
            view.typingAttributes = attributes
            updateSelection(from: view)
            return
        }

        var changes: [(NSRange, UIFont)] = []
        view.textStorage.enumerateAttribute(.font, in: selection) { value, range, _ in
            let font = value as? UIFont ?? UIFont.systemFont(ofSize: Self.defaultSize)
            changes.append((range, transform(font)))
        }
        view.textStorage.beginEditing()
        for (range, font) in changes {
            view.textStorage.addAttribute(.font, value: font, range: range)
        }
        view.textStorage.endEditing()
        view.selectedRange = selection
        contentChanged(in: view)
    }

    private func persist() {
        let range = NSRange(location: 0, length: document.length)
        guard let data = try? document.data(
            from: range,
            documentAttributes: [.documentType: NSAttributedString.DocumentType.rtf]
        ) else { return }
        defaults.set(data, forKey: Self.storageKey)
    }

    private static func attributes(size: CGFloat, bold: Bool, alignment: NSTextAlignment) -> [NSAttributedString.Key: Any] {
        [
            .font: font(size: size, bold: bold),
            .foregroundColor: UIColor.black,
            .paragraphStyle: paragraph(alignment: alignment),
        ]
    }

    private static func font(size: CGFloat, bold: Bool) -> UIFont {
        bold ? .boldSystemFont(ofSize: size) : .systemFont(ofSize: size)
    }

    private static func paragraph(alignment: NSTextAlignment) -> NSMutableParagraphStyle {
        let paragraph = NSMutableParagraphStyle()
        paragraph.alignment = alignment
        paragraph.lineBreakMode = .byWordWrapping
        paragraph.lineSpacing = 4
        return paragraph
    }
}

struct RichTextEditor: UIViewRepresentable {
    let draft: RichTextDraft

    func makeUIView(context: Context) -> UITextView {
        let view = UITextView()
        view.delegate = context.coordinator
        view.backgroundColor = .white
        view.textContainerInset = UIEdgeInsets(top: 12, left: 12, bottom: 12, right: 12)
        view.textContainer.lineFragmentPadding = 0
        view.accessibilityLabel = "Text to print"
        draft.attach(view)
        return view
    }

    func updateUIView(_ view: UITextView, context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator(draft: draft) }

    final class Coordinator: NSObject, UITextViewDelegate {
        let draft: RichTextDraft

        init(draft: RichTextDraft) { self.draft = draft }

        func textViewDidChange(_ textView: UITextView) {
            draft.contentChanged(in: textView)
        }

        func textViewDidChangeSelection(_ textView: UITextView) {
            draft.updateSelection(from: textView)
        }
    }
}
