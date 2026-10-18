import { h } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import htm from 'htm';
import { fetchReleases } from './lib/releases.js';
import { flash } from './lib/firmware.js';

const html = htm.bind(h);
const REPO = 'https://github.com/lsongdev/mini-printer';

export function FirmwarePanel({ blocked, onBusy }) {
  const [releases, setReleases] = useState([]);
  const [releaseId, setReleaseId] = useState('');
  const [loading, setLoading] = useState(true);
  const [releaseError, setReleaseError] = useState('');
  const [install, setInstall] = useState('full');
  const [acknowledged, setAcknowledged] = useState(false);
  const [localFile, setLocalFile] = useState(null);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const [failure, setFailure] = useState(false);
  const [logs, setLogs] = useState([]);
  const [progress, setProgress] = useState(0);
  const lock = useRef(false);
  const fileInput = useRef(null);
  const supported = !!navigator.serial && window.isSecureContext;
  const release = releases.find(item => item.tag === releaseId);
  const image = release?.images[install];

  async function loadReleases() {
    setLoading(true); setReleaseError('');
    try {
      const data = await fetchReleases();
      setReleases(data); setReleaseId(data[0]?.tag || '');
    } catch (error) { setReleaseError(error.message || '版本加载失败，请稍后重试。'); }
    finally { setLoading(false); }
  }
  useEffect(() => { loadReleases(); }, []);
  useEffect(() => {
    setLocalFile(null); setAcknowledged(false); setMessage(''); setLogs([]); setProgress(0);
    if (fileInput.current) fileInput.current.value = '';
  }, [releaseId, install]);
  useEffect(() => {
    const beforeUnload = event => {
      if (lock.current) { event.preventDefault(); event.returnValue = ''; }
    };
    window.addEventListener('beforeunload', beforeUnload);
    return () => window.removeEventListener('beforeunload', beforeUnload);
  }, []);

  async function startFlash() {
    if (lock.current || blocked || !supported || !image || !acknowledged) return;
    lock.current = true; setBusy(true); onBusy(true);
    setFailure(false); setMessage('请选择打印机的 USB 串口…'); setLogs([]); setProgress(0);
    try {
      // Request access during the click gesture, before imports or downloads.
      const port = await navigator.serial.requestPort();
      setMessage('正在刷机，请勿拔线或关闭页面…');
      await flash(port, image, {
        file: localFile, progress: setProgress,
        log: line => setLogs(lines => [...lines.slice(-79), String(line)]),
      });
      setProgress(100); setMessage('固件写入完成。等待打印机启动后，可在下方连接蓝牙打印；完整安装后需重新配置 Wi-Fi。');
    } catch (error) {
      if (error.name === 'NotFoundError') setMessage('未选择串口，可以重新连接。');
      else { setFailure(true); setMessage(`刷机失败：${error.message || String(error)}`); }
    } finally { lock.current = false; setBusy(false); onBusy(false); }
  }

  return html`<section id="install" class="firmware-section" aria-labelledby="firmware-title">
    <div class="section-heading"><div><p class="eyebrow">FIRMWARE / USB INSTALL</p><h2 id="firmware-title">给小机器，新的灵感<span>.</span></h2></div><p>一根 USB 数据线，直接在浏览器里安装固件。</p></div>
    ${!supported && html`<p class="firmware-notice" role="note">USB 刷机需要 HTTPS 或 localhost，以及支持 Web Serial 的电脑 Chrome / Edge。当前浏览器仍可查看版本和下载固件。</p>`}
    <div class="firmware-grid"><div class="firmware-panel">
      <p class="firmware-target">Mini Printer V4 · ESP32 · 4 MB Flash</p>
      <fieldset class="firmware-fields" disabled=${busy || blocked}>
        <label for="firmware-release">固件版本</label>
        <select id="firmware-release" value=${releaseId} disabled=${loading || !releases.length} onChange=${event => setReleaseId(event.currentTarget.value)}>
          ${!releases.length && html`<option value="">${loading ? '正在加载版本…' : '暂无可用版本'}</option>`}
          ${releases.map(item => html`<option key=${item.tag} value=${item.tag}>${item.tag}${item.prerelease ? ' · 预发布' : ''}</option>`)}
        </select>
        ${releaseError && html`<p class="error" role="alert">${releaseError} <button class="firmware-retry" onClick=${loadReleases}>重新加载</button></p>`}
        ${!loading && !releaseError && !releases.length && html`<p class="field-help">暂无带 SHA-256 校验值的可用固件。发布构建完成后，可重新加载版本列表。</p><button class="firmware-retry" onClick=${loadReleases}>重新加载</button>`}
        <div class="firmware-choices" role="group" aria-label="安装方式">
          <label><input type="radio" name="firmware-install" checked=${install === 'full'} onChange=${() => setInstall('full')}/><span><strong>完整安装</strong><small>首次安装、恢复设备或更换分区布局；重置所有设置和数据。</small></span></label>
          <label><input type="radio" name="firmware-install" checked=${install === 'update'} onChange=${() => setInstall('update')}/><span><strong>更新固件</strong><small>已使用本项目 min_spiffs 分区布局；保留兼容的设置。</small></span></label>
        </div>
        ${release && !image && html`<p class="field-help">此版本没有${install === 'full' ? '完整镜像' : '应用固件'}，请选择其他版本或安装方式。</p>`}
        <label class="firmware-ack"><input type="checkbox" checked=${acknowledged} onChange=${event => setAcknowledged(event.currentTarget.checked)}/><span>${install === 'full' ? '我确认设备为 Mini Printer V4，并了解完整安装会清除 Wi-Fi 配置及其他数据。' : '我确认设备已使用本项目的 min_spiffs 分区布局。'}</span></label>
        <div class="firmware-actions"><button class="button button-primary" disabled=${!supported || !image || loading || !acknowledged} onClick=${startFlash}>${busy ? '正在刷机…' : '连接 USB 并' + (install === 'full' ? '安装固件' : '更新固件') + ' ↧'}</button>${image && html`<a href=${image.download} target="_blank" rel="noreferrer">下载 .bin ↗</a>`}</div>
        ${image && html`<label class="firmware-local">本地固件（可选）<input ref=${fileInput} type="file" accept=".bin,application/octet-stream" onChange=${event => setLocalFile(event.currentTarget.files[0] || null)}/><small>自动下载受限时，先下载上方 .bin 再选择文件。文件须与当前版本和安装方式匹配，写入前会校验 SHA-256。</small></label>`}
      </fieldset>
      ${blocked && html`<p class="field-help">请先完成打印并断开蓝牙连接，再开始 USB 刷机。</p>`}
      <p class="firmware-meta"><a href=${release?.url || REPO + '/releases'} target="_blank" rel="noreferrer">${release?.tag || 'GitHub Releases'} ↗</a>${image && html`<span> · SHA-256 校验 · ${(image.size / 1024 / 1024).toFixed(2)} MiB</span>`}</p>
      <p class=${'job-message ' + (failure ? 'error' : '')} role="status" aria-live="polite">${message}</p>
      ${logs.length > 0 && html`<div class="firmware-progress"><progress max="100" value=${progress} aria-label="固件写入进度"></progress><span>${progress}%</span><details open=${busy || failure}><summary>刷机日志</summary><pre>${logs.join('\n')}</pre></details></div>`}
    </div><aside class="firmware-guide"><p class="eyebrow">BEFORE YOU CONNECT</p><h3>准备好，就连接。</h3><ol>
      <li><strong>确认硬件</strong><p>当前固件面向原版 V4 打印机接线，芯片为经典 ESP32。刷机工具会检查芯片型号和 Flash 容量。</p></li>
      <li><strong>插上 USB 数据线</strong><p>关闭占用串口的监视器。若连接失败，按住 BOOT，短按 EN / RESET，再松开 BOOT 后重试。</p></li>
      <li><strong>选择版本和安装方式</strong><p>首次使用请选择完整安装。日常更新需匹配现有分区布局，写入期间请勿拔线。</p></li>
      <li><strong>启动后，试试打印</strong><p>等待设备启动，在下方连接蓝牙。完整安装后，可连接 Mini-Printer-XXXX 热点配置 Wi-Fi。</p></li>
    </ol><a href=${REPO + '/blob/main/firmware/README.md'} target="_blank" rel="noreferrer">查看固件与硬件说明 ↗</a></aside></div>
  </section>`;
}
