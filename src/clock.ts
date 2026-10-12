import "./globals";
import * as sys from "kairo:sys";
import { UdpSocket } from "./net";

const NTP_EPOCH_OFFSET_MS = 2208988800000;
const MIN_VALID_MS = Date.UTC(2020, 0, 1);

export interface NtpOptions {
  server?: string;
  port?: number;
  timeoutMs?: number;
}

export const clock = {
  epochMs(): number {
    return sys.rtc_get();
  },
  now(): Date {
    return new Date(sys.rtc_get());
  },
  get isSet(): boolean {
    return sys.rtc_get() > MIN_VALID_MS;
  },
  set(value: Date | number | string): void {
    const ms = value instanceof Date ? value.getTime() : typeof value === "number" ? value : new Date(value).getTime();
    if (!Number.isFinite(ms)) throw new TypeError("clock.set: invalid time");
    sys.rtc_set(ms);
  },
  uptimeMs(): number {
    return sys.time_us() / 1000;
  },
  syncNtp(opts: NtpOptions = {}): Promise<Date> {
    const server = opts.server ?? "pool.ntp.org";
    const port = opts.port ?? 123;
    const timeout = opts.timeoutMs ?? 5000;
    return new Promise((resolve, reject) => {
      const sock = new UdpSocket();
      const timer = setTimeout(() => {
        sock.close();
        reject(new Error(`ntp: no reply from ${server}`));
      }, timeout);
      const packet = new Uint8Array(48);
      packet[0] = 0x1b;
      const t1 = sys.time_us();
      sock.on("message", (data: Uint8Array) => {
        if (data.length < 48) return;
        clearTimeout(timer);
        sock.close();
        const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        const secs = view.getUint32(40);
        const frac = view.getUint32(44);
        const serverMs = secs * 1000 + Math.round((frac / 4294967296) * 1000) - NTP_EPOCH_OFFSET_MS;
        const rtt = (sys.time_us() - t1) / 1000;
        const now = serverMs + rtt / 2;
        sys.rtc_set(now);
        resolve(new Date(now));
      });
      sock.on("error", (e: unknown) => {
        clearTimeout(timer);
        sock.close();
        reject(e);
      });
      try {
        sock.send(packet, server, port);
      } catch (e) {
        clearTimeout(timer);
        sock.close();
        reject(e);
      }
    });
  },
};

export default clock;
