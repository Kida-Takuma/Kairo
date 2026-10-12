/// <reference path="./sys.d.ts" />
/// <reference path="./node.d.ts" />
import "./globals";
import * as sys from "kairo:sys";

export type Level = boolean | 0 | 1;
export type Pull = "up" | "down" | "none";

export interface OutputPin {
  readonly pin: number;
  readonly value: boolean;
  high(): void;
  low(): void;
  write(level: Level): void;
  toggle(): void;
}

export type Edge = "rise" | "fall" | "change";

export interface InputPin {
  readonly pin: number;
  read(): boolean;
  waitFor(level: Level, pollMs?: number): Promise<void>;
  on(edge: Edge, fn: EdgeHandler): InputPin;
  off(edge?: Edge, fn?: EdgeHandler): InputPin;
  pulseIn(level: Level, timeoutUs?: number): number;
}

export type EdgeHandler = (level: boolean, timeUs: number) => void;

function makeOutput(pin: number, initial: boolean): OutputPin {
  let current = initial;
  sys.gpio_mode(pin, 1);
  sys.gpio_write(pin, current);
  const set = (v: boolean) => {
    current = v;
    sys.gpio_write(pin, v);
  };
  return {
    pin,
    get value() {
      return current;
    },
    high: () => set(true),
    low: () => set(false),
    write: (level: Level) => set(!!level),
    toggle: () => set(!current),
  };
}

function makeInput(pin: number, pull: Pull): InputPin {
  sys.gpio_mode(pin, pull === "up" ? 2 : pull === "down" ? 3 : 0);
  const read = () => sys.gpio_read(pin) === 1;
  let listeners: { edge: Edge; fn: EdgeHandler }[] = [];
  const dispatch = (level: number, t: number) => {
    const high = level === 1;
    for (const l of [...listeners]) if (l.edge === "change" || (l.edge === "rise") === high) l.fn(high, t);
  };
  const self: InputPin = {
    pin,
    read,
    async waitFor(level: Level, pollMs = 5) {
      const want = !!level;
      while (read() !== want) await sleep(pollMs);
    },
    on(edge: Edge, fn: EdgeHandler) {
      if (!listeners.length) sys.gpio_watch(pin, 3, dispatch);
      listeners.push({ edge, fn });
      return self;
    },
    off(edge?: Edge, fn?: EdgeHandler) {
      listeners = listeners.filter((l) => (edge !== undefined && l.edge !== edge) || (fn !== undefined && l.fn !== fn));
      if (!listeners.length) sys.gpio_unwatch(pin);
      return self;
    },
    pulseIn(level: Level, timeoutUs = 1000000) {
      return sys.gpio_pulse_in(pin, !!level, timeoutUs);
    },
  };
  return self;
}

export const gpio = {
  output(pin: number, initial: Level = false): OutputPin {
    return makeOutput(pin, !!initial);
  },
  input(pin: number, opts: { pull?: Pull } = {}): InputPin {
    return makeInput(pin, opts.pull ?? "none");
  },
};

export const time = {
  now: (): number => sys.time_us() / 1000,
  micros: (): number => sys.time_us(),
};

export function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => {
    sys.timer_start(ms, 0, resolve);
  });
}

export interface Timer {
  stop(): boolean;
}

export function every(ms: number, fn: () => void): Timer {
  const id = sys.timer_start(ms, Math.max(ms, 1), fn);
  return { stop: () => sys.timer_cancel(id) };
}

export function after(ms: number, fn: () => void): Timer {
  const id = sys.timer_start(ms, 0, fn);
  return { stop: () => sys.timer_cancel(id) };
}

export function halt(): void {
  sys.halt();
}

export const board = {
  get name(): string {
    return sys.board();
  },
};
