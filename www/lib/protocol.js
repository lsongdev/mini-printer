// Mini Printer MP v1. Keep device protocol separate from the BLE transport.
export const WIDTH = 384;
export const LINE_BYTES = WIDTH / 8;
export const SERVICE_UUID = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
export const CHARACTERISTIC_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26a8';
export const states = ['就绪', '打印中', '走纸中', '需要处理'];
export const errors = ['', '缺纸，请装入热敏纸', '打印头过热，请稍候', '温度传感器异常', '打印超时', '打印已取消', '打印协议错误'];

export function frame(type, payload = new Uint8Array()) {
  const lengths = { 1: 1, 2: LINE_BYTES, 3: 0, 4: 0, 16: 0 };
  if (!(payload instanceof Uint8Array) || lengths[type] !== payload.length) {
    throw new Error('无效的 MP 打印帧');
  }
  const bytes = new Uint8Array(6 + payload.length);
  bytes.set([0x4d, 0x50, 1, type, payload.length, 0]);
  bytes.set(payload, 6);
  return bytes;
}

export function begin(density) {
  if (!Number.isInteger(density) || density < 1 || density > 100) throw new Error('浓度应为 1–100');
  return frame(1, new Uint8Array([density]));
}

export function decodeStatus(value) {
  const bytes = value instanceof DataView
    ? new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
    : value instanceof ArrayBuffer ? new Uint8Array(value) : value;
  if (!(bytes instanceof Uint8Array) || bytes.length !== 11 ||
      bytes[0] !== 0x4d || bytes[1] !== 0x50 || bytes[2] !== 1 ||
      bytes[3] !== 0x80 || bytes[4] !== 5 || bytes[5] !== 0 ||
      bytes[6] > 100 || bytes[8] > 1 || bytes[9] > 3) return null;
  return {
    battery: bytes[6], temperature: bytes[7] === 255 ? null : bytes[7],
    paperPresent: bytes[8] === 0, state: bytes[9], error: bytes[10],
    stateText: states[bytes[9]], errorText: bytes[10] ? errors[bytes[10]] || `打印机错误 ${bytes[10]}` : ''
  };
}
