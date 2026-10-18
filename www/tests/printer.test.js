import test from 'node:test';
import assert from 'node:assert/strict';
import { frame, begin, decodeStatus, LINE_BYTES, WIDTH } from '../lib/protocol.js';
import { packPixels } from '../lib/raster.js';
import { BLETransport } from '../lib/ble.js';
import { Printer } from '../lib/printer.js';

function statusFrame(state = 0, error = 0, paper = 0) {
  return Uint8Array.from([0x4d, 0x50, 1, 0x80, 5, 0, 82, 30, paper, state, error]);
}

test('MP frames match the firmware specification and validate input', () => {
  assert.deepEqual([...begin(64)], [77, 80, 1, 1, 1, 0, 64]);
  assert.deepEqual([...frame(3)], [77, 80, 1, 3, 0, 0]);
  assert.equal(frame(2, new Uint8Array(LINE_BYTES)).length, 54);
  assert.throws(() => frame(2, new Uint8Array(47)));
  assert.throws(() => frame(0x80));
  assert.throws(() => begin(0));
  assert.throws(() => begin(101));
});

test('decode status respects DataView offsets and rejects malformed frames', () => {
  const buffer = new Uint8Array(20);
  buffer.set(statusFrame(1), 3);
  const status = decodeStatus(new DataView(buffer.buffer, 3, 11));
  assert.equal(status.battery, 82);
  assert.equal(status.state, 1);
  assert.equal(status.paperPresent, true);
  assert.equal(status.errorText, '');
  const invalid = statusFrame(); invalid[4] = 6;
  assert.equal(decodeStatus(invalid), null);
  assert.equal(decodeStatus(new Uint8Array(5)), null);
  const sensor = statusFrame(); sensor[7] = 255;
  assert.equal(decodeStatus(sensor).temperature, null);
});

test('raster packs MSB first and composites transparent pixels onto white', () => {
  const pixels = new Uint8ClampedArray(WIDTH * 4).fill(255);
  pixels.set([0, 0, 0, 255], 0);
  pixels.set([0, 0, 0, 255], 7 * 4);
  pixels.set([0, 0, 0, 0], 8 * 4);
  const [line] = packPixels(pixels, 1);
  assert.equal(line.length, 48);
  assert.equal(line[0], 0x81);
  assert.equal(line[1], 0);
  assert.throws(() => packPixels(pixels, 2049));
});

function mockBLE(failAt = -1) {
  const chunks = [];
  let disconnected = false;
  const transport = new BLETransport();
  transport.device = {
    gatt: { connected: true, disconnect() { disconnected = true; this.connected = false; } },
    removeEventListener() {}
  };
  transport.characteristic = {
    removeEventListener() {},
    async writeValueWithResponse(bytes) {
      if (chunks.length === failAt) throw new Error('radio failure');
      chunks.push([...bytes]);
      await Promise.resolve();
    }
  };
  return { transport, chunks, disconnected: () => disconnected };
}

test('BLE serializes complete frames using acknowledged MTU-safe fragments', async () => {
  const { transport, chunks } = mockBLE();
  const line = frame(2, new Uint8Array(48).fill(0xaa));
  await Promise.all([transport.write(line), transport.write(frame(4))]);
  assert.deepEqual(chunks.map((chunk) => chunk.length), [20, 20, 14, 6]);
  assert.deepEqual(chunks.flat(), [...line, ...frame(4)]);
});

test('failed partial BLE write disconnects and prevents queued CANCEL corruption', async () => {
  const { transport, chunks, disconnected } = mockBLE(1);
  const results = await Promise.allSettled([
    transport.write(frame(2, new Uint8Array(48))), transport.write(frame(4))
  ]);
  assert.equal(results[0].status, 'rejected');
  assert.equal(results[1].status, 'rejected');
  assert.equal(disconnected(), true);
  assert.equal(chunks.length, 1);
});

function mockPrinter({ paper = 0, fault = 0, failLine = false } = {}) {
  const commands = [];
  let state = 0;
  let error = 0;
  let finishing = false;
  let idleReads = 0;
  let printer;
  const transport = {
    connected: true,
    async write(bytes) {
      const type = bytes[3]; commands.push(type);
      if (type === 1) { state = 1; error = 0; }
      if (type === 2) {
        if (failLine) throw new Error('write failed');
        if (fault) { state = 3; error = fault; printer.receive(statusFrame(state, error, paper)); }
      }
      if (type === 3) finishing = true;
      if (type === 4) { state = 0; error = 5; }
    },
    async read() {
      // Keep reporting printing once after END; completion must await idle.
      if (finishing && ++idleReads >= 2) state = 0;
      printer.receive(statusFrame(state, error, paper));
    }
  };
  printer = new Printer({ transport });
  return { printer, transport, commands, idleReads: () => idleReads };
}
const lines = () => [new Uint8Array(48), new Uint8Array(48)];

test('job confirms printing and waits for fresh physical completion after END', async () => {
  const { printer, commands, idleReads } = mockPrinter();
  const progress = [];
  await printer.print(lines(), 64, (value) => progress.push(value));
  assert.deepEqual(commands.filter((type) => type !== 16), [1, 2, 2, 3]);
  assert.equal(idleReads(), 2);
  assert.deepEqual(progress, [0.5, 1]);
  assert.equal(printer.busy, false);
});

test('out of paper rejects before BEGIN', async () => {
  const { printer, commands } = mockPrinter({ paper: 1 });
  await assert.rejects(printer.print(lines(), 64), /缺纸/);
  assert.equal(commands.includes(1), false);
});

test('printer hardware error stops data and cancels the active job', async () => {
  const { printer, commands } = mockPrinter({ fault: 2 });
  await assert.rejects(printer.print(lines(), 64), /过热/);
  assert.equal(commands.filter((type) => type === 2).length, 1);
  assert.equal(commands.at(-1), 4);
  assert.equal(commands.includes(3), false);
});

test('cancel happens between full frames, and a new job clears cancellation', async () => {
  const { printer, commands } = mockPrinter();
  await assert.rejects(printer.print(lines(), 64, () => printer.cancel()), /取消/);
  assert.equal(commands.at(-1), 4);
  assert.equal(commands.filter((type) => type === 2).length, 1);
  await printer.print(lines(), 64);
  assert.equal(printer.busy, false);
});

test('simultaneous jobs are rejected and disconnection interrupts printing', async () => {
  const { printer, transport } = mockPrinter();
  const job = printer.print(lines(), 64, () => { transport.connected = false; });
  await assert.rejects(printer.print(lines(), 64), /已有打印任务/);
  await assert.rejects(job, /断开/);
  assert.equal(printer.busy, false);
});
