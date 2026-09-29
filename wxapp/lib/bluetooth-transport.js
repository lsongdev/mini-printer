function errorMessage(error, fallback) {
  return error && (error.errMsg || error.message) || fallback
}

class BluetoothTransport {
  constructor(api, options) {
    this.api = api
    this.serviceUUID = options.serviceUUID.toLowerCase()
    this.characteristicUUID = options.characteristicUUID.toLowerCase()
    this.namePrefix = options.namePrefix
    this.onDevices = options.onDevices || (() => {})
    this.onState = options.onState || (() => {})
    this.onValue = options.onValue || (() => {})
    this.onDisconnected = options.onDisconnected || (() => {})

    this.devices = new Map()
    this.adapterOpen = false
    this.bluetoothAvailable = false
    this.scanning = false
    this.connectingId = null
    this.deviceId = null
    this.serviceId = null
    this.characteristicId = null
    this.ready = false
    this.mtu = 23
    this.epoch = 0
    this.scanTimer = null
    this.scanStarting = null
    this.sending = false
    this.pendingWriteReject = null
    this.destroyed = false

    this.deviceListener = (result) => this.handleDevices(result)
    this.connectionListener = (result) => this.handleConnection(result)
    this.valueListener = (result) => this.handleValue(result)
    this.adapterListener = (result) => this.handleAdapter(result)
    this.mtuListener = (result) => {
      if (result.deviceId === this.deviceId && result.mtu >= 23) this.mtu = result.mtu
    }
    api.onBluetoothDeviceFound(this.deviceListener)
    api.onBLEConnectionStateChange(this.connectionListener)
    api.onBLECharacteristicValueChange(this.valueListener)
    api.onBluetoothAdapterStateChange(this.adapterListener)
    if (api.onBLEMTUChange) api.onBLEMTUChange(this.mtuListener)
  }

  call(name, options = {}, timeout = 10000) {
    return new Promise((resolve, reject) => {
      if (typeof this.api[name] !== 'function') {
        reject(new Error(`${name} is unavailable in this WeChat version.`))
        return
      }
      let settled = false
      const timer = setTimeout(() => finish(new Error(`${name} timed out.`)), timeout)
      const finish = (failure, result) => {
        if (settled) return
        settled = true
        clearTimeout(timer)
        if (failure) reject(failure)
        else resolve(result)
      }
      try {
        this.api[name]({
          ...options,
          success: (result) => finish(null, result),
          fail: (failure) => finish(new Error(errorMessage(failure, `${name} failed.`)))
        })
      } catch (failure) {
        finish(failure)
      }
    })
  }

  emitState() {
    if (!this.destroyed) {
      this.onState({
        bluetoothAvailable: this.bluetoothAvailable,
        scanning: this.scanning,
        connectingId: this.connectingId,
        deviceId: this.ready ? this.deviceId : null,
        connectedName: this.ready ? (this.devices.get(this.deviceId) || {}).name || 'Mini-Printer' : null
      })
    }
  }

  handleDevices(result) {
    if (!this.scanning || this.destroyed) return
    for (const device of result.devices || []) {
      const name = device.localName || device.name || ''
      if (!name.startsWith(this.namePrefix)) continue
      this.devices.set(device.deviceId, { deviceId: device.deviceId, name, rssi: device.RSSI })
    }
    this.onDevices([...this.devices.values()].sort((a, b) => b.rssi - a.rssi))
  }

  handleAdapter(result) {
    this.bluetoothAvailable = !!result.available
    if (!result.available) {
      this.stopScan()
      this.clearConnection(true)
    }
    this.emitState()
  }

  handleConnection(result) {
    if (result.deviceId !== this.deviceId || result.connected) return
    this.clearConnection(true)
  }

  handleValue(result) {
    if (!this.ready || result.deviceId !== this.deviceId ||
        !result.characteristicId ||
        result.characteristicId.toLowerCase() !== this.characteristicId.toLowerCase()) return
    this.onValue(result.value)
  }

  clearConnection(unexpected = false) {
    const wasReady = this.ready
    this.epoch++
    this.ready = false
    this.connectingId = null
    this.deviceId = null
    this.serviceId = null
    this.characteristicId = null
    this.mtu = 23
    if (this.pendingWriteReject) {
      this.pendingWriteReject(new Error('Printer disconnected.'))
      this.pendingWriteReject = null
    }
    this.emitState()
    if (unexpected && wasReady && !this.destroyed) this.onDisconnected()
  }

  async startScan() {
    if (this.scanStarting) return this.scanStarting
    this.scanStarting = this.startScanOnce()
    try { await this.scanStarting } finally { this.scanStarting = null }
  }

  async startScanOnce() {
    if (this.destroyed) return
    if (!this.adapterOpen) {
      try {
        await this.call('openBluetoothAdapter')
        this.adapterOpen = true
        this.bluetoothAvailable = true
      } catch (failure) {
        this.bluetoothAvailable = false
        this.emitState()
        throw new Error('Turn on Bluetooth and allow WeChat to find nearby printers.')
      }
    }
    await this.stopScan()
    if (this.destroyed) return
    this.devices.clear()
    this.onDevices([])
    try {
      await this.call('startBluetoothDevicesDiscovery', { allowDuplicatesKey: false })
      if (this.destroyed) {
        try { await this.call('stopBluetoothDevicesDiscovery', {}, 3000) } catch (_) { /* Closing. */ }
        return
      }
      this.scanning = true
      this.emitState()
      this.scanTimer = setTimeout(() => this.stopScan(), 12000)
    } catch (failure) {
      this.scanning = false
      this.emitState()
      throw failure
    }
  }

