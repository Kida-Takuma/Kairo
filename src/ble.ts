import "./globals";
import * as sys from "kairo:sys";
import { sleep } from "./index";

export interface BleDevice {
  addr: string;
  name: string;
  rssi: number;
  connectable: boolean;
}

export interface ScanOptions {
  durationMs?: number;
  onDevice?: (device: BleDevice) => void;
  filter?: (device: BleDevice) => boolean;
}

export class BleConnection {
  readonly addr: string;
  private readonly conn: number;
  private _paired = false;

  constructor(addr: string, conn: number) {
    this.addr = addr;
    this.conn = conn;
  }

  get connected(): boolean {
    try {
      return sys.ble_conn_status(this.conn) === 1;
    } catch {
      return false;
    }
  }

  get paired(): boolean {
    return this._paired;
  }

  async pair(opts: { timeoutMs?: number } = {}): Promise<void> {
    const timeout = opts.timeoutMs ?? 30000;
    sys.ble_pair(this.conn);
    const t0 = Date.now();
    for (;;) {
      const s = sys.ble_pair_status(this.conn);
      if (s === 1) {
        this._paired = true;
        return;
      }
      if (s === 2) throw new Error(`ble: pairing with ${this.addr} failed`);
      if (Date.now() - t0 > timeout) throw new Error(`ble: pairing with ${this.addr} timed out`);
      await sleep(100);
    }
  }

  disconnect(): void {
    try {
      sys.ble_disconnect(this.conn);
    } catch {}
  }
}

let inited = false;

async function ensureInit(): Promise<void> {
  if (inited) return;
  const t0 = Date.now();
  for (;;) {
    try {
      sys.ble_init();
      inited = true;
      return;
    } catch (e) {
      if (!String(e).includes("would block") || Date.now() - t0 > 5000) throw e;
      await sleep(50);
    }
  }
}

export const ble = {
  init: ensureInit,
  async scan(opts: ScanOptions = {}): Promise<BleDevice[]> {
    await ensureInit();
    const duration = opts.durationMs ?? 5000;
    sys.ble_scan_start(duration);
    const seen = new Map<string, BleDevice>();
    const t0 = Date.now();
    for (;;) {
      const { devices, done } = sys.ble_scan_results();
      for (const d of devices) {
        if (opts.filter && !opts.filter(d)) continue;
        if (!seen.has(d.addr)) {
          seen.set(d.addr, d);
          opts.onDevice?.(d);
        } else seen.set(d.addr, d);
      }
      if (done || Date.now() - t0 > duration + 2000) break;
      await sleep(100);
    }
    try {
      sys.ble_scan_stop();
    } catch {}
    return [...seen.values()].sort((a, b) => b.rssi - a.rssi);
  },
  async connect(addr: string, opts: { timeoutMs?: number } = {}): Promise<BleConnection> {
    await ensureInit();
    const timeout = opts.timeoutMs ?? 10000;
    const conn = sys.ble_connect(addr);
    const t0 = Date.now();
    for (;;) {
      const s = sys.ble_conn_status(conn);
      if (s === 1) return new BleConnection(addr, conn);
      if (s === 2) {
        sys.ble_disconnect(conn);
        throw new Error(`ble: connection to ${addr} failed`);
      }
      if (Date.now() - t0 > timeout) {
        sys.ble_disconnect(conn);
        throw new Error(`ble: connection to ${addr} timed out`);
      }
      await sleep(50);
    }
  },
};

export default ble;
