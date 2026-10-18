import { h, render } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import htm from 'htm';
import { Printer } from './lib/printer.js';
import { textRaster, imageRaster, testRaster, drawRaster } from './lib/raster.js';

const html = htm.bind(h);
const REPO = 'https://github.com/lsongdev/mini-printer';

function PrinterIcon() {
  return html`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><path d="M6 9V3h12v6M6 17H3V9h18v8h-3M6 14h12v8H6zM9 17h6M9 19h4"/><circle cx="18" cy="11.5" r=".6"/></svg>`;
}

function App() {
  const [mode, setMode] = useState('text');
  const [text, setText] = useState('把灵感留在纸上。\n\n今天，也有值得记录的小事。\n\nHello, little printer.');
  const [size, setSize] = useState(24);
  const [align, setAlign] = useState('left');
  const [image, setImage] = useState(null);
  const [imageName, setImageName] = useState('');
  const [imageLoading, setImageLoading] = useState(false);
  const [dither, setDither] = useState(true);
  const [threshold, setThreshold] = useState(150);
  const [density, setDensity] = useState(64);
  const [device, setDevice] = useState('');
  const [connecting, setConnecting] = useState(false);
  const [status, setStatus] = useState(null);
  const [printing, setPrinting] = useState(false);
  const [progress, setProgress] = useState(0);
  const [message, setMessage] = useState('');
  const [failure, setFailure] = useState(false);
  const canvas = useRef(null);
  const imageVersion = useRef(0);
  const printer = useRef(null);
  if (!printer.current) printer.current = new Printer({
    onStatus: setStatus,
    onDisconnect: () => {
      setDevice(''); setStatus(null); setFailure(true);
      setMessage('蓝牙连接已断开，请重新连接打印机。');
    }
  });
  const supported = !!navigator.bluetooth && window.isSecureContext;
  const locked = printing || connecting;

  const raster = useMemo(() => {
    try {
      const lines = mode === 'text' ? textRaster(text, { size, align })
        : mode === 'test' ? testRaster()
        : image ? imageRaster(image, { dither, threshold }) : [];
      return { lines, error: '' };
    } catch (error) { return { lines: [], error: error.message }; }
  }, [mode, text, size, align, image, dither, threshold]);

  useEffect(() => { if (canvas.current) drawRaster(canvas.current, raster.lines); }, [raster]);
  useEffect(() => () => { imageVersion.current++; printer.current.disconnect(); }, []);

  async function connect() {
    setConnecting(true); setMessage(''); setFailure(false);
    try { setDevice(await printer.current.connect()); setMessage('连接成功，可以开始打印了。'); }
    catch (error) {
      setDevice(''); setStatus(null);
      if (error.name === 'NotFoundError') setMessage('未选择设备。开启打印机后可再次连接。');
      else { setFailure(true); setMessage(`连接失败：${error.message}`); }
    } finally { setConnecting(false); }
  }

  function disconnect() {
    printer.current.disconnect(); setDevice(''); setStatus(null);
    setFailure(false); setMessage('已断开打印机。');
  }

  async function chooseImage(event) {
    const file = event.currentTarget.files[0];
    if (!file) return;
    const version = ++imageVersion.current;
    setImage(null); setImageName(''); setMessage('');
    if (!file.type.startsWith('image/') || file.size > 20 * 1024 * 1024) {
      setFailure(true); setMessage('请选择不超过 20 MB 的图片。'); return;
    }
    setImageLoading(true);
    const url = URL.createObjectURL(file);
    try {
      const loaded = new Image(); loaded.src = url;
      await loaded.decode();
      if (version !== imageVersion.current) return;
      setImage(loaded); setImageName(file.name); setFailure(false);
    } catch {
      if (version === imageVersion.current) { setFailure(true); setMessage('无法解码这张图片，请尝试 PNG 或 JPEG。'); }
    } finally {
      URL.revokeObjectURL(url);
      if (version === imageVersion.current) setImageLoading(false);
    }
  }

  async function print() {
    if (printing) return;
    setPrinting(true); setProgress(0); setFailure(false); setMessage('正在准备打印…');
    try {
      await printer.current.print(raster.lines, density, (value) => {
        setProgress(value);
        setMessage(value < 1 ? '正在发送打印内容…' : '内容已发送，等待打印机完成…');
      });
      setMessage('打印完成。把这份小小的灵感带走吧。');
    } catch (error) { setFailure(true); setMessage(error.message); }
    finally { setPrinting(false); }
  }

  return html`<div class="app-shell">
    <header class="site-header">
      <a class="brand" href="#" aria-label="Mini Printer 首页"><${PrinterIcon} /><strong>Mini Printer<span class="brand-dot">.</span></strong></a>
      <nav aria-label="主导航"><a href="#workbench">开始打印</a><a href="#project">关于项目</a><a class="repo-link" href=${REPO} target="_blank" rel="noreferrer">GitHub ↗</a></nav>
    </header>
    <main>
      <section class="hero" aria-labelledby="hero-title">
        <div class="hero-copy"><p class="eyebrow"><span class="live-dot"></span> OPEN HARDWARE · SMALL IDEAS, REAL PAPER</p>
          <h1 id="hero-title">把灵感<br/>留在<span>纸上。</span></h1>
          <p class="hero-description">一台小小的开源热敏打印机。<br/>从一段文字到一张照片，让屏幕里的日常，有了可以触摸的温度。</p>
          <div class="hero-actions"><a class="button button-primary" href="#workbench">试试打印 <span aria-hidden="true">↗</span></a><a class="text-link" href=${REPO + '/tree/main/docs'} target="_blank" rel="noreferrer">探索制作过程 ↗</a></div>
          <p class="hero-note">ESP32 驱动 · 无需墨水 · 浏览器直连</p>
        </div>
        <figure class="hero-visual"><div class="photo-label">THE LITTLE MACHINE BEHIND THE PAPER</div><img src="./assets/printer.png" alt="Mini Printer 的实际内部组装：ESP32 电路板、锂电池与热敏打印机构" width="680" height="680" fetchpriority="high"/><figcaption><span>01 / 从电路开始，向纸张延伸</span><span>MINI PRINTER</span></figcaption></figure>
      </section>
      <div class="spec-strip" aria-label="项目参数"><div><strong>384<span> dots</span></strong><span>细小的点，清晰的表达</span></div><div><strong>ESP32</strong><span>一颗芯片，连接灵感</span></div><div><strong>BLE <span>+ Wi-Fi</span></strong><span>无线连接，轻松打印</span></div><a href=${REPO} target="_blank" rel="noreferrer">从硬件到软件<br/><strong>全部开源 ↗</strong></a></div>

      <section id="workbench" class="workbench" aria-labelledby="workbench-title">
        <div class="section-heading"><div><p class="eyebrow">PLAY WITH PAPER</p><h2 id="workbench-title">你的纸上工作台<span>.</span></h2></div><p>写一点，选一张，然后打印。</p></div>
        <div class="connection-bar"><div class="connection-info"><span class=${`status-dot ${device ? 'connected' : ''}`}></span><div><strong>${device || '还没有连接打印机'}</strong><small>${connecting ? '请在浏览器弹窗中选择 Mini-Printer…' : device ? '已通过蓝牙连接' : '打开打印机，靠近一点，就可以开始。'}</small></div></div><button class=${`button ${device ? 'button-secondary' : 'button-primary'}`} onClick=${device ? disconnect : connect} disabled=${locked || !supported}>${connecting ? '正在连接…' : device ? '断开连接' : '连接打印机 ↗'}</button></div>
        ${!supported && html`<p class="compatibility" role="note">${!window.isSecureContext ? '蓝牙需要 HTTPS 或 localhost 安全环境。' : '当前浏览器不支持 Web Bluetooth。'} 请使用 Android Chrome，或电脑上的 Chrome / Edge。iPhone / iPad 可使用项目中的 iOS 客户端。<a href=${REPO + '/tree/main/ios'} target="_blank" rel="noreferrer">查看客户端 ↗</a></p>`}
        ${status && html`<dl class="printer-status"><div><dt>设备</dt><dd>${status.stateText}</dd></div><div><dt>电量</dt><dd>${status.battery}%</dd></div><div><dt>温度</dt><dd>${status.temperature === null ? '传感器异常' : `${status.temperature} °C`}</dd></div><div><dt>纸张</dt><dd>${status.paperPresent ? '已装纸' : '缺纸'}</dd></div>${status.error > 0 && html`<div class="status-error"><dt>提示</dt><dd>${status.errorText}</dd></div>`}</dl>`}
        <div class="studio-grid">
          <div class="editor-panel">
            <div class="mode-switch" role="group" aria-label="打印内容类型">${[['text', '文字便笺'], ['image', '图片打印'], ['test', '测试页']].map(([value, label]) => html`<button key=${value} aria-pressed=${mode === value} onClick=${() => setMode(value)} disabled=${locked}>${label}</button>`)}</div>
            <fieldset disabled=${locked} class="editor-fields">
              ${mode === 'text' && html`<label class="field-label" for="print-text">写下此刻的想法<span>${text.length} / 2000</span></label><textarea id="print-text" value=${text} maxLength="2000" rows="7" onInput=${(e) => setText(e.currentTarget.value)} placeholder="一份清单、一句喜欢的话，或今天的小小灵感…"></textarea><div class="field-row"><label>字号<select value=${size} onChange=${(e) => setSize(Number(e.currentTarget.value))}><option value="18">小 · 18 px</option><option value="24">标准 · 24 px</option><option value="32">大 · 32 px</option><option value="40">特大 · 40 px</option></select></label><label>对齐<select value=${align} onChange=${(e) => setAlign(e.currentTarget.value)}><option value="left">左对齐</option><option value="center">居中</option><option value="right">右对齐</option></select></label></div>`}
              ${mode === 'image' && html`<div class="image-picker"><span class="upload-mark" aria-hidden="true">＋</span><label for="image-file">选一张喜欢的图片</label><p>PNG、JPEG、WebP 等 · 最大 20 MB</p><input id="image-file" type="file" accept="image/*" onChange=${chooseImage}/><small>${imageLoading ? '正在读取图片…' : imageName || '图片仅在你的浏览器中处理'}</small></div><label class="checkbox-label"><input type="checkbox" checked=${dither} onChange=${(e) => setDither(e.currentTarget.checked)}/>保留照片层次（黑白抖动）</label><label class="range-label" for="threshold">黑白阈值 <output>${threshold}</output></label><input id="threshold" type="range" min="50" max="220" value=${threshold} onInput=${(e) => setThreshold(Number(e.currentTarget.value))}/><p class="field-help">低阈值更浅，高阈值更深。图片保持比例缩放，最长 2048 行。</p>`}
              ${mode === 'test' && html`<div class="test-description"><${PrinterIcon}/><h3>和打印机打个招呼。</h3><p>包含中英文文字与黑白棋盘格，<br/>适合检查连接、装纸和打印效果。</p></div>`}
              <div class="density-control"><label class="range-label" for="density">打印浓度 <output>${density}%</output></label><input id="density" type="range" min="1" max="100" value=${density} onInput=${(e) => setDensity(Number(e.currentTarget.value))}/><div class="range-caption"><span>浅一些</span><span>深一些</span></div></div>
            </fieldset>
            ${raster.error && html`<p class="error" role="alert">${raster.error}</p>`}
            <div class="print-actions"><button class="button button-primary print-button" disabled=${!device || locked || !raster.lines.length || imageLoading || !status?.paperPresent || status?.state === 1 || status?.state === 2} onClick=${print}><${PrinterIcon}/>${printing ? progress < 1 ? `正在发送 ${Math.round(progress * 100)}%` : '等待打印完成…' : '打印这份灵感'}<span aria-hidden="true">→</span></button>${printing && html`<button class="button button-secondary" onClick=${() => { printer.current.cancel(); setMessage('正在取消，请稍候…'); }}>取消</button>`}</div>
            ${printing && html`<progress value=${progress} max="1" aria-label="打印数据发送进度"></progress>`}
            <p class=${`job-message ${failure ? 'error' : ''}`} role="status" aria-live="polite">${message || '内容在本地处理，通过蓝牙直接送到你的打印机。'}</p>
          </div>
          <div class="preview-panel"><div class="preview-heading"><span class="eyebrow">PAPER PREVIEW</span><span>${raster.lines.length ? `384 × ${raster.lines.length} dots` : '等待你的灵感'}</span></div><div class="paper-stage"><div class="paper"><div class="paper-top"><span>MINI PRINTER</span><span>● ● ●</span></div><canvas ref=${canvas} hidden=${!raster.lines.length} role="img" aria-label="将实际发送到打印机的黑白点阵预览"></canvas>${!raster.lines.length && html`<p class="empty-preview">${raster.error ? '调整内容后即可预览' : mode === 'image' ? '选一张图片\n让回忆落在纸上' : '写点什么\n让灵感落在纸上'}</p>`}<div class="paper-bottom">MADE OF LITTLE MOMENTS</div></div></div><p class="preview-note">实际黑白点阵预览 · 打印浓度影响纸面深浅</p></div>
        </div>
        <p class="browser-note">浏览器直连需要 HTTPS 和 Web Bluetooth。<a href="https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API#browser_compatibility" target="_blank" rel="noreferrer">查看浏览器兼容性 ↗</a></p>
      </section>

      <section id="project" class="project-section" aria-labelledby="project-title"><div class="section-heading"><div><p class="eyebrow">BUILT TO BE OPEN</p><h2 id="project-title">小机器，大有可玩<span>.</span></h2></div><p>从第一根连线，到第一张纸。</p></div><div class="project-grid"><article><span class="chapter">01 / HARDWARE</span><h3>亲手做一台。</h3><p>ESP32、384 点热敏打印头、电机与电池。从电路、装配到烧录，跟着硬件笔记，把零件变成一台打印机。</p><a href=${REPO + '/tree/main/docs'} target="_blank" rel="noreferrer">查看硬件与装配笔记 ↗</a></article><article><span class="chapter">02 / FIRMWARE</span><h3>每一个点，都可掌控。</h3><p>固件负责驱动、打印队列和设备状态。统一的 MP 协议可通过 BLE、TCP 或 HTTP 发送，方便编写自己的客户端。</p><a href=${REPO + '/blob/main/docs/protocol.md'} target="_blank" rel="noreferrer">了解打印协议 ↗</a></article><article><span class="chapter">03 / SOFTWARE</span><h3>用你喜欢的方式。</h3><p>在这个网页直接打印，或探索 iOS、Android、微信小程序和 Python 客户端。一次小实验，可以有很多种入口。</p><a href=${REPO} target="_blank" rel="noreferrer">探索完整源代码 ↗</a></article></div></section>
      <section class="faq-section" aria-label="使用帮助"><h2>开打之前。</h2><details><summary>如何连接和打印？</summary><p>打开打印机并装好热敏纸，使用兼容浏览器访问本站，点击“连接打印机”，在系统弹窗选择 Mini-Printer。输入文字或选择图片，确认右侧预览后点击打印。无需配对到系统打印机列表。</p></details><details><summary>为什么手机上无法连接？</summary><p>网页蓝牙取决于浏览器和操作系统支持。推荐 Android Chrome 或电脑 Chrome / Edge。Safari 和 iPhone / iPad 上的常用浏览器不提供此接口，可使用仓库里的 iOS 客户端。微信内置浏览器建议改用系统支持的浏览器打开。</p></details><details><summary>可以连接其他热敏打印机吗？</summary><p>此工作台面向本项目固件，使用 Mini Printer 的自定义 BLE 服务和 MP v1 协议。通用 ESC/POS 或其他厂商打印机不能直接使用。</p></details><details><summary>我的文字和图片会上传吗？</summary><p>文字排版和图片黑白转换都在本地浏览器中完成，打印数据通过 BLE 直接发送到设备。本站不上传打印内容；页面公共样式和 Preact、HTM 模块由外部 CDN 加载。</p></details></section>
    </main>
    <footer class="site-footer"><a class="brand" href="#"><${PrinterIcon}/><strong>Mini Printer.</strong></a><p>A little paper. A little possibility.</p><a href=${REPO} target="_blank" rel="noreferrer">Made by Lsong ↗</a></footer>
  </div>`;
}

const root = document.getElementById('app');
root.replaceChildren();
render(html`<${App}/>`, root);
