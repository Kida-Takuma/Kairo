import "./globals";
import * as sys from "kairo:sys";

export interface BatteryConfig {
  adc?: number;
  divider?: number;
  full?: number;
  empty?: number;
  samples?: number;
}

const LIPO_CURVE: [number, number][] = [
  [4.2, 100],
  [4.15, 95],
  [4.11, 90],
  [4.08, 85],
  [4.02, 80],
  [3.98, 75],
  [3.95, 70],
  [3.91, 65],
  [3.87, 60],
  [3.85, 55],
  [3.84, 50],
  [3.82, 45],
  [3.8, 40],
  [3.79, 35],
  [3.77, 30],
  [3.75, 25],
  [3.73, 20],
  [3.71, 15],
  [3.69, 10],
  [3.61, 5],
  [3.27, 0],
];

let cfg: Required<BatteryConfig> = { adc: 0, divider: 2, full: 4.2, empty: 3.3, samples: 4 };

function curvePercent(v: number): number {
  if (v >= LIPO_CURVE[0]![0]) return 100;
  for (let i = 1; i < LIPO_CURVE.length; i++) {
    const [v1, p1] = LIPO_CURVE[i]!;
    if (v >= v1) {
      const [v0, p0] = LIPO_CURVE[i - 1]!;
      return Math.round(p1 + ((v - v1) / (v0 - v1)) * (p0 - p1));
    }
  }
  return 0;
}

export const battery = {
  configure(c: BatteryConfig): void {
    cfg = { ...cfg, ...c };
  },
  voltage(): number {
    let sum = 0;
    for (let i = 0; i < cfg.samples; i++) sum += sys.adc_read_mv(cfg.adc);
    return ((sum / cfg.samples) * cfg.divider) / 1000;
  },
  percent(): number {
    const v = this.voltage();
    if (cfg.full === 4.2 && cfg.empty === 3.3) return curvePercent(v);
    return Math.max(0, Math.min(100, Math.round(((v - cfg.empty) / (cfg.full - cfg.empty)) * 100)));
  },
  read(): { voltage: number; percent: number } {
    const voltage = this.voltage();
    return {
      voltage,
      percent:
        cfg.full === 4.2 && cfg.empty === 3.3
          ? curvePercent(voltage)
          : Math.max(0, Math.min(100, Math.round(((voltage - cfg.empty) / (cfg.full - cfg.empty)) * 100))),
    };
  },
};

export default battery;
