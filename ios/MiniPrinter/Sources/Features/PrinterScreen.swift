import PhotosUI
import SwiftUI

struct PrinterScreen: View {
    @State private var store = PrinterStore()
    @State private var textDraft = RichTextDraft()
    @State private var selectedPhoto: PhotosPickerItem?
    @State private var isSubmittingText = false

    var body: some View {
        NavigationStack {
            Form {
                printerSection
                textSection
                photoSection
                printSection
            }
            .scrollDismissesKeyboard(.interactively)
            .navigationTitle("Mini Printer")
            .toolbar {
                if store.isPrinting { ProgressView() }
            }
            .alert(
                "Printer Error",
                isPresented: Binding(
                    get: { store.errorMessage != nil },
                    set: { if !$0 { store.errorMessage = nil } }
                )
            ) {
                Button("OK") { store.errorMessage = nil }
            } message: {
                Text(store.errorMessage ?? "An unknown error occurred.")
            }
            .onChange(of: selectedPhoto) { _, item in
                Task { await loadPhoto(item) }
            }
            .task { store.scan() }
        }
    }

    private var printerSection: some View {
        Section("Printer") {
            if let name = store.connectedName {
                LabeledContent("Connected to", value: name)
                if let status = store.status {
                    LabeledContent("State", value: status.state.title)
                    LabeledContent("Battery", value: "\(status.battery)%")
                    LabeledContent("Print head") {
                        Text(status.temperature.map { "\($0)°C" } ?? "Unavailable")
                    }
                    LabeledContent("Paper", value: status.paperPresent ? "Ready" : "Out of paper")
                    if let error = status.errorMessage {
                        LabeledContent("Error", value: error)
                    }
                }
                Button("Disconnect", role: .destructive) { store.disconnect() }
                    .disabled(store.isPrinting)
            } else {
                if store.devices.isEmpty {
                    if store.isScanning {
                        Label("Searching nearby…", systemImage: "antenna.radiowaves.left.and.right")
                            .foregroundStyle(.secondary)
                    } else {
                        Label(
                            store.bluetoothAvailable
                                ? "No printer found. Tap Scan to try again."
                                : "Turn on Bluetooth to discover printers.",
                            systemImage: "printer"
                        )
                        .foregroundStyle(.secondary)
                    }
                }
                ForEach(store.devices) { device in
                    Button {
                        store.connect(to: device)
                    } label: {
                        HStack {
                            Label(device.name, systemImage: "printer")
                            Spacer()
                            if store.connectingPrinterID == device.id {
                                ProgressView()
                                Text("Connecting…")
                                    .foregroundStyle(.secondary)
                            } else {
                                Image(systemName: "chevron.right")
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                    .disabled(store.isConnecting)
                }
                Button {
                    store.scan()
                } label: {
                    Label(store.isScanning ? "Scanning…" : "Scan for Printers", systemImage: "arrow.clockwise")
                }
                .disabled(!store.bluetoothAvailable || store.isConnecting || store.isScanning)
            }
        }
    }

    private var textSection: some View {
        Section("Text") {
            RichTextEditor(draft: textDraft)
                .frame(height: 150)

            HStack(spacing: 8) {
                Button { textDraft.toggleBold() } label: {
                    Image(systemName: "bold")
                }
                .tint(textDraft.selectedBold ? .accentColor : .secondary)
                .accessibilityLabel("Bold")

                Menu {
                    Button("Small · 18 pt") { textDraft.setFontSize(18) }
                    Button("Medium · 24 pt") { textDraft.setFontSize(24) }
                    Button("Large · 32 pt") { textDraft.setFontSize(32) }
                } label: {
                    Text("\(textDraft.selectedFontSize) pt")
                }
                .accessibilityLabel("Font size")

                Spacer(minLength: 0)

                alignmentButton("text.alignleft", alignment: .left, label: "Align left")
                alignmentButton("text.aligncenter", alignment: .center, label: "Align center")
                alignmentButton("text.alignright", alignment: .right, label: "Align right")
            }
            .buttonStyle(.bordered)
            .controlSize(.small)

            Text("Select text to format it. With no selection, styles apply to new text; alignment applies to the paragraph.")
                .font(.footnote)
                .foregroundStyle(.secondary)

            Button(action: submitText) {
                if isSubmittingText {
                    Label("Printing Text…", systemImage: "printer.fill")
                } else {
                    Label("Print Text", systemImage: "printer.fill")
                }
            }
            .disabled(isSubmittingText || store.isPrinting)
        }
    }

    @MainActor
    private func submitText() {
        guard !isSubmittingText && !store.isPrinting else { return }
        textDraft.dismissKeyboard()
        let document = textDraft.snapshot()
        isSubmittingText = true
        Task {
            await store.printText(document)
            isSubmittingText = false
        }
    }

    private func alignmentButton(_ symbol: String, alignment: NSTextAlignment, label: String) -> some View {
        Button { textDraft.setAlignment(alignment) } label: {
            Image(systemName: symbol)
        }
        .tint(textDraft.selectedAlignment == alignment ? .accentColor : .secondary)
        .accessibilityLabel(label)
    }

    private var photoSection: some View {
        Section("Photo") {
            PhotosPicker(selection: $selectedPhoto, matching: .images) {
                Label("Choose Photo", systemImage: "photo.on.rectangle")
            }
            if let photo = store.photo {
                Image(uiImage: photo)
                    .resizable()
                    .scaledToFit()
                    .frame(maxWidth: .infinity, maxHeight: 260)
                    .accessibilityLabel("Selected photo preview")
            } else {
                Text("Select a photo to print it in black and white.")
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var printSection: some View {
        Section("Print") {
            LabeledContent("Density", value: "\(store.density)%")
            Slider(
                value: Binding(
                    get: { Double(store.density) },
                    set: { store.density = Int($0) }
                ),
                in: 1...100,
                step: 1
            )
            .accessibilityLabel("Print density")

            Button {
                Task { await store.printPhoto() }
            } label: {
                Label("Print Photo", systemImage: "printer.fill")
            }
            .disabled(!store.isConnected || store.photo == nil || store.isPrinting || isSubmittingText || store.status?.paperPresent == false)

            Button {
                Task { await store.printTestPage() }
            } label: {
                Label("Print Test Page", systemImage: "doc.text")
            }
            .disabled(!store.isConnected || store.isPrinting || isSubmittingText || store.status?.paperPresent == false)

            if store.isPrinting {
                LabeledContent("Submitted lines", value: "\(store.submittedLines)")
            }
        }
    }

    @MainActor
    private func loadPhoto(_ item: PhotosPickerItem?) async {
        guard let item else { store.photo = nil; return }
        do {
            guard let data = try await item.loadTransferable(type: Data.self),
                  let image = UIImage(data: data)
            else { throw PhotoError.invalidImage }
            store.photo = image
        } catch {
            store.errorMessage = error.localizedDescription
        }
    }

    private enum PhotoError: LocalizedError {
        case invalidImage
        var errorDescription: String? { "The selected photo could not be opened." }
    }
}
