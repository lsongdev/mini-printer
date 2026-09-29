import Foundation

enum PrinterWireProtocol {
    static let width = 384
    static let bytesPerLine = width / 8
    static let linesPerBatch = 8

    enum Error: LocalizedError {
        case invalidLine
        case invalidStatus

        var errorDescription: String? {
            switch self {
            case .invalidLine: "A print line must contain exactly 48 bytes."
            case .invalidStatus: "The printer returned an invalid status frame."
            }
        }
    }

    static func begin(density: UInt8) -> Data {
        frame(command: 0x01, payload: Data([density]))
    }

    static func line(_ raster: Data) throws -> Data {
        guard raster.count == bytesPerLine else { throw Error.invalidLine }
        return frame(command: 0x02, payload: raster)
    }

    static let end = frame(command: 0x03)
    static let cancel = frame(command: 0x04)

    static func lineBatches(_ lines: [Data]) throws -> [Data] {
        var batches: [Data] = []
        for start in stride(from: 0, to: lines.count, by: linesPerBatch) {
            var batch = Data()
            for line in lines[start..<min(start + linesPerBatch, lines.count)] {
                batch.append(try self.line(line))
            }
            batches.append(batch)
        }
        return batches
    }

    private static func frame(command: UInt8, payload: Data = Data()) -> Data {
        var data = Data([0x4D, 0x50, 0x01, command])
        let length = UInt16(payload.count)
        data.append(UInt8(truncatingIfNeeded: length))
        data.append(UInt8(truncatingIfNeeded: length >> 8))
        data.append(payload)
        return data
    }
}

struct PrinterStatus: Equatable {
    enum State: UInt8 {
        case idle = 0
        case printing = 1
        case feeding = 2
        case error = 3

        var title: String {
            switch self {
            case .idle: "Ready"
            case .printing: "Printing"
            case .feeding: "Feeding"
            case .error: "Needs attention"
            }
        }
    }

    let battery: Int
    let temperature: Int?
    let paperPresent: Bool
    let state: State
    let errorCode: UInt8

    var errorMessage: String? {
        switch errorCode {
        case 0: nil
        case 1: "Out of paper"
        case 2: "Print head overheated"
        case 3: "Temperature sensor failed"
        case 4: "Print job timed out"
        case 5: "Print job cancelled"
        case 6: "Printer rejected the protocol frame"
        default: "Printer error \(errorCode)"
        }
    }

    init(frame: Data) throws {
        guard frame.count == 11,
              frame[0] == 0x4D, frame[1] == 0x50,
              frame[2] == 0x01, frame[3] == 0x80,
              frame[4] == 5, frame[5] == 0,
              frame[6] <= 100,
              frame[8] <= 1,
              let state = State(rawValue: frame[9])
        else { throw PrinterWireProtocol.Error.invalidStatus }

        battery = Int(frame[6])
        temperature = frame[7] == 255 ? nil : Int(frame[7])
        paperPresent = frame[8] == 0
        self.state = state
        errorCode = frame[10]
    }
}
