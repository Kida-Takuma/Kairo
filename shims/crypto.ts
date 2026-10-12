import * as sys from "kairo:sys";
import { Buffer } from "buffer";
import { toBytes } from "../src/bytes";

type Data = string | Uint8Array | ArrayBuffer | ArrayBufferView;
type Encoding = "hex" | "base64" | "base64url" | "utf8" | "utf-8" | "latin1" | "binary";

function rotl(x: number, n: number): number {
  return (x << n) | (x >>> (32 - n));
}
function rotr(x: number, n: number): number {
  return (x >>> n) | (x << (32 - n));
}

function pad(msg: Uint8Array, bigEndian: boolean): Uint8Array {
  const bitLen = msg.length * 8;
  const total = ((msg.length + 9 + 63) >> 6) << 6;
  const out = new Uint8Array(total);
  out.set(msg);
  out[msg.length] = 0x80;
  const view = new DataView(out.buffer);
  const hi = Math.floor(bitLen / 0x100000000);
  const lo = bitLen >>> 0;
  if (bigEndian) {
    view.setUint32(total - 8, hi);
    view.setUint32(total - 4, lo);
  } else {
    view.setUint32(total - 8, lo, true);
    view.setUint32(total - 4, hi, true);
  }
  return out;
}

const K256 = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74,
  0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d,
  0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e,
  0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
  0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

export function sha256(data: Uint8Array): Uint8Array {
  const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
  const m = pad(data, true);
  const view = new DataView(m.buffer);
  const w = new Uint32Array(64);
  for (let off = 0; off < m.length; off += 64) {
    for (let i = 0; i < 16; i++) w[i] = view.getUint32(off + i * 4);
    for (let i = 16; i < 64; i++) {
      const s0 = rotr(w[i - 15]!, 7) ^ rotr(w[i - 15]!, 18) ^ (w[i - 15]! >>> 3);
      const s1 = rotr(w[i - 2]!, 17) ^ rotr(w[i - 2]!, 19) ^ (w[i - 2]! >>> 10);
      w[i] = (w[i - 16]! + s0 + w[i - 7]! + s1) >>> 0;
    }
    let [a, b, c, d, e, f, g, hh] = h as unknown as [number, number, number, number, number, number, number, number];
    for (let i = 0; i < 64; i++) {
      const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const ch = (e & f) ^ (~e & g);
      const t1 = (hh + S1 + ch + K256[i]! + w[i]!) >>> 0;
      const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const maj = (a & b) ^ (a & c) ^ (b & c);
      const t2 = (S0 + maj) >>> 0;
      hh = g;
      g = f;
      f = e;
      e = (d + t1) >>> 0;
      d = c;
      c = b;
      b = a;
      a = (t1 + t2) >>> 0;
    }
    h[0] = (h[0]! + a) >>> 0;
    h[1] = (h[1]! + b) >>> 0;
    h[2] = (h[2]! + c) >>> 0;
    h[3] = (h[3]! + d) >>> 0;
    h[4] = (h[4]! + e) >>> 0;
    h[5] = (h[5]! + f) >>> 0;
    h[6] = (h[6]! + g) >>> 0;
    h[7] = (h[7]! + hh) >>> 0;
  }
  const out = new Uint8Array(32);
  const ov = new DataView(out.buffer);
  for (let i = 0; i < 8; i++) ov.setUint32(i * 4, h[i]!);
  return out;
}

export function sha1(data: Uint8Array): Uint8Array {
  const h = new Uint32Array([0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0]);
  const m = pad(data, true);
  const view = new DataView(m.buffer);
  const w = new Uint32Array(80);
  for (let off = 0; off < m.length; off += 64) {
    for (let i = 0; i < 16; i++) w[i] = view.getUint32(off + i * 4);
    for (let i = 16; i < 80; i++) w[i] = rotl(w[i - 3]! ^ w[i - 8]! ^ w[i - 14]! ^ w[i - 16]!, 1);
    let [a, b, c, d, e] = h as unknown as [number, number, number, number, number];
    for (let i = 0; i < 80; i++) {
      let f: number, k: number;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5a827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ed9eba1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8f1bbcdc;
      } else {
        f = b ^ c ^ d;
        k = 0xca62c1d6;
      }
      const t = (rotl(a, 5) + f + e + k + w[i]!) >>> 0;
      e = d;
      d = c;
      c = rotl(b, 30);
      b = a;
      a = t;
    }
    h[0] = (h[0]! + a) >>> 0;
    h[1] = (h[1]! + b) >>> 0;
    h[2] = (h[2]! + c) >>> 0;
    h[3] = (h[3]! + d) >>> 0;
    h[4] = (h[4]! + e) >>> 0;
  }
  const out = new Uint8Array(20);
  const ov = new DataView(out.buffer);
  for (let i = 0; i < 5; i++) ov.setUint32(i * 4, h[i]!);
  return out;
}

