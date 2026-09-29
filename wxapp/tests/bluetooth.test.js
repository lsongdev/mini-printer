const assert = require('node:assert/strict')
const test = require('node:test')
const BluetoothTransport = require('../lib/bluetooth-transport')

test('BLE writes split at MTU minus three and never overlap', async () => {
  const chunks = []
  let active = 0
  let maximumActive = 0
  const api = {
    onBluetoothDeviceFound() {},
    onBLEConnectionStateChange() {},
    onBLECharacteristicValueChange() {},
    onBluetoothAdapterStateChange() {},
    offBluetoothDeviceFound() {},
    offBLEConnectionStateChange() {},
    offBLECharacteristicValueChange() {},
    offBluetoothAdapterStateChange() {},
    writeBLECharacteristicValue({ value, writeType, success }) {
      assert.equal(writeType, 'write')
      active++
      maximumActive = Math.max(maximumActive, active)
      chunks.push(new Uint8Array(value))
      setTimeout(() => { active--; success({}) }, 1)
    }
  }
  const transport = new BluetoothTransport(api, {
    serviceUUID: 'service', characteristicUUID: 'characteristic', namePrefix: 'Mini-Printer'
  })
  transport.ready = true
  transport.deviceId = 'device'
  transport.serviceId = 'service'
  transport.characteristicId = 'characteristic'
  transport.mtu = 23
  await transport.send(Uint8Array.from({ length: 54 }, (_, i) => i))
  assert.deepEqual(chunks.map((chunk) => chunk.length), [20, 20, 14])
  assert.deepEqual([...chunks[0], ...chunks[1], ...chunks[2]], [...Array(54).keys()])
  assert.equal(maximumActive, 1)
})

test('connection verifies the printer service, enables notifications and reads status', async () => {
  const calls = []
  const api = {
    onBluetoothDeviceFound() {},
    onBLEConnectionStateChange() {},
    onBLECharacteristicValueChange() {},
    onBluetoothAdapterStateChange() {},
    offBluetoothDeviceFound() {},
    offBLEConnectionStateChange() {},
    offBLECharacteristicValueChange() {},
    offBluetoothAdapterStateChange() {},
    getDeviceInfo: () => ({ platform: 'ios' }),
    openBluetoothAdapter: ({ success }) => success({}),
    startBluetoothDevicesDiscovery: ({ success }) => success({}),
    stopBluetoothDevicesDiscovery: ({ success }) => success({}),
    createBLEConnection: ({ success }) => success({}),
    getBLEDeviceServices: ({ success }) => success({ services: [{ uuid: 'SERVICE' }] }),
    getBLEDeviceCharacteristics: ({ success }) => success({
      characteristics: [{ uuid: 'CHARACTERISTIC', properties: { write: true, notify: true, read: true } }]
    }),
    getBLEMTU: ({ success }) => success({ mtu: 185 }),
    notifyBLECharacteristicValueChange: ({ type, success }) => {
      assert.equal(type, 'notification')
      calls.push('notify')
      success({})
    },
    readBLECharacteristicValue: ({ success }) => { calls.push('read'); success({}) },
    closeBLEConnection: ({ success }) => success({}),
    closeBluetoothAdapter: ({ success }) => success({})
  }
  const transport = new BluetoothTransport(api, {
    serviceUUID: 'service', characteristicUUID: 'characteristic', namePrefix: 'Mini-Printer'
  })
  await transport.startScan()
  transport.handleDevices({ devices: [{ deviceId: 'device', name: 'Mini-Printer-A7B0', RSSI: -45 }] })
  await transport.connect('device')
  assert.equal(transport.ready, true)
  assert.equal(transport.mtu, 185)
  assert.deepEqual(calls, ['notify', 'read'])
  await transport.destroy()
})

test('a failed acknowledged write closes the stale connection', async () => {
  let closed = false
  const api = {
    onBluetoothDeviceFound() {},
    onBLEConnectionStateChange() {},
    onBLECharacteristicValueChange() {},
    onBluetoothAdapterStateChange() {},
    writeBLECharacteristicValue: ({ fail }) => fail({ errMsg: 'GATT write failed' }),
    closeBLEConnection: ({ success }) => { closed = true; success({}) }
  }
  const transport = new BluetoothTransport(api, {
    serviceUUID: 'service', characteristicUUID: 'characteristic', namePrefix: 'Mini-Printer'
  })
  transport.ready = true
  transport.deviceId = 'device'
  transport.serviceId = 'service'
  transport.characteristicId = 'characteristic'
  await assert.rejects(transport.send(new Uint8Array([1])), /GATT write failed/)
  assert.equal(transport.ready, false)
  assert.equal(closed, true)
})
