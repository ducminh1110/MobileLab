import { deflateSync } from "node:zlib";

let crcTable: Uint32Array | undefined;

function crc32(buffer: Buffer): number {
  if (!crcTable) {
    crcTable = new Uint32Array(256);
    for (let n = 0; n < 256; n += 1) {
      let c = n;
      for (let k = 0; k < 8; k += 1) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crcTable[n] = c >>> 0;
    }
  }
  let crc = 0xffffffff;
  for (const byte of buffer) crc = crcTable[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type: string, data: Buffer): Buffer {
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([length, body, crc]);
}

function hslToRgb(h: number, s: number, l: number): [number, number, number] {
  const a = s * Math.min(l, 1 - l);
  const f = (n: number) => {
    const k = (n + h / 30) % 12;
    return l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1));
  };
  return [Math.round(f(0) * 255), Math.round(f(8) * 255), Math.round(f(4) * 255)];
}

function hash(text: string): number {
  let h = 2166136261;
  for (let i = 0; i < text.length; i += 1) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}

/**
 * Draws a recognisable phone-shaped placeholder (no external image dependencies). Used by the demo
 * mode only; real devices return real `simctl io screenshot` output.
 */
export function phonePlaceholderPng(seed: string, width = 240, height = 520): Buffer {
  const hue = hash(seed) % 360;
  const [r1, g1, b1] = hslToRgb(hue, 0.55, 0.28);
  const [r2, g2, b2] = hslToRgb((hue + 40) % 360, 0.6, 0.12);
  const pixels = Buffer.alloc(width * height * 3);

  const set = (x: number, y: number, r: number, g: number, b: number) => {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const i = (y * width + x) * 3;
    pixels[i] = r;
    pixels[i + 1] = g;
    pixels[i + 2] = b;
  };
  const rect = (x0: number, y0: number, w: number, h: number, rgb: [number, number, number], radius = 0) => {
    for (let y = y0; y < y0 + h; y += 1) {
      for (let x = x0; x < x0 + w; x += 1) {
        if (radius > 0) {
          const dx = Math.max(x0 + radius - x, 0, x - (x0 + w - 1 - radius));
          const dy = Math.max(y0 + radius - y, 0, y - (y0 + h - 1 - radius));
          if (dx * dx + dy * dy > radius * radius) continue;
        }
        set(x, y, rgb[0], rgb[1], rgb[2]);
      }
    }
  };

  for (let y = 0; y < height; y += 1) {
    const t = y / (height - 1);
    const r = Math.round(r1 + (r2 - r1) * t);
    const g = Math.round(g1 + (g2 - g1) * t);
    const b = Math.round(b1 + (b2 - b1) * t);
    for (let x = 0; x < width; x += 1) set(x, y, r, g, b);
  }

  rect(Math.round(width / 2 - 34), 12, 68, 20, [8, 8, 10], 10); // dynamic island
  const card: [number, number, number] = [255, 255, 255];
  for (let i = 0; i < 5; i += 1) {
    const y = 80 + i * 66;
    rect(18, y, width - 36, 52, [Math.min(card[0], 235), Math.min(card[1], 238), 245], 12);
    rect(30, y + 12, 28, 28, hslToRgb((hue + i * 47) % 360, 0.5, 0.5), 8);
    rect(70, y + 14, 90 + ((i * 23) % 40), 8, [90, 96, 110], 4);
    rect(70, y + 30, 60 + ((i * 31) % 50), 6, [160, 165, 175], 3);
  }
  rect(Math.round(width / 2 - 40), height - 14, 80, 5, [235, 235, 240], 2); // home indicator

  const stride = width * 3;
  const raw = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; y += 1) {
    raw[y * (stride + 1)] = 0;
    pixels.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
  }

  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8; // bit depth
  header[9] = 2; // colour type: RGB

  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", header),
    chunk("IDAT", deflateSync(raw)),
    chunk("IEND", Buffer.alloc(0))
  ]);
}
