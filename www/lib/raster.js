import { WIDTH, LINE_BYTES } from './protocol.js';

export const MAX_LINES = 2048;
const BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]];

export function packPixels(pixels, height, { dither = false, threshold = 160 } = {}) {
  if (!Number.isInteger(height) || height < 1 || height > MAX_LINES || pixels.length !== WIDTH * height * 4) {
    throw new Error('图像尺寸无效');
  }
  return Array.from({ length: height }, (_, y) => {
    const line = new Uint8Array(LINE_BYTES);
    for (let x = 0; x < WIDTH; x++) {
      const i = (y * WIDTH + x) * 4;
      const alpha = pixels[i + 3] / 255;
      const gray = (pixels[i] * 0.299 + pixels[i + 1] * 0.587 + pixels[i + 2] * 0.114) * alpha + 255 * (1 - alpha);
      if (gray + (dither ? (BAYER[y & 3][x & 3] - 7.5) * 12 : 0) < threshold) {
        line[x >> 3] |= 0x80 >> (x & 7);
      }
    }
    return line;
  });
}

function surface(height) {
  const canvas = document.createElement('canvas');
  canvas.width = WIDTH;
  canvas.height = height;
  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, WIDTH, height);
  return { canvas, ctx };
}

export function textRaster(text, { size = 24, align = 'left' } = {}) {
  if (!text.trim()) return [];
  const font = `${size}px ui-monospace, "PingFang SC", "Microsoft YaHei", monospace`;
  const { canvas, ctx } = surface(1);
  ctx.font = font;
  const wrapped = [];
  for (const paragraph of text.replace(/\r\n?/g, '\n').split('\n')) {
    let current = '';
    for (const character of paragraph) {
      if (ctx.measureText(current + character).width > WIDTH - 32 && current) {
        wrapped.push(current);
        current = '';
      }
      current += character;
    }
    wrapped.push(current);
  }
  const leading = Math.ceil(size * 1.5);
  const height = wrapped.length * leading + 32;
  if (height > MAX_LINES) throw new Error('文字太长，请减少内容（最多 2048 行点阵）');
  canvas.height = height;
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, WIDTH, height);
  ctx.font = font;
  ctx.textBaseline = 'top';
  ctx.textAlign = align;
  ctx.fillStyle = '#000';
  const x = align === 'center' ? WIDTH / 2 : align === 'right' ? WIDTH - 16 : 16;
  wrapped.forEach((line, i) => ctx.fillText(line, x, 16 + i * leading));
  return packPixels(ctx.getImageData(0, 0, WIDTH, height).data, height);
}

export function imageRaster(image, options) {
  const width = image.naturalWidth;
  const originalHeight = image.naturalHeight;
  if (!width || !originalHeight) throw new Error('图片无法读取');
  const scale = Math.min(WIDTH / width, MAX_LINES / originalHeight);
  const height = Math.max(1, Math.round(originalHeight * scale));
  const { ctx } = surface(height);
  const targetWidth = Math.max(1, Math.round(width * scale));
  ctx.drawImage(image, Math.floor((WIDTH - targetWidth) / 2), 0, targetWidth, height);
  return packPixels(ctx.getImageData(0, 0, WIDTH, height).data, height, options);
}

export function testRaster() {
  const lines = textRaster('MINI PRINTER\n\nHello, paper.\n你好，小小打印机。\n\n384 DOTS / ESP32 / BLE', { size: 22, align: 'center' });
  for (let y = 0; y < 64; y++) {
    const line = new Uint8Array(LINE_BYTES);
    for (let x = 16; x < WIDTH - 16; x++) {
      if ((Math.floor(x / 8) + Math.floor(y / 8)) % 2 === 0) line[x >> 3] |= 0x80 >> (x & 7);
    }
    lines.push(line);
  }
  for (let y = 0; y < 16; y++) lines.push(new Uint8Array(LINE_BYTES));
  return lines;
}

export function drawRaster(canvas, lines) {
  canvas.width = WIDTH;
  canvas.height = Math.max(1, lines.length);
  const ctx = canvas.getContext('2d');
  const pixels = ctx.createImageData(WIDTH, canvas.height);
  pixels.data.fill(255);
  lines.forEach((line, y) => {
    for (let x = 0; x < WIDTH; x++) {
      if (line[x >> 3] & (0x80 >> (x & 7))) {
        const index = (y * WIDTH + x) * 4;
        pixels.data[index] = pixels.data[index + 1] = pixels.data[index + 2] = 0;
      }
    }
  });
  ctx.putImageData(pixels, 0, 0);
}
