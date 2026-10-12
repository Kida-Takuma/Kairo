import * as sys from "kairo:sys";

const g = globalThis as Record<string, unknown>;

export function inspect(v: unknown, depth = 2): string {
  if (typeof v === "string") return JSON.stringify(v);
  if (v instanceof Error) return `${v.name}: ${v.message}${v.stack ? "\n" + v.stack.replace(/\n$/, "") : ""}`;
  if (typeof v === "function") return `[Function: ${v.name || "anonymous"}]`;
  if (typeof v === "bigint") return `${v}n`;
  if (typeof v === "symbol") return v.toString();
  if (v === null || typeof v !== "object") return String(v);
  if (depth < 0) return Array.isArray(v) ? "[Array]" : "[Object]";
  if (Array.isArray(v)) return `[ ${v.map((x) => inspect(x, depth - 1)).join(", ")} ]`;
  if (v instanceof Map) return `Map(${v.size}) { ${[...v].map(([k, x]) => `${inspect(k, depth - 1)} => ${inspect(x, depth - 1)}`).join(", ")} }`;
  if (v instanceof Set) return `Set(${v.size}) { ${[...v].map((x) => inspect(x, depth - 1)).join(", ")} }`;
  if (v instanceof Date) return v.toISOString();
  if (ArrayBuffer.isView(v)) {
    const arr = v as unknown as ArrayLike<number>;
    return `${v.constructor.name}(${arr.length}) [ ${Array.from(arr).slice(0, 32).join(", ")}${arr.length > 32 ? ", ..." : ""} ]`;
  }
  const entries = Object.entries(v as Record<string, unknown>).map(([k, x]) => `${k}: ${inspect(x, depth - 1)}`);
  const name = (v as object).constructor?.name;
  const prefix = name && name !== "Object" ? `${name} ` : "";
  return entries.length ? `${prefix}{ ${entries.join(", ")} }` : `${prefix}{}`;
}

export function format(...args: unknown[]): string {
  let out = "";
  let i = 0;
  const first = args[0];
  if (typeof first === "string" && /%[sdifjoO%]/.test(first)) {
    i = 1;
    out = first.replace(/%([sdifjoO%])/g, (m, c: string) => {
      if (c === "%") return "%";
      if (i >= args.length) return m;
      const a = args[i++];
      switch (c) {
        case "s":
          return typeof a === "string" ? a : inspect(a, 1);
        case "d":
        case "i":
          return String(typeof a === "number" ? (c === "i" ? Math.trunc(a) : a) : Number(a));
        case "f":
          return String(Number(a));
        case "j":
          try {
            return JSON.stringify(a);
          } catch {
            return "[Circular]";
          }
        default:
          return inspect(a);
      }
    });
  }
  for (; i < args.length; i++) {
    const a = args[i];
    out += (out ? " " : "") + (typeof a === "string" ? a : inspect(a));
  }
  return out;
}

const write =
  (prefix: string) =>
  (...args: unknown[]) =>
    sys.console_write(prefix + format(...args) + "\n");
const counts = new Map<string, number>();
const timers = new Map<string, number>();
const consoleImpl = {
  log: write(""),
  info: write(""),
  debug: write(""),
  warn: write("[warn] "),
  error: write("[error] "),
  trace: write("[trace] "),
  assert(cond: unknown, ...args: unknown[]) {
    if (!cond) write("Assertion failed: ")(...args);
  },
  count(label = "default") {
    const n = (counts.get(label) ?? 0) + 1;
    counts.set(label, n);
    sys.console_write(`${label}: ${n}\n`);
  },
  time(label = "default") {
    timers.set(label, sys.time_us());
  },
  timeEnd(label = "default") {
    const t0 = timers.get(label);
    if (t0 !== undefined) sys.console_write(`${label}: ${((sys.time_us() - t0) / 1000).toFixed(3)}ms\n`);
    timers.delete(label);
  },
  dir(v: unknown) {
    sys.console_write(inspect(v) + "\n");
  },
  table(v: unknown) {
    sys.console_write(inspect(v) + "\n");
  },
  group() {},
  groupEnd() {},
};
g.console = consoleImpl;

type TimerCb = (...args: unknown[]) => void;
function startTimer(fn: TimerCb, ms: number, period: boolean, args: unknown[]): number {
  const cb = args.length ? () => fn(...args) : (fn as () => void);
  const d = Math.max(0, Number(ms) || 0);
  return sys.timer_start(d, period ? Math.max(d, 1) : 0, cb);
}
g.setTimeout = (fn: TimerCb, ms = 0, ...args: unknown[]) => startTimer(fn, ms, false, args);
g.setInterval = (fn: TimerCb, ms = 0, ...args: unknown[]) => startTimer(fn, ms, true, args);
g.setImmediate = (fn: TimerCb, ...args: unknown[]) => startTimer(fn, 0, false, args);
const clear = (id: unknown) => {
  if (typeof id === "number") sys.timer_cancel(id);
};
g.clearTimeout = clear;
g.clearInterval = clear;
g.clearImmediate = clear;

