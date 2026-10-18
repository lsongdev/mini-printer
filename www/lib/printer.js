import { begin, frame, decodeStatus, LINE_BYTES, SERVICE_UUID, CHARACTERISTIC_UUID } from './protocol.js';
import { BLETransport } from './ble.js';

const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export class Printer {
  constructor({ onStatus = () => {}, onDisconnect = () => {}, transport } = {}) {
    this.onStatus = onStatus;
    this.transport = transport || new BLETransport({
      onValue: (value) => this.receive(value),
      onDisconnect: () => { this.status = null; onDisconnect(); }
    });
    this.busy = false;
    this.cancelRequested = false;
  }

  receive(value) {
    const status = decodeStatus(value);
    if (status) { this.status = status; this.onStatus(status); }
  }

  connect() {
    this.status = null;
    return this.transport.connect({
      filters: [{ namePrefix: 'Mini-Printer' }, { services: [SERVICE_UUID] }],
      serviceUUID: SERVICE_UUID, characteristicUUID: CHARACTERISTIC_UUID
    });
  }

  disconnect() { this.transport.disconnect(); this.status = null; }
  cancel() { this.cancelRequested = true; }

  async refresh() {
    await this.transport.write(frame(0x10));
    // A read provides fresh status even when notifications are delayed or lost.
    await this.transport.read();
    return this.status;
  }

  checkJob() {
    if (this.cancelRequested) throw new Error('打印已取消');
    if (!this.transport.connected) throw new Error('打印机已断开');
    if (this.status && (this.status.error || this.status.state === 3)) {
      throw new Error(this.status.errorText || '打印机需要处理');
    }
  }

  async waitForState(state, timeout) {
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
      await this.refresh();
      this.checkJob();
      if (this.status?.state === state) return;
      await pause(200);
    }
    throw new Error('等待打印机状态超时，请检查设备');
  }

  async print(lines, density, onProgress = () => {}) {
    if (this.busy) throw new Error('已有打印任务');
    if (!this.transport.connected) throw new Error('请先连接打印机');
    if (!Array.isArray(lines) || !lines.length || lines.some((line) =>
      !(line instanceof Uint8Array) || line.length !== LINE_BYTES)) throw new Error('无效的打印图像');
    const start = begin(density);
    this.busy = true;
    this.cancelRequested = false;
    let started = false;
    try {
      await this.refresh();
      if (!this.status) throw new Error('未收到打印机状态，请重新连接');
      if (!this.status.paperPresent) throw new Error('缺纸，请装入热敏纸');
      if (this.status.state === 1 || this.status.state === 2) throw new Error('打印机正在忙，请稍后再试');
      // BEGIN clears a previous cancelled/error state in the firmware.
      await this.transport.write(start);
      started = true;
      this.status = null;
      await this.waitForState(1, 10000);
      for (let index = 0; index < lines.length; index++) {
        this.checkJob();
        await this.transport.write(frame(2, lines[index]));
        onProgress((index + 1) / lines.length);
      }
      this.checkJob();
      await this.transport.write(frame(3));
      // Upload completion is not physical print completion; wait for fresh idle.
      await this.waitForState(0, 60000);
      started = false;
    } catch (error) {
      if (started && this.transport.connected) {
        try { await this.transport.write(frame(4)); } catch { /* Keep original failure. */ }
      }
      throw error;
    } finally { this.busy = false; }
  }
}
