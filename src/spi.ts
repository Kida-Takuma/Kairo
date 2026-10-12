import "./globals";
import * as sys from "kairo:sys";
import { gpio, type OutputPin } from "./index";
import { toBytes } from "./bytes";

export interface SpiOptions {
  sck: number;
  mosi?: number;
  miso?: number;
  cs?: number;
  frequency?: number;
  mode?: 0 | 1 | 2 | 3;
}

export class SpiBus {
  readonly bus: number;
  private cs: OutputPin | null = null;

  constructor(bus: number, opts: SpiOptions) {
    this.bus = bus;
    sys.spi_open(bus, opts.sck, opts.mosi ?? -1, opts.miso ?? -1, opts.frequency ?? 1000000, opts.mode ?? 0);
    if (opts.cs !== undefined) this.cs = gpio.output(opts.cs, true);
  }

  transfer(data: Uint8Array | number[] | string): Uint8Array {
    const bytes = Array.isArray(data) ? new Uint8Array(data) : toBytes(data);
    this.cs?.low();
    try {
      return sys.spi_transfer(this.bus, bytes);
    } finally {
      this.cs?.high();
    }
  }

  write(data: Uint8Array | number[] | string): void {
    this.transfer(data);
  }

  read(length: number, fill = 0): Uint8Array {
    return this.transfer(new Uint8Array(length).fill(fill));
  }

  close(): void {
    sys.spi_close(this.bus);
  }
}

export const spi = {
  open(bus: number, opts: SpiOptions): SpiBus {
    return new SpiBus(bus, opts);
  },
};

export default spi;
