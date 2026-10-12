import "./globals";
import * as sys from "kairo:sys";
import { EventEmitter } from "../shims/events";
import { toBytes } from "./bytes";

export interface UartOptions {
  tx?: number;
  rx?: number;
  baud?: number;
  pollMs?: number;
}

export class Uart extends EventEmitter {
  readonly id: number;
  private timer: number | undefined;
  private lineBuf = "";
  private decoder = new TextDecoder();

  constructor(id: number, opts: UartOptions = {}) {
    super();
    this.id = id;
    sys.uart_open(id, opts.tx ?? -1, opts.rx ?? -1, opts.baud ?? 9600);
    this.timer = setInterval(() => this.poll(), opts.pollMs ?? 20);
  }

  private poll(): void {
    for (let i = 0; i < 8; i++) {
      let chunk: Uint8Array | undefined;
      try {
        chunk = sys.uart_read(this.id, 512);
      } catch (e) {
        this.emit("error", e);
        this.close();
        return;
      }
      if (!chunk) return;
      this.emit("data", chunk);
      if (this.listenerCount("line")) {
        this.lineBuf += this.decoder.decode(chunk);
        let i2;
        while ((i2 = this.lineBuf.indexOf("\n")) >= 0) {
          const line = this.lineBuf.slice(0, i2).replace(/\r$/, "");
          this.lineBuf = this.lineBuf.slice(i2 + 1);
          if (line) this.emit("line", line);
        }
      }
      if (chunk.length < 512) return;
    }
  }

  write(data: string | Uint8Array): number {
    return sys.uart_write(this.id, toBytes(data));
  }

  close(): void {
    if (this.timer !== undefined) clearInterval(this.timer);
    this.timer = undefined;
    try {
      sys.uart_close(this.id);
    } catch {}
    this.emit("close");
  }
}

export const uart = {
  open(id: number, opts?: UartOptions): Uart {
    return new Uart(id, opts);
  },
};

export default uart;
