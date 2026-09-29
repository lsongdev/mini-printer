import PhotosUI
import SwiftUI

struct PrinterScreen: View {
    @State private var store = PrinterStore()
    @State private var selectedPhoto: PhotosPickerItem?

    var body: some View {
        NavigationStack {
            Form {
                printerSection
                photoSection
                printSection
            }
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
                if store.isConnecting {
                    HStack {
                        ProgressView()
                        Text("Connecting…")
                    }
                }
                if store.devices.isEmpty {
                    ContentUnavailableView(
                        "No Printer Found",
                        systemImage: "printer",
                        description: Text(
                            !store.bluetoothAvailable
                                ? "Turn on Bluetooth to discover printers."
                                : store.isScanning ? "Searching nearby…" : "Tap Scan to find your printer."
                        )
                    )
                }
                ForEach(store.devices) { device in
                    Button {
                        store.connect(to: device)
                    } label: {
                        HStack {
                            Label(device.name, systemImage: "printer")
                            Spacer()
                            Image(systemName: "chevron.right")
                                .foregroundStyle(.secondary)
                        }
                    }
                    .disabled(store.isConnecting)
                }
                Button {
                    store.scan()
                } label: {
                    Label(store.isScanning ? "Scan Again" : "Scan for Printers", systemImage: "arrow.clockwise")
                }
                .disabled(!store.bluetoothAvailable || store.isConnecting)
            }
        }
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
            .disabled(!store.isConnected || store.photo == nil || store.isPrinting || store.status?.paperPresent == false)

            Button {
                Task { await store.printTestPage() }
            } label: {
                Label("Print Test Page", systemImage: "doc.text")
            }
            .disabled(!store.isConnected || store.isPrinting || store.status?.paperPresent == false)

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
