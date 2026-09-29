const assert = require('node:assert/strict')
const test = require('node:test')
const protocol = require('../lib/printer-protocol')
const rasterizer = require('../lib/rasterizer')

test('MP v1 frames match the documented wire format', () => {
  assert.deepEqual([...protocol.begin(64)], [0x4d, 0x50, 1, 1, 1, 0, 64])
  assert.deepEqual([...protocol.end], [0x4d, 0x50, 1, 3, 0, 0])
  const first = new Uint8Array(48).fill(0x55)
  const second = new Uint8Array(48).fill(0xaa)
  const batch = protocol.lineBatch([first, second])
  assert.equal(batch.length, 108)
  assert.deepEqual(batch.slice(0, 54), protocol.line(first))
  assert.deepEqual(batch.slice(54), protocol.line(second))
})

test('status parser validates shape, paper, battery and temperature', () => {
  const frame = Uint8Array.from([0x4d, 0x50, 1, 0x80, 5, 0, 73, 255, 1, 3, 1])
  const status = protocol.decodeStatus(frame.buffer)
  assert.equal(status.battery, 73)
  assert.equal(status.temperature, null)
  assert.equal(status.paperPresent, false)
  assert.equal(status.stateText, 'Needs attention')
  assert.equal(status.errorText, 'Out of paper')
  assert.equal(protocol.decodeStatus(frame.slice(0, 10)), null)
  frame[6] = 101
  assert.equal(protocol.decodeStatus(frame), null)
})

test('photo raster uses 384 dots, MSB-first pixels and white transparency', () => {
  const pixels = new Uint8ClampedArray(protocol.WIDTH * 4)
  pixels.fill(255)
  pixels.set([0, 0, 0, 255], 0)
  pixels.set([0, 0, 0, 0], 4)
  const lines = rasterizer.photoLines(pixels, 1)
  assert.equal(lines.length, 1)
  assert.equal(lines[0].length, 48)
  assert.equal(lines[0][0], 0x80)
  assert.equal(rasterizer.photoHeight(100, 100), 384)
  assert.equal(rasterizer.photoHeight(100, 1000), 2048)
})

test('test page stays within paper width and has 96 lines', () => {
  const lines = rasterizer.testPageLines()
  assert.equal(lines.length, 96)
  assert.ok(lines.every((line) => line.length === 48))
  assert.ok(lines[4].some((byte) => byte !== 0))
  assert.ok(lines[91].some((byte) => byte !== 0))
})
