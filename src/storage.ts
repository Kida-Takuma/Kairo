import "./globals";
import * as sys from "kairo:sys";

const enc = new TextEncoder();
const dec = new TextDecoder();

export const storage = {
  get(key: string): string | null {
    const b = sys.kv_get(key);
    return b ? dec.decode(b) : null;
  },
  set(key: string, value: string): void {
    sys.kv_set(key, enc.encode(value));
  },
  getBytes(key: string): Uint8Array | null {
    return sys.kv_get(key);
  },
  setBytes(key: string, value: Uint8Array): void {
    sys.kv_set(key, value);
  },
  getJSON<T = unknown>(key: string, fallback?: T): T | undefined {
    const s = this.get(key);
    if (s === null) return fallback;
    try {
      return JSON.parse(s) as T;
    } catch {
      return fallback;
    }
  },
  setJSON(key: string, value: unknown): void {
    this.set(key, JSON.stringify(value));
  },
  has(key: string): boolean {
    return sys.kv_get(key) !== null;
  },
  delete(key: string): boolean {
    return sys.kv_delete(key);
  },
  keys(): string[] {
    return sys.kv_keys();
  },
  clear(): void {
    for (const k of sys.kv_keys()) sys.kv_delete(k);
  },
};

export default storage;
