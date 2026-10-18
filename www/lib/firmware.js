import { FLASH_SIZE, APP_SIZE } from './releases.js';

const loadTool = () => import('https://unpkg.com/esptool-js@0.7.0/bundle.js');

export async function verifiedImage(image, { file, fetcher = fetch } = {}) {
  if (!/^https:\/\/api\.github\.com\/repos\/lsongdev\/mini-printer\/releases\/assets\/\d+$/.test(image?.path) || !/^[a-f0-9]{64}$/.test(image.sha256) || !Number.isSafeInteger(image.size) || image.size <= 0 || image.size > FLASH_SIZE) throw new Error('固件信息无效。');
  let bytes;
  if (file) {
    if (file.size !== image.size) throw new Error('固件文件大小不匹配，请选择当前版本对应的 .bin 文件。');
    bytes = new Uint8Array(await file.arrayBuffer());
  } else {
    let response;
    try {
      response = await fetcher(image.path, { headers: { Accept: 'application/octet-stream' }, signal: AbortSignal.timeout(120000) });
    } catch {
      throw new Error('GitHub 固件下载被浏览器或网络阻止。请点击「下载 .bin」，在「本地固件」中选择文件，再重新刷机。');
    }
    if (!response.ok) throw new Error(`固件下载失败 (${response.status})，请下载后选择本地文件。`);
    bytes = new Uint8Array(await response.arrayBuffer());
  }
  if (bytes.length !== image.size) throw new Error('固件文件大小不匹配，请刷新页面重试。');
  const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), n => n.toString(16).padStart(2, '0')).join('');
  if (hash !== image.sha256) throw new Error('固件 SHA-256 校验失败，已停止刷机。');
  return bytes;
}

export async function flash(port, image, { log = () => {}, progress = () => {}, file, loadFlasher = loadTool } = {}) {
  let transport;
  try {
    log('加载刷机工具…');
    const { ESPLoader, Transport } = await loadFlasher();
    log(file ? '正在校验本地固件…' : '正在下载并校验固件…');
    const data = await verifiedImage(image, { file });
    if (![0, 0x10000].includes(image.address) || (image.address === 0 ? data.length !== FLASH_SIZE : data.length > APP_SIZE)) throw new Error('固件布局与所选设备不符。');
    log('SHA-256 校验通过。正在连接引导程序…');
    transport = new Transport(port, true);
    const loader = new ESPLoader({ transport, baudrate: 115200, terminal: { clean() {}, write: log, writeLine: log } });
    await loader.main();
    if (loader.chip.CHIP_NAME !== 'ESP32') throw new Error(`检测到 ${loader.chip.CHIP_NAME}，Mini Printer V4 需要 ESP32，已停止刷机。`);
    const flashSize = await loader.detectFlashSize();
    if (!flashSize || loader.flashSizeBytes(flashSize) < FLASH_SIZE) throw new Error('设备 Flash 容量与所选型号不匹配，已停止刷机。');
    log('设备型号检查通过，开始写入。请勿断开 USB。');
    await loader.writeFlash({
      fileArray: [{ data, address: image.address }], flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
      eraseAll: false, compress: true,
      reportProgress: (_index, written, total) => progress(Math.floor(written / total * 100)),
    });
    log('固件写入完成，正在重启设备…');
    try { await loader.after('hard_reset'); }
    catch { log('自动重启未完成。请手动重新开机后连接打印机。'); }
  } finally {
    if (transport) {
      try { await transport.disconnect(); }
      catch { log('USB 串口已断开；再次连接前可重新插拔设备。'); }
    }
  }
}