const MD5_S = [
  7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16,
  23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
];
const MD5_K = new Uint32Array(64).map((_, i) => Math.floor(Math.abs(Math.sin(i + 1)) * 0x100000000));

export function md5(data: Uint8Array): Uint8Array {
  let a0 = 0x67452301,
    b0 = 0xefcdab89,
    c0 = 0x98badcfe,
    d0 = 0x10325476;
  const m = pad(data, false);
  const view = new DataView(m.buffer);
  const w = new Uint32Array(16);
  for (let off = 0; off < m.length; off += 64) {
    for (let i = 0; i < 16; i++) w[i] = view.getUint32(off + i * 4, true);
    let a = a0,
      b = b0,
      c = c0,
      d = d0;
    for (let i = 0; i < 64; i++) {
      let f: number, g: number;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      const t = d;
      d = c;
      c = b;
      b = (b + rotl((a + f + MD5_K[i]! + w[g]!) >>> 0, MD5_S[i]!)) >>> 0;
      a = t;
    }
    a0 = (a0 + a) >>> 0;
    b0 = (b0 + b) >>> 0;
    c0 = (c0 + c) >>> 0;
    d0 = (d0 + d) >>> 0;
  }
  const out = new Uint8Array(16);
  const ov = new DataView(out.buffer);
  ov.setUint32(0, a0, true);
  ov.setUint32(4, b0, true);
  ov.setUint32(8, c0, true);
  ov.setUint32(12, d0, true);
  return out;
}

const ALGOS: Record<string, { fn: (d: Uint8Array) => Uint8Array; block: number; size: number }> = {
  sha256: { fn: sha256, block: 64, size: 32 },
  "sha-256": { fn: sha256, block: 64, size: 32 },
  sha1: { fn: sha1, block: 64, size: 20 },
  "sha-1": { fn: sha1, block: 64, size: 20 },
  md5: { fn: md5, block: 64, size: 16 },
};

function algo(name: string) {
  const a = ALGOS[name.toLowerCase()];
  if (!a) throw new Error(`crypto: unsupported digest '${name}' (available: sha256, sha1, md5)`);
  return a;
}

export function hmac(name: string, key: Uint8Array, data: Uint8Array): Uint8Array {
  const a = algo(name);
  if (key.length > a.block) key = a.fn(key);
  const k = new Uint8Array(a.block);
  k.set(key);
  const ipad = new Uint8Array(a.block + data.length);
  const opad = new Uint8Array(a.block + a.size);
  for (let i = 0; i < a.block; i++) {
    ipad[i] = k[i]! ^ 0x36;
    opad[i] = k[i]! ^ 0x5c;
  }
  ipad.set(data, a.block);
  opad.set(a.fn(ipad), a.block);
  return a.fn(opad);
}

export function toBase64Url(bytes: Uint8Array): string {
  return Buffer.from(bytes).toString("base64").replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export function fromBase64Url(s: string): Uint8Array {
  const b64 = s.replace(/-/g, "+").replace(/_/g, "/") + "=".repeat((4 - (s.length % 4)) % 4);
  return new Uint8Array(Buffer.from(b64, "base64"));
}

function encodeOut(bytes: Uint8Array, enc?: Encoding): Buffer | string {
  if (enc === "base64url") return toBase64Url(bytes);
  const b = Buffer.from(bytes);
  return enc ? b.toString(enc) : b;
}

function inputBytes(data: Data, enc?: Encoding): Uint8Array {
  if (typeof data === "string" && enc === "base64url") return fromBase64Url(data);
  if (typeof data === "string" && enc && enc !== "utf8" && enc !== "utf-8") return new Uint8Array(Buffer.from(data, enc));
  return toBytes(data as Uint8Array);
}

export class Hash {
  private parts: Uint8Array[] = [];
  constructor(private readonly name: string) {
    algo(name);
  }
  update(data: Data, enc?: Encoding): this {
    this.parts.push(inputBytes(data, enc));
    return this;
  }
  digest(): Buffer;
  digest(enc: Encoding): string;
  digest(enc?: Encoding): Buffer | string {
    const total = this.parts.reduce((s, p) => s + p.length, 0);
    const all = new Uint8Array(total);
    let off = 0;
    for (const p of this.parts) {
      all.set(p, off);
      off += p.length;
    }
    return encodeOut(algo(this.name).fn(all), enc);
  }
  copy(): Hash {
    const h = new Hash(this.name);
    h.parts = [...this.parts];
    return h;
  }
}

export class Hmac {
  private parts: Uint8Array[] = [];
  private key: Uint8Array;
  constructor(
    private readonly name: string,
    key: Data,
  ) {
    algo(name);
    this.key = toBytes(key as Uint8Array);
  }
  update(data: Data, enc?: Encoding): this {
    this.parts.push(inputBytes(data, enc));
    return this;
  }
  digest(): Buffer;
  digest(enc: Encoding): string;
  digest(enc?: Encoding): Buffer | string {
    const total = this.parts.reduce((s, p) => s + p.length, 0);
    const all = new Uint8Array(total);
    let off = 0;
    for (const p of this.parts) {
      all.set(p, off);
      off += p.length;
    }
    return encodeOut(hmac(this.name, this.key, all), enc);
  }
}

export function createHash(name: string): Hash {
  return new Hash(name);
}

export function createHmac(name: string, key: Data): Hmac {
  return new Hmac(name, key);
}

export function randomBytes(n: number): Buffer {
  return Buffer.from(sys.random_bytes(n));
}

export function randomInt(min: number, max?: number): number {
  if (max === undefined) {
    max = min;
    min = 0;
  }
  const range = max - min;
  const b = sys.random_bytes(6);
  const r = (b[0]! * 2 ** 40 + b[1]! * 2 ** 32 + b[2]! * 2 ** 24 + b[3]! * 2 ** 16 + b[4]! * 2 ** 8 + b[5]!) / 2 ** 48;
  return min + Math.floor(r * range);
}

export function randomUUID(): string {
  return globalThis.crypto.randomUUID();
}

export function getRandomValues<T extends ArrayBufferView>(arr: T): T {
  return globalThis.crypto.getRandomValues(arr);
}

export function timingSafeEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a[i]! ^ b[i]!;
  return diff === 0;
}

