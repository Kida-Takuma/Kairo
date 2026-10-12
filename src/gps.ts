import "./globals";
import { EventEmitter } from "../shims/events";
import { Uart, type UartOptions } from "./uart";

export interface GpsFix {
  latitude: number;
  longitude: number;
  altitude: number | null;
  speedKmh: number | null;
  course: number | null;
  satellites: number | null;
  hdop: number | null;
  time: Date | null;
  quality: number;
}

export interface GpsOptions extends UartOptions {
  uart?: number;
}

function parseCoord(value: string, hemi: string): number | null {
  if (!value) return null;
  const dot = value.indexOf(".");
  const degLen = dot - 2;
  const deg = parseInt(value.slice(0, degLen), 10);
  const min = parseFloat(value.slice(degLen));
  if (Number.isNaN(deg) || Number.isNaN(min)) return null;
  const d = deg + min / 60;
  return hemi === "S" || hemi === "W" ? -d : d;
}

function checksumOk(line: string): boolean {
  const star = line.lastIndexOf("*");
  if (star < 0) return true;
  let sum = 0;
  for (let i = 1; i < star; i++) sum ^= line.charCodeAt(i);
  return sum === parseInt(line.slice(star + 1, star + 3), 16);
}

function parseTime(hms: string, dmy?: string): Date | null {
  if (hms.length < 6) return null;
  const h = Number(hms.slice(0, 2));
  const m = Number(hms.slice(2, 4));
  const s = parseFloat(hms.slice(4));
  const now = new Date();
  let y = now.getUTCFullYear();
  let mo = now.getUTCMonth();
  let d = now.getUTCDate();
  if (dmy && dmy.length >= 6) {
    d = Number(dmy.slice(0, 2));
    mo = Number(dmy.slice(2, 4)) - 1;
    y = 2000 + Number(dmy.slice(4, 6));
  }
  return new Date(Date.UTC(y, mo, d, h, m, Math.floor(s), Math.round((s % 1) * 1000)));
}

export class Gps extends EventEmitter {
  readonly uart: Uart;
  position: GpsFix | null = null;
  private partial: Partial<GpsFix> = {};

  constructor(opts: GpsOptions = {}) {
    super();
    this.uart = new Uart(opts.uart ?? 1, { tx: opts.tx ?? -1, rx: opts.rx ?? -1, baud: opts.baud ?? 9600, pollMs: opts.pollMs });
    this.uart.on("line", (line: string) => this.onLine(line));
    this.uart.on("error", (e: unknown) => this.emit("error", e));
  }

  private onLine(line: string): void {
    if (!line.startsWith("$") || !checksumOk(line)) return;
    this.emit("nmea", line);
    const body = line.slice(1, line.lastIndexOf("*") > 0 ? line.lastIndexOf("*") : undefined);
    const f = body.split(",");
    const type = (f[0] ?? "").slice(2);
    if (type === "GGA") {
      const quality = Number(f[6] ?? 0);
      const lat = parseCoord(f[2] ?? "", f[3] ?? "");
      const lon = parseCoord(f[4] ?? "", f[5] ?? "");
      if (quality > 0 && lat !== null && lon !== null) {
        this.partial.latitude = lat;
        this.partial.longitude = lon;
        this.partial.quality = quality;
        this.partial.satellites = f[7] ? Number(f[7]) : null;
        this.partial.hdop = f[8] ? Number(f[8]) : null;
        this.partial.altitude = f[9] ? Number(f[9]) : null;
        this.partial.time = parseTime(f[1] ?? "");
        this.publish();
      } else if (quality === 0) {
        this.emit("nofix");
      }
    } else if (type === "RMC") {
      if (f[2] !== "A") return;
      const lat = parseCoord(f[3] ?? "", f[4] ?? "");
      const lon = parseCoord(f[5] ?? "", f[6] ?? "");
      if (lat === null || lon === null) return;
      this.partial.latitude = lat;
      this.partial.longitude = lon;
      this.partial.speedKmh = f[7] ? Number(f[7]) * 1.852 : null;
      this.partial.course = f[8] ? Number(f[8]) : null;
      this.partial.time = parseTime(f[1] ?? "", f[9]);
      if (this.partial.quality === undefined) this.partial.quality = 1;
      this.publish();
    }
  }

  private publish(): void {
    const p = this.partial;
    if (p.latitude === undefined || p.longitude === undefined) return;
    this.position = {
      latitude: p.latitude,
      longitude: p.longitude,
      altitude: p.altitude ?? null,
      speedKmh: p.speedKmh ?? null,
      course: p.course ?? null,
      satellites: p.satellites ?? null,
      hdop: p.hdop ?? null,
      time: p.time ?? null,
      quality: p.quality ?? 1,
    };
    this.emit("fix", this.position);
  }

  waitForFix(timeoutMs = 60000): Promise<GpsFix> {
    if (this.position) return Promise.resolve(this.position);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.off("fix", onFix);
        reject(new Error("gps: no fix"));
      }, timeoutMs);
      const onFix = (fix: GpsFix) => {
        clearTimeout(timer);
        resolve(fix);
      };
      this.once("fix", onFix);
    });
  }

  close(): void {
    this.uart.close();
  }
}

export const gps = {
  open(opts?: GpsOptions): Gps {
    return new Gps(opts);
  },
};

export default gps;
