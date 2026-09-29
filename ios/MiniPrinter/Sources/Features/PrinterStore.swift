import Observation
import UIKit

@MainActor
@Observable
final class PrinterStore {
    var devices: [DiscoveredPrinter] = []
    var isScanning = false
    var isConnecting = false
    var bluetoothAvailable = false
    var connectedName: String?
    var status: PrinterStatus?
    var photo: UIImage?
    var density = 64
    var isPrinting = false
    var submittedLines = 0
    var errorMessage: String?

    @ObservationIgnored private let transport: BLEPrinterTransport
    @ObservationIgnored private var sawPrintingForJob = false

    var isConnected: Bool { connectedName != nil }

    init() {
        let transport = BLEPrinterTransport()
        self.transport = transport
        transport.onDevices = { [weak self] in self?.devices = $0 }
        transport.onScanning = { [weak self] in self?.isScanning = $0 }
        transport.onBluetoothPower = { [weak self] in self?.bluetoothAvailable = $0 }
        transport.onReady = { [weak self] name in
            self?.connectedName = name
            self?.isConnecting = false
            if name == nil { self?.status = nil }
        }
        transport.onStatus = { [weak self] status in
            guard let self else { return }
            self.status = status
            if self.isPrinting && status.state == .printing {
                self.sawPrintingForJob = true
            }
        }
        transport.onFailure = { [weak self] error in
            self?.isConnecting = false
            self?.errorMessage = error.localizedDescription
        }
    }

    func scan() { transport.startScan() }
    func stopScan() { transport.stopScan() }
    func disconnect() { transport.disconnect() }

    func connect(to device: DiscoveredPrinter) {
        do {
            isConnecting = true
            try transport.connect(to: device.id)
        } catch {
            isConnecting = false
            errorMessage = error.localizedDescription
        }
    }

    func printPhoto() async {
        guard let photo else { return }
        do { await printLines(try PrintRasterizer.photoLines(from: photo)) }
        catch { errorMessage = error.localizedDescription }
    }

    func printTestPage() async {
        await printLines(PrintRasterizer.testPageLines())
    }

    private func printLines(_ lines: [Data]) async {
        guard isConnected else {
            errorMessage = "Connect to a printer first."
            return
        }
        guard status?.paperPresent != false else {
            errorMessage = "Load paper before printing."
            return
        }
        guard !isPrinting else { return }

        isPrinting = true
        sawPrintingForJob = false
        submittedLines = 0
        var jobStarted = false
        defer { isPrinting = false }

        do {
            let batches = try PrinterWireProtocol.lineBatches(lines)
            try await transport.send(PrinterWireProtocol.begin(density: UInt8(density)))
            jobStarted = true

            for (index, batch) in batches.enumerated() {
                try await transport.send(batch)
                submittedLines = min((index + 1) * PrinterWireProtocol.linesPerBatch, lines.count)
                if let status, status.state == .error || (sawPrintingForJob && status.errorCode != 0) {
                    throw PrintError.printer(status.errorMessage ?? "Printer stopped unexpectedly.")
                }
            }

            try await transport.send(PrinterWireProtocol.end)
            try await waitUntilComplete()
        } catch {
            if jobStarted { try? await transport.send(PrinterWireProtocol.cancel) }
            errorMessage = error.localizedDescription
        }
    }

    private func waitUntilComplete() async throws {
        let deadline = Date().addingTimeInterval(90)
        while Date() < deadline {
            if let status {
                if status.state == .error || (sawPrintingForJob && status.errorCode != 0) {
                    throw PrintError.printer(status.errorMessage ?? "Printer stopped unexpectedly.")
                }
                if sawPrintingForJob && status.state == .idle { return }
            }
            try await Task.sleep(nanoseconds: 250_000_000)
        }
        throw PrintError.printer("The printer did not report completion in time.")
    }

    private enum PrintError: LocalizedError {
        case printer(String)
        var errorDescription: String? {
            if case let .printer(message) = self { return message }
            return nil
        }
    }
}