export function getHashes(): string[] {
  return ["sha256", "sha1", "md5"];
}

export interface CryptoKeyLike {
  type: "secret";
  extractable: boolean;
  algorithm: { name: string; hash?: { name: string } };
  usages: string[];
  raw: Uint8Array;
}

function hashName(h: string | { name: string }): string {
  return typeof h === "string" ? h : h.name;
}

function toAb(b: Uint8Array): ArrayBuffer {
  return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength) as ArrayBuffer;
}

export const subtle = {
  async digest(alg: string | { name: string }, data: Data): Promise<ArrayBuffer> {
    return toAb(algo(hashName(alg)).fn(toBytes(data as Uint8Array)));
  },
  async importKey(
    format: string,
    key: Data | { k?: string },
    alg: { name: string; hash?: string | { name: string } },
    extractable: boolean,
    usages: string[],
  ): Promise<CryptoKeyLike> {
    if (alg.name !== "HMAC") throw new Error(`subtle.importKey: only HMAC is supported (got ${alg.name})`);
    let raw: Uint8Array;
    if (format === "raw") raw = toBytes(key as Uint8Array);
    else if (format === "jwk" && typeof (key as { k?: string }).k === "string") raw = fromBase64Url((key as { k: string }).k);
    else throw new Error(`subtle.importKey: unsupported format ${format}`);
    return { type: "secret", extractable, algorithm: { name: "HMAC", hash: { name: hashName(alg.hash ?? "SHA-256") } }, usages, raw };
  },
  async exportKey(format: string, key: CryptoKeyLike): Promise<ArrayBuffer | { kty: string; k: string }> {
    if (format === "raw") return toAb(key.raw);
    if (format === "jwk") return { kty: "oct", k: toBase64Url(key.raw) };
    throw new Error(`subtle.exportKey: unsupported format ${format}`);
  },
  async generateKey(alg: { name: string; hash?: string | { name: string }; length?: number }, extractable: boolean, usages: string[]): Promise<CryptoKeyLike> {
    if (alg.name !== "HMAC") throw new Error("subtle.generateKey: only HMAC is supported");
    const h = hashName(alg.hash ?? "SHA-256");
    const raw = sys.random_bytes(alg.length ? alg.length / 8 : algo(h).block);
    return { type: "secret", extractable, algorithm: { name: "HMAC", hash: { name: h } }, usages, raw };
  },
  async sign(alg: string | { name: string }, key: CryptoKeyLike, data: Data): Promise<ArrayBuffer> {
    if (hashName(alg) !== "HMAC") throw new Error("subtle.sign: only HMAC is supported");
    return toAb(hmac(key.algorithm.hash?.name ?? "SHA-256", key.raw, toBytes(data as Uint8Array)));
  },
  async verify(alg: string | { name: string }, key: CryptoKeyLike, signature: Data, data: Data): Promise<boolean> {
    const expected = new Uint8Array(await this.sign(alg, key, data));
    return timingSafeEqual(expected, toBytes(signature as Uint8Array));
  },
};

export type SubtleCrypto = typeof subtle;

export const webcrypto = { subtle, getRandomValues, randomUUID };

const g = globalThis as unknown as { crypto: { subtle?: unknown } };
if (g.crypto && !g.crypto.subtle) g.crypto.subtle = subtle;

export default {
  createHash,
  createHmac,
  randomBytes,
  randomInt,
  randomUUID,
  getRandomValues,
  timingSafeEqual,
  getHashes,
  subtle,
  webcrypto,
  Hash,
  Hmac,
  toBase64Url,
  fromBase64Url,
};
