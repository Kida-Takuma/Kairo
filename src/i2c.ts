import "./globals";
import * as sys from "kairo:sys";
import { toBytes } from "./bytes";

export interface I2cOptions {
  sda: number;
  scl: number;
  frequency?: number;
}

export class I2cBus {
  readonly bus: number;

  constructor(bus: number, opts: I2cOptions) {
    this.bus = bus;
    sys.i2c_open(bus, opts.sda, opts.scl, opts.frequency ?? 100000);
  }

  write(addr: number, data: Uint8Array | number[] | string): boolean {
    return sys.i2c_write(this.bus, addr, Array.isArray(data) ? new Uint8Array(data) : toBytes(data));
  }

  read(addr: number, length: number): Uint8Array | null {
    return sys.i2c_read(this.bus, addr, length);
  }

  writeRead(addr: number, data: Uint8Array | number[] | string, length: number): Uint8Array | null {
    return sys.i2c_write_read(this.bus, addr, Array.isArray(data) ? new Uint8Array(data) : toBytes(data), length);
  }

  readRegister(addr: number, register: number, length = 1): Uint8Array | null {
    return this.writeRead(addr, [register], length);
  }

  readRegisterU8(addr: number, register: number): number | null {
    return this.readRegister(addr, register, 1)?.[0] ?? null;
  }

  readRegisterU16(addr: number, register: number, littleEndian = false): number | null {
    const b = this.readRegister(addr, register, 2);
    if (!b) return null;
    return littleEndian ? b[0]! | (b[1]! << 8) : (b[0]! << 8) | b[1]!;
  }

  writeRegister(addr: number, register: number, data: number | Uint8Array | number[]): boolean {
    const payload = typeof data === "number" ? [data] : Array.from(data);
    return this.write(addr, [register, ...payload]);
  }

  probe(addr: number): boolean {
    return sys.i2c_write(this.bus, addr, new Uint8Array(0));
  }

  scan(): number[] {
    const found: number[] = [];
    for (let addr = 0x08; addr <= 0x77; addr++) if (this.probe(addr)) found.push(addr);
    return found;
  }

  device(addr: number): I2cDevice {
    return new I2cDevice(this, addr);
  }

  close(): void {
    sys.i2c_close(this.bus);
  }
}

export class I2cDevice {
  constructor(
    readonly bus: I2cBus,
    readonly address: number,
  ) {}
  write(data: Uint8Array | number[] | string): boolean {
    return this.bus.write(this.address, data);
  }
  read(length: number): Uint8Array | null {
    return this.bus.read(this.address, length);
  }
  readRegister(register: number, length = 1): Uint8Array | null {
    return this.bus.readRegister(this.address, register, length);
  }
  readRegisterU8(register: number): number | null {
    return this.bus.readRegisterU8(this.address, register);
  }
  readRegisterU16(register: number, littleEndian = false): number | null {
    return this.bus.readRegisterU16(this.address, register, littleEndian);
  }
  writeRegister(register: number, data: number | Uint8Array | number[]): boolean {
    return this.bus.writeRegister(this.address, register, data);
  }
}

export const i2c = {
  open(bus: number, opts: I2cOptions): I2cBus {
    return new I2cBus(bus, opts);
  },
};

export default i2c;
