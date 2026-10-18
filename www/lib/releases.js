const API = 'https://api.github.com/repos/lsongdev/mini-printer';
export const FLASH_SIZE = 4 * 1024 * 1024;
export const APP_SIZE = 0x1e0000; // min_spiffs app0 partition

export function parseReleases(data) {
  if (!Array.isArray(data)) throw new Error('GitHub 返回了无效的版本列表。');
  return data.filter(release => release && !release.draft && typeof release.tag_name === 'string').map(release => {
    const images = {};
    const assets = Array.isArray(release.assets) ? release.assets : [];
    for (const [kind, suffix, address] of [['update', 'firmware', 0x10000], ['full', 'full', 0]]) {
      const asset = assets.find(asset => asset?.name === `mini-printer-${suffix}.bin` && asset.state === 'uploaded');
      const sha256 = /^sha256:([a-f0-9]{64})$/.exec(asset?.digest)?.[1];
      if (!asset || !sha256 || !Number.isSafeInteger(asset.id) || asset.id <= 0 || !Number.isSafeInteger(asset.size)) continue;
      if (kind === 'full' ? asset.size !== FLASH_SIZE : asset.size <= 0 || asset.size > APP_SIZE) continue;
      images[kind] = {
        name: asset.name, path: `${API}/releases/assets/${asset.id}`,
        download: `https://github.com/lsongdev/mini-printer/releases/download/${encodeURIComponent(release.tag_name)}/${asset.name}`,
        size: asset.size, sha256, address,
      };
    }
    return {
      tag: release.tag_name, url: `https://github.com/lsongdev/mini-printer/releases/tag/${encodeURIComponent(release.tag_name)}`,
      prerelease: release.prerelease, images,
    };
  }).filter(release => Object.keys(release.images).length);
}

export async function fetchReleases(fetcher = fetch) {
  const response = await fetcher(`${API}/releases?per_page=100`, {
    headers: { Accept: 'application/vnd.github+json' }, signal: AbortSignal.timeout(20000),
  });
  if (!response.ok) {
    if ([403, 429].includes(response.status)) throw new Error('GitHub API 请求受限，请稍后重试，或直接前往 Releases 下载固件。');
    throw new Error(`版本列表加载失败 (${response.status})。`);
  }
  return parseReleases(await response.json());
}
