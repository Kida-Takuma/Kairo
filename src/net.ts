import "./globals";
import * as sys from "kairo:sys";
import { EventEmitter } from "../shims/events";
import { concat, toBytes } from "./bytes";

export const POLL_IN = 1;
export const POLL_OUT = 2;
export const POLL_ERR = 4;
export const POLL_HUP = 8;

export interface ConnectOptions {
  tls?: boolean;
  timeoutMs?: number;
}

export class TcpSocket extends EventEmitter {
  readonly fd: number;
  remoteAddr = "";
  remotePort = 0;
  private queue: Uint8Array[] = [];
  private queued = 0;
  private ending = false;
  private _closed = false;
  private _connected = false;

  constructor(fd: number, connected: boolean) {
    super();
    this.fd = fd;
    this._connected = connected;
    if (connected) this.watch();
  }

  get closed(): boolean {
    return this._closed;
  }

  get connected(): boolean {
    return this._connected && !this._closed;
  }

  get pending(): number {
    return this.queued;
  }

  static connect(host: string, port: number, opts: ConnectOptions = {}): Promise<TcpSocket> {
    return new Promise((resolve, reject) => {
      const fd = sys.net_socket(0);
      let done = false;
      let timer: number | undefined;
      const finish = (err?: Error) => {
        if (done) return;
        done = true;
        if (timer !== undefined) clearTimeout(timer);
        sys.net_unwatch(fd);
        if (err) {
          sys.net_close(fd);
          reject(err);
          return;
        }
        const s = new TcpSocket(fd, true);
        s.remoteAddr = host;
        s.remotePort = port;
        resolve(s);
      };
      try {
        const now = opts.tls ? sys.net_tls_connect(fd, host, port) : sys.net_connect(fd, host, port);
        if (now) return finish();
      } catch (e) {
        return finish(e as Error);
      }
      if (opts.timeoutMs) timer = setTimeout(() => finish(new Error(`connect ${host}:${port}: timeout`)), opts.timeoutMs);
      sys.net_watch(fd, POLL_OUT | POLL_IN, () => {
        try {
          if (sys.net_connect_status(fd)) finish();
        } catch (e) {
          finish(e as Error);
        }
      });
    });
  }

  private watch(): void {
    if (this._closed) return;
    sys.net_watch(this.fd, POLL_IN | (this.queued ? POLL_OUT : 0), (ev) => this.onPoll(ev));
  }

  private onPoll(ev: number): void {
    if (this._closed) return;
    if (ev & POLL_IN) this.drainIn();
    if (this._closed) return;
    if (ev & POLL_OUT) this.flush();
    if (ev & POLL_ERR) this.destroy(new Error("socket error"));
  }

  private drainIn(): void {
    for (let i = 0; i < 8 && !this._closed; i++) {
      let chunk: Uint8Array | null | undefined;
      try {
        chunk = sys.net_recv(this.fd, 4096);
      } catch (e) {
        this.destroy(e as Error);
        return;
      }
      if (chunk === undefined) return;
      if (chunk === null) {
        this.emit("end");
        this.destroy();
        return;
      }
      this.emit("data", chunk);
      if (chunk.length < 4096) return;
    }
  }

  write(data: string | Uint8Array | ArrayBuffer): boolean {
    if (this._closed || this.ending) return false;
    const bytes = toBytes(data);
    if (!bytes.length) return true;
    this.queue.push(bytes);
    this.queued += bytes.length;
    this.flush();
    return this.queued < 16384;
  }

  private flush(): void {
    while (this.queue.length && !this._closed) {
      const head = this.queue[0]!;
      let n: number;
      try {
        n = sys.net_send(this.fd, head);
      } catch (e) {
        this.destroy(e as Error);
        return;
      }
      if (n <= 0) break;
      this.queued -= n;
      if (n < head.length) {
        this.queue[0] = head.subarray(n);
        break;
      }
      this.queue.shift();
    }
    if (this._closed) return;
    if (!this.queue.length) {
      this.emit("drain");
      if (this.ending) {
        this.destroy();
        return;
      }
    }
    this.watch();
  }