class KairoTextEncoder {
  readonly encoding = "utf-8";
  encode(s = ""): Uint8Array {
    const out: number[] = [];
    for (let i = 0; i < s.length; i++) {
      let c = s.charCodeAt(i);
      if (c >= 0xd800 && c < 0xdc00 && i + 1 < s.length) {
        const d = s.charCodeAt(i + 1);
        if (d >= 0xdc00 && d < 0xe000) {
          c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
          i++;
        }
      }
      if (c < 0x80) out.push(c);
      else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
      else if (c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
      else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    }
    return new Uint8Array(out);
  }
  encodeInto(s: string, dest: Uint8Array): { read: number; written: number } {
    const b = this.encode(s);
    const n = Math.min(b.length, dest.length);
    dest.set(b.subarray(0, n));
    return { read: s.length, written: n };
  }
}

class KairoTextDecoder {
  readonly encoding: string;
  readonly fatal: boolean;
  constructor(label = "utf-8", opts: { fatal?: boolean } = {}) {
    const l = label.toLowerCase();
    if (l !== "utf-8" && l !== "utf8") throw new RangeError(`TextDecoder: unsupported encoding ${label}`);
    this.encoding = "utf-8";
    this.fatal = !!opts.fatal;
  }
  decode(input?: ArrayBuffer | ArrayBufferView): string {
    if (!input) return "";
    const b = input instanceof ArrayBuffer ? new Uint8Array(input) : new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
    let s = "";
    for (let i = 0; i < b.length;) {
      const c = b[i]!;
      let cp: number;
      let n: number;
      if (c < 0x80) {
        cp = c;
        n = 1;
      } else if ((c & 0xe0) === 0xc0) {
        cp = c & 0x1f;
        n = 2;
      } else if ((c & 0xf0) === 0xe0) {
        cp = c & 0x0f;
        n = 3;
      } else if ((c & 0xf8) === 0xf0) {
        cp = c & 0x07;
        n = 4;
      } else {
        if (this.fatal) throw new TypeError("TextDecoder: invalid UTF-8");
        s += "�";
        i++;
        continue;
      }
      if (i + n > b.length) {
        if (this.fatal) throw new TypeError("TextDecoder: invalid UTF-8");
        s += "�";
        break;
      }
      for (let k = 1; k < n; k++) cp = (cp << 6) | (b[i + k]! & 0x3f);
      i += n;
      s += cp > 0xffff ? String.fromCharCode(0xd800 + ((cp - 0x10000) >> 10), 0xdc00 + ((cp - 0x10000) & 0x3ff)) : String.fromCharCode(cp);
    }
    return s;
  }
}
if (typeof g.TextEncoder === "undefined") g.TextEncoder = KairoTextEncoder;
if (typeof g.TextDecoder === "undefined") g.TextDecoder = KairoTextDecoder;

export const processImpl = {
  env: {} as Record<string, string | undefined>,
  argv: [] as string[],
  platform: "kairo",
  arch: sys.board(),
  version: "v0.0.0",
  versions: { kairo: "0.0.1" } as Record<string, string>,
  pid: 1,
  browser: false,
  cwd: () => "/",
  exit: (_code?: number) => sys.halt(),
  nextTick: (fn: (...a: unknown[]) => void, ...args: unknown[]) => {
    queueMicrotask(() => fn(...args));
  },
  hrtime: Object.assign(
    (prev?: [number, number]): [number, number] => {
      const us = sys.time_us();
      let s = Math.floor(us / 1e6);
      let ns = (us % 1e6) * 1000;
      if (prev) {
        s -= prev[0];
        ns -= prev[1];
        if (ns < 0) {
          s--;
          ns += 1e9;
        }
      }
      return [s, ns];
    },
    { bigint: () => BigInt(Math.floor(sys.time_us())) * 1000n },
  ),
  uptime: () => sys.time_us() / 1e6,
  memoryUsage: () => ({ rss: 0, heapTotal: 0, heapUsed: 0, external: 0, arrayBuffers: 0 }),
  on: () => processImpl,
  once: () => processImpl,
  off: () => processImpl,
  emit: () => false,
  stdout: {
    write: (s: string) => {
      sys.console_write(String(s));
      return true;
    },
    isTTY: false,
  },
  stderr: {
    write: (s: string) => {
      sys.console_write(String(s));
      return true;
    },
    isTTY: false,
  },
};
g.process = processImpl;
g.global = globalThis;
if (typeof g.crypto === "undefined") {
  g.crypto = {
    getRandomValues<T extends ArrayBufferView>(arr: T): T {
      const bytes = sys.random_bytes(arr.byteLength);
      new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength).set(bytes);
      return arr;
    },
    randomUUID(): string {
      const b = sys.random_bytes(16);
      b[6] = (b[6]! & 0x0f) | 0x40;
      b[8] = (b[8]! & 0x3f) | 0x80;
      const h = [...b].map((x) => x.toString(16).padStart(2, "0")).join("");
      return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`;
    },
  };
}
if (typeof g.structuredClone === "undefined") {
  g.structuredClone = (v: unknown) => (v === undefined ? v : JSON.parse(JSON.stringify(v)));
}

declare global {
  var console: typeof consoleImpl;
  var process: typeof processImpl;
  var global: typeof globalThis;
  function setTimeout(fn: (...args: any[]) => void, ms?: number, ...args: unknown[]): number;
  function setInterval(fn: (...args: any[]) => void, ms?: number, ...args: unknown[]): number;
  function setImmediate(fn: (...args: any[]) => void, ...args: unknown[]): number;
  function clearTimeout(id: number | undefined): void;
  function clearInterval(id: number | undefined): void;
  function clearImmediate(id: number | undefined): void;
  function queueMicrotask(fn: () => void): void;
  function structuredClone<T>(v: T): T;
  function atob(s: string): string;
  function btoa(s: string): string;
  var performance: { now(): number };
  var TextEncoder: typeof KairoTextEncoder;
  type TextEncoder = KairoTextEncoder;
  var TextDecoder: typeof KairoTextDecoder;
  type TextDecoder = KairoTextDecoder;
  var crypto: { getRandomValues<T extends ArrayBufferView>(arr: T): T; randomUUID(): string; subtle: import("../shims/crypto").SubtleCrypto };
}
