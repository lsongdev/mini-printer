const BluetoothTransport = require('../../lib/bluetooth-transport')
const protocol = require('../../lib/printer-protocol')
const rasterizer = require('../../lib/rasterizer')

function statusView(status) {
  return {
    state: status.stateText,
    battery: `${status.battery}%`,
    temperature: status.temperature === null ? 'Unavailable' : `${status.temperature}°C`,
    paper: status.paperPresent ? 'Ready' : 'Out of paper',
    error: status.errorText || ''
  }
}

Page({
  data: {
    devices: [],
    bluetoothAvailable: false,
    scanning: false,
    connectingId: null,
    connectedName: null,
    status: null,
    statusView: null,
    photoPath: null,
    processingPhoto: false,
    density: 64,
    printing: false,
    submittedLines: 0,
    totalLines: 0,
    canPrintPhoto: false,
    canPrintTest: false
  },

  onLoad() {
    this.photoLines = null
    this.lastStatus = null
    this.sawPrinting = false
    this.completion = null
    this.disposed = false
    this.transport = new BluetoothTransport(wx, {
      serviceUUID: protocol.SERVICE_UUID,
      characteristicUUID: protocol.CHARACTERISTIC_UUID,
      namePrefix: 'Mini-Printer',
      onDevices: (devices) => this.setData({ devices }),
      onState: (state) => this.onTransportState(state),
      onValue: (value) => this.onPrinterValue(value),
      onDisconnected: () => this.onPrinterDisconnected()
    })
    this.scan()
  },

  onHide() {
    if (this.transport) this.transport.stopScan()
  },

  onUnload() {
    this.disposed = true
    this.rejectCompletion(new Error('Printer screen closed.'))
    if (this.transport) this.transport.destroy()
  },

  showError(error) {
    if (this.disposed) return
    wx.showModal({
      title: 'Printer Error',
      content: error && error.message || String(error),
      showCancel: false
    })
  },

  updateControls() {
    const available = !!this.transport && this.transport.ready &&
      !this.data.printing && (!this.data.status || this.data.status.paperPresent)
    this.setData({
      canPrintTest: available,
      canPrintPhoto: available && !!this.photoLines && !this.data.processingPhoto
    })
  },

  onTransportState(state) {
    if (this.disposed) return
    if (!state.deviceId) this.lastStatus = null
    this.setData({
      bluetoothAvailable: state.bluetoothAvailable,
      scanning: state.scanning,
      connectingId: state.connectingId,
      connectedName: state.connectedName,
      status: state.deviceId ? this.data.status : null,
      statusView: state.deviceId ? this.data.statusView : null
    })
    this.updateControls()
  },

  onPrinterValue(value) {
    const status = protocol.decodeStatus(value)
    if (!status || this.disposed) return
    this.lastStatus = status
    this.setData({ status, statusView: statusView(status) })
    this.updateControls()
    if (!this.data.printing) return
    if (status.state === 1) this.sawPrinting = true
    if (status.state === 3 || (this.sawPrinting && status.errorCode !== 0)) {
      this.rejectCompletion(new Error(status.errorText || 'Printer stopped unexpectedly.'))
    } else if (this.sawPrinting && status.state === 0) {
      this.resolveCompletion()
    }
  },

  onPrinterDisconnected() {
    this.rejectCompletion(new Error('Printer disconnected.'))
    if (!this.data.printing) this.showError(new Error('Printer disconnected.'))
  },

  async scan() {
    try { await this.transport.startScan() } catch (failure) { this.showError(failure) }
  },

  async connect(event) {
    if (this.data.connectingId) return
    try { await this.transport.connect(event.currentTarget.dataset.id) }
    catch (failure) { this.showError(failure) }
  },

  async disconnect() {
    await this.transport.disconnect()
  },

  changeDensity(event) {
    this.setData({ density: event.detail.value })
  },

  async choosePhoto() {
    if (this.data.processingPhoto || this.data.printing) return
    try {
      const selection = await new Promise((resolve, reject) => {
        wx.chooseMedia({
          count: 1,
          mediaType: ['image'],
          sourceType: ['album'],
          success: resolve,
          fail: reject
        })
      })
      const path = selection.tempFiles[0].tempFilePath
      this.photoLines = null
      this.setData({ photoPath: path, processingPhoto: true })
      this.updateControls()
      const lines = await this.rasterize(path)
      if (this.disposed) return
      this.photoLines = lines
      this.setData({ processingPhoto: false })
      this.updateControls()
    } catch (failure) {
      if (String(failure && (failure.errMsg || failure.message)).toLowerCase().includes('cancel')) return
      this.photoLines = null
      this.setData({ processingPhoto: false })
      this.updateControls()
      this.showError(new Error('The selected photo could not be prepared.'))
    }
  },

  rasterize(path) {
    return new Promise((resolve, reject) => {
      wx.createSelectorQuery().in(this).select('#raster-canvas').fields({ node: true }).exec((found) => {
        const canvas = found && found[0] && found[0].node
        if (!canvas) { reject(new Error('Image canvas is unavailable.')); return }
        const image = canvas.createImage()
        image.onerror = () => reject(new Error('The selected photo could not be opened.'))
        image.onload = () => {
          try {
            const height = rasterizer.photoHeight(image.width, image.height)
            const drawWidth = Math.min(protocol.WIDTH, height * image.width / image.height)
            canvas.width = protocol.WIDTH
            canvas.height = height
            const context = canvas.getContext('2d')
            context.fillStyle = '#fff'
            context.fillRect(0, 0, protocol.WIDTH, height)
            context.drawImage(image, (protocol.WIDTH - drawWidth) / 2, 0, drawWidth, height)
            const pixels = context.getImageData(0, 0, protocol.WIDTH, height).data
            resolve(rasterizer.photoLines(pixels, height))
          } catch (failure) { reject(failure) }
        }
        image.src = path
      })
    })
  },

  resolveCompletion() {
    if (!this.completion) return
    const completion = this.completion
    this.completion = null
    clearTimeout(completion.timer)
    completion.resolve()
  },

  rejectCompletion(failure) {
    if (!this.completion) return
    const completion = this.completion
    this.completion = null
    clearTimeout(completion.timer)
    completion.reject(failure)
  },

  waitForCompletion() {
    if (this.lastStatus && (this.lastStatus.state === 3 ||
        (this.sawPrinting && this.lastStatus.errorCode !== 0))) {
      return Promise.reject(new Error(this.lastStatus.errorText || 'Printer stopped unexpectedly.'))
    }
    if (this.sawPrinting && this.lastStatus && this.lastStatus.state === 0) return Promise.resolve()
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.completion = null
        reject(new Error('The printer did not finish within 90 seconds.'))
      }, 90000)
      this.completion = { resolve, reject, timer }
    })
  },

  printPhoto() {
    if (this.photoLines) this.printJob(this.photoLines)
  },

  printTestPage() {
    this.printJob(rasterizer.testPageLines())
  },

  async printJob(lines) {
    if (this.data.printing) return
    if (!this.transport.ready) { this.showError(new Error('Connect to a printer first.')); return }
    if (this.lastStatus && !this.lastStatus.paperPresent) {
      this.showError(new Error('Load paper before printing.'))
      return
    }
    this.sawPrinting = false
    this.setData({ printing: true, submittedLines: 0, totalLines: lines.length })
    this.updateControls()
    let started = false
    try {
      await this.transport.send(protocol.begin(this.data.density))
      started = true
      for (let start = 0; start < lines.length; start += protocol.LINES_PER_BATCH) {
        await this.transport.send(protocol.lineBatch(lines.slice(start, start + protocol.LINES_PER_BATCH)))
        this.setData({ submittedLines: Math.min(start + protocol.LINES_PER_BATCH, lines.length) })
        if (this.lastStatus && (this.lastStatus.state === 3 ||
            (this.sawPrinting && this.lastStatus.errorCode !== 0))) {
          throw new Error(this.lastStatus.errorText || 'Printer stopped unexpectedly.')
        }
      }
      await this.transport.send(protocol.end)
      await this.waitForCompletion()
    } catch (failure) {
      if (started && this.transport.ready) {
        try { await this.transport.send(protocol.cancel) } catch (_) { /* Connection may be gone. */ }
      }
      this.showError(failure)
    } finally {
      this.setData({ printing: false })
      this.updateControls()
    }
  }
})
