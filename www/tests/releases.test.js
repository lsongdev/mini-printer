import test from 'node:test';
import assert from 'node:assert/strict';
import { parseReleases, fetchReleases } from '../lib/releases.js';

const asset = { id: 123, name: 'mini-printer-firmware.bin', size: 1024, state: 'uploaded', digest: 'sha256:' + 'a'.repeat(64) };
const release = { tag_name: 'daily-test', prerelease: true, draft: false, assets: [asset] };

test('release discovery includes daily prereleases and uses Mini Printer assets and GitHub digests', () => {
  const list = parseReleases([release]);
  assert.equal(list.length, 1); assert.equal(list[0].prerelease, true);
  const image = list[0].images.update;
  assert.equal(image.sha256, 'a'.repeat(64)); assert.equal(image.address, 0x10000);
  assert.equal(image.path, 'https://api.github.com/repos/lsongdev/mini-printer/releases/assets/123');
  assert.match(image.download, /daily-test\/mini-printer-firmware.bin$/);
  assert.equal(list[0].images.full, undefined);
});
test('draft, missing hashes, unknown assets and invalid image sizes do not become flash options', () => {
  for (const fixture of [
    { ...release, draft: true },
    { ...release, assets: [{ ...asset, digest: null }] },
    { ...release, assets: [{ ...asset, name: 'unknown.bin' }] },
    { ...release, assets: [{ ...asset, size: 17 * 1024 * 1024 }] },
    { ...release, assets: [{ ...asset, name: 'mini-printer-full.bin', size: 1024 }] },
  ]) assert.deepEqual(parseReleases([fixture]), []);
});
test('release requests use the public API and explain rate limits', async () => {
  const list = await fetchReleases(async (url, options) => {
    assert.match(url, /^https:\/\/api.github.com\/repos\/lsongdev\/mini-printer\/releases\?/);
    assert.equal(options.headers.Authorization, undefined);
    return Response.json([release]);
  });
  assert.equal(list[0].tag, 'daily-test');
  await assert.rejects(fetchReleases(async () => new Response('', { status: 403 })), /请求受限/);
  await assert.rejects(fetchReleases(async () => Response.json({ message: 'invalid' })), /无效的版本列表/);
});

test('full images use 0x0 and updates cannot exceed the min_spiffs app slot', () => {
  const full = { ...asset, id: 456, name: 'mini-printer-full.bin', size: 4 * 1024 * 1024 };
  const [parsed] = parseReleases([{ ...release, assets: [asset, full] }]);
  assert.equal(parsed.images.full.address, 0);
  assert.equal(parsed.images.full.size, 4 * 1024 * 1024);
  assert.equal(parsed.images.update.address, 0x10000);
  assert.deepEqual(parseReleases([{ ...release, assets: [{ ...asset, size: 0x1e0001 }] }]), []);
  assert.deepEqual(parseReleases([null, {}, { ...release, assets: null }]), []);
});
