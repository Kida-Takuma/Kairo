import "./globals";
import * as sys from "kairo:sys";

export interface PwmPin {
  readonly pin: number;
  readonly frequency: number;
  readonly duty: number;
  write(duty: number): void;
  setFrequency(hz: number): void;
  stop(): void;
}

export const pwm = {
  output(pin: number, opts: { frequency?: number; duty?: number } = {}): PwmPin {
    let frequency = opts.frequency ?? 1000;
    let duty = Math.min(1, Math.max(0, opts.duty ?? 0));
    sys.pwm_set(pin, frequency, duty);
    return {
      pin,
      get frequency() {
        return frequency;
      },
      get duty() {
        return duty;
      },
      write(d: number) {
        duty = Math.min(1, Math.max(0, d));
        sys.pwm_set(pin, frequency, duty);
      },
      setFrequency(hz: number) {
        frequency = hz;
        sys.pwm_set(pin, frequency, duty);
      },
      stop() {
        sys.pwm_stop(pin);
      },
    };
  },
};

export interface ServoOptions {
  minUs?: number;
  maxUs?: number;
  minAngle?: number;
  maxAngle?: number;
  frequency?: number;
}

export interface Servo {
  readonly pin: number;
  readonly angle: number;
  write(angle: number): void;
  writeMicroseconds(us: number): void;
  detach(): void;
}

export const servo = {
  attach(pin: number, opts: ServoOptions = {}): Servo {
    const minUs = opts.minUs ?? 500;
    const maxUs = opts.maxUs ?? 2400;
    const minAngle = opts.minAngle ?? 0;
    const maxAngle = opts.maxAngle ?? 180;
    const frequency = opts.frequency ?? 50;
    const period = 1e6 / frequency;
    let current = (minAngle + maxAngle) / 2;
    const writeUs = (us: number) => sys.pwm_set(pin, frequency, Math.min(1, Math.max(0, us / period)));
    const angleToUs = (a: number) => minUs + ((Math.min(maxAngle, Math.max(minAngle, a)) - minAngle) / (maxAngle - minAngle)) * (maxUs - minUs);
    writeUs(angleToUs(current));
    return {
      pin,
      get angle() {
        return current;
      },
      write(angle: number) {
        current = Math.min(maxAngle, Math.max(minAngle, angle));
        writeUs(angleToUs(current));
      },
      writeMicroseconds(us: number) {
        current = minAngle + ((us - minUs) / (maxUs - minUs)) * (maxAngle - minAngle);
        writeUs(us);
      },
      detach() {
        sys.pwm_stop(pin);
      },
    };
  },
};

export default pwm;
