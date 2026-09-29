const { WIDTH, BYTES_PER_LINE } = require('./printer-protocol')

const MAX_LINES = 2048
const BAYER = [
  [0, 8, 2, 10],
  [12, 4, 14, 6],
  [3, 11, 1, 9],
  [15, 7, 13, 5]
]

function photoHeight(width, height) {
  if (width <= 0 || height <= 0) throw new Error('The selected image cannot be printed.')
  return Math.min(MAX_LINES, Math.max(32, Math.round(WIDTH * height / width)))
}

function photoLines(pixels, height) {
  if (!(pixels instanceof Uint8ClampedArray || pixels instanceof Uint8Array) ||
      pixels.length !== WIDTH * height * 4) {
    throw new Error('Invalid photo pixels.')
  }
  const lines = []
  for (let y = 0; y < height; y++) {
    const line = new Uint8Array(BYTES_PER_LINE)
    for (let x = 0; x < WIDTH; x++) {
      const index = (y * WIDTH + x) * 4
      const alpha = pixels[index + 3] / 255
      const gray = ((pixels[index] * 299 + pixels[index + 1] * 587 +
        pixels[index + 2] * 114) / 1000) * alpha + 255 * (1 - alpha)
      if (gray + (BAYER[y & 3][x & 3] - 8) * 8 < 128) {
        line[x >> 3] |= 0x80 >> (x & 7)
      }
    }
    lines.push(line)
  }
  return lines
}

function testPageLines() {
  const lines = []
  for (let y = 0; y < 96; y++) {
    const line = new Uint8Array(BYTES_PER_LINE)
    const dot = (x) => { line[x >> 3] |= 0x80 >> (x & 7) }
    if (y === 4 || y === 91) {
      for (let x = 8; x < WIDTH - 8; x++) dot(x)
    } else if (y > 4 && y < 91) {
      dot(8)
      dot(WIDTH - 9)
    }
    if (y >= 10 && y < 40) {
      const group = Math.floor((y - 10) / 5)
      for (let x = group * 64 + 12; x < group * 64 + 52; x++) {
        if (x & 1) dot(x)
      }
    }
    if (y >= 54 && y < 86) {
      for (let x = 16; x < WIDTH - 16; x++) {
        if ((Math.floor((x - 16) / 8) + Math.floor((y - 54) / 8)) % 2 === 0) dot(x)
      }
    }
    lines.push(line)
  }
  return lines
}

module.exports = { photoHeight, photoLines, testPageLines }