  async stopScan() {
    clearTimeout(this.scanTimer)
    this.scanTimer = null
    if (!this.scanning) return
    this.scanning = false
    this.emitState()
    try { await this.call('stopBluetoothDevicesDiscovery', {}, 3000) } catch (_) { /* Already stopped. */ }
  }

  async connect(deviceId) {
    if (!this.devices.has(deviceId)) throw new Error('Select a discovered printer.')
    await this.stopScan()
    if (this.deviceId || this.connectingId) await this.disconnect()
    const epoch = ++this.epoch
    this.connectingId = deviceId
    this.emitState()
    let created = false
    try {
      await this.call('createBLEConnection', { deviceId, timeout: 20000 }, 22000)
      created = true
      if (epoch !== this.epoch) throw new Error('Connection cancelled.')
      this.deviceId = deviceId

      const services = await this.call('getBLEDeviceServices', { deviceId })
      const service = (services.services || []).find((item) => item.uuid.toLowerCase() === this.serviceUUID)
      if (!service) throw new Error('This device does not provide the Mini Printer service.')
      this.serviceId = service.uuid

      const found = await this.call('getBLEDeviceCharacteristics', { deviceId, serviceId: service.uuid })
      const characteristic = (found.characteristics || []).find((item) =>
        item.uuid.toLowerCase() === this.characteristicUUID)
      if (!characteristic || !characteristic.properties.write || !characteristic.properties.notify) {
        throw new Error('This printer cannot accept print data and status notifications.')
      }
      this.characteristicId = characteristic.uuid

      // Android can negotiate a larger MTU. iOS chooses its own value.
      let platform = ''
      try {
        const info = this.api.getDeviceInfo ? this.api.getDeviceInfo() : this.api.getSystemInfoSync()
        platform = String(info.platform || '').toLowerCase()
      } catch (_) { /* MTU query still works without platform information. */ }
      if (platform === 'android' && this.api.setBLEMTU) {
        try { await this.call('setBLEMTU', { deviceId, mtu: 511 }, 3000) } catch (_) { /* Use current MTU. */ }
      }
      if (this.api.getBLEMTU) {
        try {
          const result = await this.call('getBLEMTU', { deviceId, writeType: 'write' }, 3000)
          if (result.mtu >= 23) this.mtu = result.mtu
        } catch (_) { /* Fall back to 20-byte writes. */ }
      }
      await this.call('notifyBLECharacteristicValueChange', {
        deviceId, serviceId: this.serviceId, characteristicId: this.characteristicId,
        state: true, type: 'notification'
      })
      if (epoch !== this.epoch) throw new Error('Connection cancelled.')
      this.ready = true
      this.connectingId = null
      this.emitState()
      if (characteristic.properties.read) {
        try {
          await this.call('readBLECharacteristicValue', {
            deviceId, serviceId: this.serviceId, characteristicId: this.characteristicId
          }, 3000)
        } catch (_) { /* Status notifications remain active. */ }
      }
    } catch (failure) {
      if (epoch === this.epoch) await this.disconnect()
      else if (created) {
        try { await this.call('closeBLEConnection', { deviceId }, 3000) } catch (_) { /* Already closed. */ }
      }
      throw failure
    }
  }

  async disconnect() {
    const deviceId = this.deviceId || this.connectingId
    this.clearConnection()
    if (deviceId) {
      try { await this.call('closeBLEConnection', { deviceId }, 3000) } catch (_) { /* Already closed. */ }
    }
  }

  async send(bytes) {
    if (!this.ready || !this.deviceId) throw new Error('Connect to a printer first.')
    if (this.sending) throw new Error('Another Bluetooth write is in progress.')
    this.sending = true
    try {
      const deviceId = this.deviceId
      const maximum = Math.min(512, Math.max(20, this.mtu - 3))
      for (let offset = 0; offset < bytes.length; offset += maximum) {
        if (!this.ready || this.deviceId !== deviceId) throw new Error('Printer disconnected.')
        const value = bytes.slice(offset, offset + maximum).buffer
        await new Promise((resolve, reject) => {
          this.pendingWriteReject = reject
          this.call('writeBLECharacteristicValue', {
            deviceId,
            serviceId: this.serviceId,
            characteristicId: this.characteristicId,
            writeType: 'write',
            value
          }, 8000).then(resolve, reject).finally(() => { this.pendingWriteReject = null })
        })
      }
    } catch (failure) {
      await this.disconnect()
      throw failure
    } finally {
      this.sending = false
    }
  }

  async destroy() {
    this.destroyed = true
    if (this.scanStarting) {
      try { await this.scanStarting } catch (_) { /* Scan was interrupted. */ }
    }
    await this.stopScan()
    await this.disconnect()
    this.api.offBluetoothDeviceFound(this.deviceListener)
    this.api.offBLEConnectionStateChange(this.connectionListener)
    this.api.offBLECharacteristicValueChange(this.valueListener)
    this.api.offBluetoothAdapterStateChange(this.adapterListener)
    if (this.api.offBLEMTUChange) this.api.offBLEMTUChange(this.mtuListener)
    if (this.adapterOpen) {
      try { await this.call('closeBluetoothAdapter', {}, 3000) } catch (_) { /* Already closed. */ }
    }
  }
}

module.exports = BluetoothTransport
