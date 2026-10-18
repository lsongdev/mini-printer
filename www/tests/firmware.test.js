import test from 'node:test';
import assert from 'node:assert/strict';
import { verifiedImage, flash } from '../lib/firmware.js';

const bytes = new Uint8Array([0, 1, 255, 32]);
const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), n => n.toString(16).padStart(2, '0')).join('');
const image = { path: 'https://api.github.com/repos/lsongdev/mini-printer/releases/assets/123', size: bytes.length, sha256: hash };
const fetcher = async (url, options) => {
  assert.equal(url, image.path);
  assert.equal(options.headers.Accept, 'application/octet-stream');
  return new Response(bytes);
};

test('release downloads request binary content and verify SHA-256', async () => {
  assert.deepEqual(await verifiedImage(image, { fetcher }), bytes);
});
test('local files use the selected release hash and do not make a network request', async () => {
  const file = new File([bytes], 'firmware.bin');
  assert.deepEqual(await verifiedImage(image, { file, fetcher: () => assert.fail('unexpected network request') }), bytes);
  await assert.rejects(verifiedImage(image, { file: new File([bytes.slice(1)], 'firmware.bin') }), /大小不匹配/);
  await assert.rejects(verifiedImage(image, { file: new File([new Uint8Array(4)], 'firmware.bin') }), /校验失败/);
});
test('wrong hashes, truncation, HTTP errors and untrusted asset URLs abort before flashing', async () => {
  await assert.rejects(verifiedImage({ ...image, sha256: 'a'.repeat(64) }, { fetcher }), /校验失败/);
  await assert.rejects(verifiedImage({ ...image, size: 3 }, { fetcher }), /大小不匹配/);
  await assert.rejects(verifiedImage(image, { fetcher: async () => new Response('', { status: 404 }) }), /下载失败/);
  await assert.rejects(verifiedImage({ ...image, path: 'https://example.com/file.bin' }, { fetcher }), /信息无效/);
});
test('CORS and network failure explain the local-file recovery path', async () => {
  await assert.rejects(verifiedImage(image, { fetcher: async () => { throw new TypeError('Failed to fetch'); } }), /本地固件/);
});

function flasher({ chip = 'ESP32', size = 4 * 1024 * 1024, writeError, resetError } = {}) {
  const calls = [];
  return {
    calls,
    loadFlasher: async () => ({
      Transport: class {
        constructor(port) { calls.push(['port', port]); }
        async disconnect() { calls.push(['disconnect']); }
      },
      ESPLoader: class {
        chip = { CHIP_NAME: chip };
        async main() { calls.push(['main']); }
        async detectFlashSize() { return 'detected'; }
        flashSizeBytes() { return size; }
        async writeFlash(options) {
          calls.push(['write', options]);
          if (writeError) throw writeError;
          options.reportProgress(0, bytes.length, bytes.length);
        }
        async after(mode) {
          calls.push(['reset', mode]);
          if (resetError) throw resetError;
        }
      },
    }),
  };
}
const update = { ...image, address: 0x10000 };
const file = new File([bytes], 'mini-printer-firmware.bin');

test('application update writes verified bytes at 0x10000, resets and releases the port', async () => {
  const mock = flasher();
  const progress = [];
  await flash('usb-port', update, { file, loadFlasher: mock.loadFlasher, progress: value => progress.push(value) });
  const options = mock.calls.find(([name]) => name === 'write')[1];
  assert.deepEqual(options.fileArray, [{ data: bytes, address: 0x10000 }]);
  assert.equal(options.eraseAll, false);
  assert.deepEqual(progress, [100]);
  assert.deepEqual(mock.calls.slice(-2), [['reset', 'hard_reset'], ['disconnect']]);
});
test('complete installation writes the entire 4 MiB image at 0x0', async () => {
  const data = new Uint8Array(4 * 1024 * 1024).fill(255);
  const sha256 = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', data)), n => n.toString(16).padStart(2, '0')).join('');
  const mock = flasher();
  await flash('port', { ...image, address: 0, size: data.length, sha256 }, {
    file: new File([data], 'mini-printer-full.bin'), loadFlasher: mock.loadFlasher,
  });
  const { fileArray, eraseAll } = mock.calls.find(([name]) => name === 'write')[1];
  assert.equal(fileArray[0].address, 0);
  assert.deepEqual(fileArray[0].data, data);
  assert.equal(eraseAll, false);
  assert.deepEqual(mock.calls.at(-1), ['disconnect']);
});
test('chip and flash capacity mismatch prevent writes and release the port', async () => {
  for (const settings of [{ chip: 'ESP32-S3' }, { size: 2 * 1024 * 1024 }]) {
    const mock = flasher(settings);
    await assert.rejects(flash('port', update, { file, loadFlasher: mock.loadFlasher }), /停止刷机/);
    assert.equal(mock.calls.some(([name]) => name === 'write'), false);
    assert.deepEqual(mock.calls.at(-1), ['disconnect']);
  }
});
test('invalid hash or layout prevents opening the port', async () => {
  for (const invalid of [{ ...update, sha256: 'a'.repeat(64) }, { ...update, address: 0 }, { ...update, address: 0x8000 }]) {
    const mock = flasher();
    await assert.rejects(flash('port', invalid, { file, loadFlasher: mock.loadFlasher }), /校验失败|布局/);
    assert.deepEqual(mock.calls, []);
  }
});
test('write failures release the port; reset failures explain manual restart', async () => {
  const mock = flasher({ writeError: new Error('USB disconnected') });
  await assert.rejects(flash('port', update, { file, loadFlasher: mock.loadFlasher }), /USB disconnected/);
  assert.deepEqual(mock.calls.at(-1), ['disconnect']);
  assert.equal(mock.calls.some(([name]) => name === 'reset'), false);
  const reset = flasher({ resetError: new Error('reset failed') });
  const logs = [];
  await flash('port', update, { file, loadFlasher: reset.loadFlasher, log: line => logs.push(line) });
  assert.ok(logs.some(line => line.includes('手动重新开机')));
  assert.deepEqual(reset.calls.at(-1), ['disconnect']);
});
