import "./globals";
import * as sys from "kairo:sys";
import { sleep } from "./index";

export type WifiStatus = "idle" | "connecting" | "connected" | "failed";

export interface AccessPoint {
  ssid: string;
  rssi: number;
  channel: number;
  secure: boolean;
}

const STATUS: WifiStatus[] = ["idle", "connecting", "connected", "failed"];

export const wifi = {
  status(): WifiStatus {
    return STATUS[sys.wifi_status()] ?? "idle";
  },
  async connect(ssid: string, password?: string, opts: { timeoutMs?: number } = {}): Promise<{ ip: string }> {
    const timeout = opts.timeoutMs ?? 20000;
    sys.wifi_connect(ssid, password);
    const t0 = Date.now();
    for (;;) {
      const s = sys.wifi_status();
      if (s === 2) return { ip: sys.net_local_ip() ?? "0.0.0.0" };
      if (s === 3) throw new Error(`wifi: failed to connect to '${ssid}'`);
      if (Date.now() - t0 > timeout) throw new Error(`wifi: timeout connecting to '${ssid}'`);
      await sleep(100);
    }
  },
  disconnect(): void {
    sys.wifi_disconnect();
  },
  async scan(opts: { timeoutMs?: number } = {}): Promise<AccessPoint[]> {
    const timeout = opts.timeoutMs ?? 15000;
    sys.wifi_scan_start();
    const t0 = Date.now();
    for (;;) {
      const r = sys.wifi_scan_results();
      if (r) return r.sort((a, b) => b.rssi - a.rssi);
      if (Date.now() - t0 > timeout) throw new Error("wifi: scan timeout");
      await sleep(100);
    }
  },
  ip(): string | null {
    return sys.net_local_ip();
  },
  rssi(): number {
    return sys.wifi_rssi();
  },
};

export default wifi;
