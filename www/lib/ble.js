// Acknowledged, serialized GATT writes. No printer-specific data lives here.
export class BLETransport {
  constructor({ onValue = () => {}, onDisconnect = () => {} } = {}) {
    this.onValue = onValue;
    this.onDisconnect = onDisconnect;
    this.queue = Promise.resolve();
    this.handleValue = (event) => this.onValue(event.target.value);
    this.handleDisconnect = () => {
      this.detach();
      this.onDisconnect();
    };
  }

  get connected() { return !!(this.device?.gatt.connected && this.characteristic); }

  async connect({ filters, serviceUUID, characteristicUUID }) {
    if (this.device) this.disconnect();
    // requestDevice must run directly from the user's click, before any await.
    const device = await navigator.bluetooth.requestDevice({ filters, optionalServices: [serviceUUID] });
    this.device = device;
    device.addEventListener('gattserverdisconnected', this.handleDisconnect);
    try {
      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(serviceUUID);
      const characteristic = await service.getCharacteristic(characteristicUUID);
      if (!characteristic.properties.write || !characteristic.properties.notify || !characteristic.properties.read) {
        throw new Error('设备不支持所需的写入、读取和状态通知');
      }
      this.characteristic = characteristic;
      characteristic.addEventListener('characteristicvaluechanged', this.handleValue);
      await characteristic.startNotifications();
      if (characteristic.properties.read) await this.read();
      if (!this.connected) throw new Error('打印机已断开');
      return device.name || 'Mini-Printer';
    } catch (error) {
      this.disconnect();
      throw error;
    }
  }

  detach() {
    this.characteristic?.removeEventListener('characteristicvaluechanged', this.handleValue);
    this.device?.removeEventListener('gattserverdisconnected', this.handleDisconnect);
    this.characteristic = null;
    this.device = null;
  }

  disconnect() {
    const device = this.device;
    this.detach();
    device?.gatt.disconnect();
  }

  enqueue(operation) {
    const characteristic = this.characteristic;
    const run = this.queue.then(async () => {
      if (!this.connected || this.characteristic !== characteristic) throw new Error('打印机已断开，请重新连接');
      let timer;
      try {
        return await Promise.race([
          operation(characteristic),
          new Promise((_, reject) => {
            timer = setTimeout(() => {
              if (this.characteristic === characteristic) {
                this.disconnect();
                this.onDisconnect();
              }
              reject(new Error('蓝牙操作超时，连接已关闭，请重新连接'));
            }, 15000);
          })
        ]);
      } finally { clearTimeout(timer); }
    });
    this.queue = run.catch(() => {});
    return run;
  }

  async write(bytes) {
    const original = this.characteristic;
    return this.enqueue(async (characteristic) => {
      // Web Bluetooth has no MTU query; 20 bytes works with the minimum BLE MTU.
      // Finish the entire frame before allowing another command (including CANCEL).
      for (let offset = 0; offset < bytes.length; offset += 20) {
        if (this.characteristic !== characteristic || !this.connected) throw new Error('打印机已断开');
        const chunk = bytes.slice(offset, offset + 20);
        await characteristic.writeValueWithResponse(chunk);
      }
    }).catch((error) => {
      // A failed fragment can leave the peripheral parser mid-frame.
      if (this.characteristic === original) {
        this.disconnect();
        this.onDisconnect();
      }
      throw error;
    });
  }

  async read() {
    return this.enqueue(async (characteristic) => {
      const value = await characteristic.readValue();
      if (this.characteristic === characteristic) this.onValue(value);
      return value;
    });
  }
}
