const WIDTH = 384
const BYTES_PER_LINE = WIDTH / 8
const LINES_PER_BATCH = 8

const SERVICE_UUID = '4fafc201-1fb5-459e-8fcc-c5c9c331914b'
const CHARACTERISTIC_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26a8'

const STATES = ['Ready', 'Printing', 'Feeding', 'Needs attention']
const ERRORS = [
  null,
  'Out of paper',
  'Print head overheated',
  'Temperature sensor failed',
  'Print job timed out',
  'Print job cancelled',
  'Printer rejected the protocol frame'
]

function frame(command, payload = new Uint8Array(0)) {
  if (!(payload instanceof Uint8Array) || payload.length > 48) {
    throw new Error('A protocol payload must be at most 48 bytes.')
  }
  const bytes = new Uint8Array(6 + payload.length)
  bytes.set([0x4d, 0x50, 1, command, payload.length, 0])
  bytes.set(payload, 6)
  return bytes
}

function begin(density) {
  if (!Number.isInteger(density) || density < 1 || density > 100) {
    throw new Error('Print density must be between 1 and 100.')
  }
  return frame(0x01, new Uint8Array([density]))
}

function line(raster) {
  if (!(raster instanceof Uint8Array) || raster.length !== BYTES_PER_LINE) {
    throw new Error('A print line must contain exactly 48 bytes.')
  }
  return frame(0x02, raster)
}

function lineBatch(lines) {
  if (!Array.isArray(lines) || lines.length > LINES_PER_BATCH) {
    throw new Error('A batch can contain at most eight print lines.')
  }
  const bytes = new Uint8Array(lines.length * (6 + BYTES_PER_LINE))
  lines.forEach((raster, index) => bytes.set(line(raster), index * (6 + BYTES_PER_LINE)))
  return bytes
}

function decodeStatus(value) {
  const bytes = value instanceof ArrayBuffer ? new Uint8Array(value) : value
  if (!(bytes instanceof Uint8Array) || bytes.length !== 11 ||
      bytes[0] !== 0x4d || bytes[1] !== 0x50 || bytes[2] !== 1 ||
      bytes[3] !== 0x80 || bytes[4] !== 5 || bytes[5] !== 0 ||
      bytes[6] > 100 || bytes[8] > 1 || bytes[9] >= STATES.length) {
    return null
  }
  const errorCode = bytes[10]
  return {
    battery: bytes[6],
    temperature: bytes[7] === 255 ? null : bytes[7],
    paperPresent: bytes[8] === 0,
    state: bytes[9],
    stateText: STATES[bytes[9]],
    errorCode,
    errorText: ERRORS[errorCode] || (errorCode ? `Printer error ${errorCode}` : null)
  }
}

module.exports = {
  WIDTH,
  BYTES_PER_LINE,
  LINES_PER_BATCH,
  SERVICE_UUID,
  CHARACTERISTIC_UUID,
  begin,
  line,
  lineBatch,
  end: frame(0x03),
  cancel: frame(0x04),
  decodeStatus
}
