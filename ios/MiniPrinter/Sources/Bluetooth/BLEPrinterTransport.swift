import CoreBluetooth
import Foundation

struct DiscoveredPrinter: Identifiable, Equatable {
    let id: UUID
    let name: String
    let rssi: Int
}

@MainActor
final class BLEPrinterTransport: NSObject, @preconcurrency CBCentralManagerDelegate, @preconcurrency CBPeripheralDelegate {
    enum Error: LocalizedError {
        case bluetoothUnavailable
        case notConnected
        case unsupportedPrinter
        case writeInProgress
        case writeTimedOut

        var errorDescription: String? {
            switch self {
            case .bluetoothUnavailable: "Turn on Bluetooth to find your printer."
            case .notConnected: "Connect to a printer first."
            case .unsupportedPrinter: "This device does not provide the Mini Printer service."
            case .writeInProgress: "Another Bluetooth write is still in progress."
            case .writeTimedOut: "The printer did not acknowledge the Bluetooth write."
            }
        }
    }

    var onDevices: (([DiscoveredPrinter]) -> Void)?
    var onReady: ((String?) -> Void)?
    var onStatus: ((PrinterStatus) -> Void)?
    var onFailure: ((Swift.Error) -> Void)?
    var onScanning: ((Bool) -> Void)?
    var onBluetoothPower: ((Bool) -> Void)?

    private static let serviceUUID = CBUUID(string: "4fafc201-1fb5-459e-8fcc-c5c9c331914b")
    private static let characteristicUUID = CBUUID(string: "beb5483e-36e1-4688-b7f5-ea07361b26a8")

    private var central: CBCentralManager!
    private var peripherals: [UUID: CBPeripheral] = [:]
    private var devices: [DiscoveredPrinter] = []
    private var peripheral: CBPeripheral?
    private var characteristic: CBCharacteristic?
    private var scanRequested = false
    private var disconnectRequested = false
    private var ready = false
    private var pendingWrite: (id: UUID, continuation: CheckedContinuation<Void, Swift.Error>)?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    func startScan() {
        scanRequested = true
        guard central.state == .poweredOn else {
            return
        }
        central.stopScan()
        devices.removeAll()
        peripherals.removeAll()
        onDevices?([])
        // The current ESP32 advertisement contains the printer name but does
        // not advertise its service UUID, so foreground discovery uses the name.
        central.scanForPeripherals(withServices: nil)
        onScanning?(true)
    }

    func stopScan() {
        scanRequested = false
        central.stopScan()
        onScanning?(false)
    }

    func connect(to id: UUID) throws {
        guard central.state == .poweredOn else { throw Error.bluetoothUnavailable }
        guard let target = peripherals[id] else { throw Error.unsupportedPrinter }
        stopScan()
        disconnectRequested = false
        peripheral = target
        characteristic = nil
        ready = false
        central.connect(target)
    }

    func disconnect() {
        disconnectRequested = true
        guard let peripheral else { return }
        central.cancelPeripheralConnection(peripheral)
        clearConnection()
    }

    func send(_ data: Data) async throws {
        guard ready, let peripheral, let characteristic else { throw Error.notConnected }
        let maximum = min(512, peripheral.maximumWriteValueLength(for: .withResponse))
        guard maximum > 0 else { throw Error.unsupportedPrinter }

        for offset in stride(from: 0, to: data.count, by: maximum) {
            let end = min(offset + maximum, data.count)
            try await write(Data(data[offset..<end]), to: characteristic, on: peripheral)
        }
    }

    private func write(_ data: Data, to characteristic: CBCharacteristic, on peripheral: CBPeripheral) async throws {
        guard pendingWrite == nil else { throw Error.writeInProgress }
        let id = UUID()
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Swift.Error>) in
            pendingWrite = (id, continuation)
            peripheral.writeValue(data, for: characteristic, type: .withResponse)
            Task { @MainActor [weak self] in
                try? await Task.sleep(nanoseconds: 8_000_000_000)
                self?.timeoutWrite(id: id)
            }
        }
    }

    private func timeoutWrite(id: UUID) {
        guard let pendingWrite, pendingWrite.id == id else { return }
        self.pendingWrite = nil
        pendingWrite.continuation.resume(throwing: Error.writeTimedOut)
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        clearConnection()
    }

    private func clearConnection() {
        ready = false
        characteristic = nil
        peripheral = nil
        if let pendingWrite {
            self.pendingWrite = nil
            pendingWrite.continuation.resume(throwing: Error.notConnected)
        }
        onReady?(nil)
    }

    private func fail(_ error: Swift.Error) {
        if let peripheral { central.cancelPeripheralConnection(peripheral) }
        clearConnection()
        onFailure?(error)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        onBluetoothPower?(central.state == .poweredOn)
        if central.state == .poweredOn {
            if scanRequested { startScan() }
        } else {
            onScanning?(false)
            if ready || peripheral != nil { clearConnection() }
        }
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name
        guard let name, name.hasPrefix("Mini-Printer") else { return }
        peripherals[peripheral.identifier] = peripheral
        let item = DiscoveredPrinter(id: peripheral.identifier, name: name, rssi: RSSI.intValue)
        devices.removeAll { $0.id == item.id }
        devices.append(item)
        devices.sort { $0.rssi > $1.rssi }
        onDevices?(devices)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.delegate = self
        peripheral.discoverServices([Self.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Swift.Error?) {
        fail(error ?? Error.notConnected)
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Swift.Error?) {
        let expected = disconnectRequested
        clearConnection()
        disconnectRequested = false
        if !expected, let error { onFailure?(error) }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Swift.Error?) {
        if let error { fail(error); return }
        guard let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            fail(Error.unsupportedPrinter)
            return
        }
        peripheral.discoverCharacteristics([Self.characteristicUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Swift.Error?) {
        if let error { fail(error); return }
        guard let characteristic = service.characteristics?.first(where: { $0.uuid == Self.characteristicUUID }),
              characteristic.properties.contains(.write),
              characteristic.properties.contains(.notify)
        else { fail(Error.unsupportedPrinter); return }
        self.characteristic = characteristic
        peripheral.setNotifyValue(true, for: characteristic)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Swift.Error?) {
        if let error { fail(error); return }
        guard characteristic.isNotifying else { fail(Error.unsupportedPrinter); return }
        ready = true
        onReady?(peripheral.name ?? "Mini-Printer")
        peripheral.readValue(for: characteristic)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Swift.Error?) {
        guard error == nil, let value = characteristic.value,
              let status = try? PrinterStatus(frame: value) else { return }
        onStatus?(status)
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Swift.Error?) {
        guard let pendingWrite else { return }
        self.pendingWrite = nil
        if let error {
            pendingWrite.continuation.resume(throwing: error)
        } else {
            pendingWrite.continuation.resume()
        }
    }
}