  end(data?: string | Uint8Array): void {
    if (data !== undefined) this.write(data);
    this.ending = true;
    if (!this.queue.length) this.destroy();
  }

  destroy(err?: Error): void {
    if (this._closed) return;
    this._closed = true;
    this._connected = false;
    this.queue = [];
    this.queued = 0;
    try {
      sys.net_close(this.fd);
    } catch {}
    if (err) this.emit("error", err);
    this.emit("close", !!err);
  }

  close(): void {
    this.destroy();
  }

  readAll(): Promise<Uint8Array> {
    return new Promise((resolve, reject) => {
      const chunks: Uint8Array[] = [];
      this.on("data", (c: Uint8Array) => chunks.push(c));
      this.on("close", () => resolve(concat(chunks)));
      this.on("error", reject);
    });
  }
}

export class TcpServer extends EventEmitter {
  private fd = -1;
  private _port = 0;

  get port(): number {
    return this._port;
  }

  get listening(): boolean {
    return this.fd >= 0;
  }

  listen(port: number, cb?: () => void): this {
    if (this.fd >= 0) throw new Error("already listening");
    const fd = sys.net_socket(0);
    try {
      sys.net_listen(fd, port, 8);
    } catch (e) {
      sys.net_close(fd);
      throw e;
    }
    this.fd = fd;
    this._port = port;
    sys.net_watch(fd, POLL_IN, () => this.accept());
    if (cb) queueMicrotask(cb);
    queueMicrotask(() => this.emit("listening"));
    return this;
  }

  private accept(): void {
    for (let i = 0; i < 8; i++) {
      let a: { fd: number; addr: string; port: number } | undefined;
      try {
        a = sys.net_accept(this.fd);
      } catch (e) {
        this.emit("error", e);
        return;
      }
      if (!a) return;
      const s = new TcpSocket(a.fd, true);
      s.remoteAddr = a.addr;
      s.remotePort = a.port;
      this.emit("connection", s);
    }
  }

  close(): void {
    if (this.fd < 0) return;
    sys.net_close(this.fd);
    this.fd = -1;
    this.emit("close");
  }
}

export class UdpSocket extends EventEmitter {
  readonly fd: number;
  private _closed = false;

  constructor() {
    super();
    this.fd = sys.net_socket(1);
  }

  bind(port: number): this {
    sys.net_bind(this.fd, port);
    this.watch();
    return this;
  }

  private watch(): void {
    sys.net_watch(this.fd, POLL_IN, () => {
      for (let i = 0; i < 8; i++) {
        let m: { data: Uint8Array; addr: string; port: number } | undefined;
        try {
          m = sys.net_recvfrom(this.fd, 2048);
        } catch (e) {
          this.emit("error", e);
          return;
        }
        if (!m) return;
        this.emit("message", m.data, { addr: m.addr, port: m.port });
      }
    });
  }

  send(data: string | Uint8Array | ArrayBuffer, host: string, port: number): number {
    if (this._closed) throw new Error("socket closed");
    this.watch();
    return sys.net_sendto(this.fd, host, port, toBytes(data));
  }

  close(): void {
    if (this._closed) return;
    this._closed = true;
    sys.net_close(this.fd);
    this.emit("close");
  }
}

export function connect(host: string, port: number, opts?: ConnectOptions): Promise<TcpSocket> {
  return TcpSocket.connect(host, port, opts);
}

export function createServer(onConnection?: (socket: TcpSocket) => void): TcpServer {
  const s = new TcpServer();
  if (onConnection) s.on("connection", onConnection);
  return s;
}

export function resolve(host: string): string {
  return sys.net_resolve(host);
}

export function localIp(): string | null {
  return sys.net_local_ip();
}
