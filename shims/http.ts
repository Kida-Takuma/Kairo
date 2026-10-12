import { EventEmitter } from "./events";
import { concat, toBytes } from "../src/bytes";
import { fetch as kairoFetch } from "../src/fetch";
import { serve, type HttpServer } from "../src/http";
import { KairoHeaders, KairoRequest, KairoResponse, KairoURL, STATUS_TEXT } from "../src/web";

export const STATUS_CODES: Record<number, string> = STATUS_TEXT;
export const METHODS = ["DELETE", "GET", "HEAD", "OPTIONS", "PATCH", "POST", "PUT"];

type HeaderValue = string | number | string[];

export class IncomingMessage extends EventEmitter {
  method = "GET";
  url = "/";
  headers: Record<string, string> = {};
  rawHeaders: string[] = [];
  httpVersion = "1.1";
  httpVersionMajor = 1;
  httpVersionMinor = 1;
  statusCode?: number;
  statusMessage?: string;
  complete = false;
  socket: { remoteAddress: string; remotePort: number; encrypted: boolean };
  connection: { remoteAddress: string; remotePort: number; encrypted: boolean };
  rawBody: Uint8Array = new Uint8Array(0);
  private encoding: string | null = null;

  constructor(remoteAddress = "", remotePort = 0) {
    super();
    this.socket = { remoteAddress, remotePort, encrypted: false };
    this.connection = this.socket;
  }

  setEncoding(enc: string): this {
    this.encoding = enc;
    return this;
  }

  pushBody(body: Uint8Array): void {
    this.rawBody = body;
    queueMicrotask(() => {
      if (body.length) this.emit("data", this.encoding ? new TextDecoder().decode(body) : body);
      this.complete = true;
      this.emit("end");
    });
  }

  resume(): this {
    return this;
  }
  pause(): this {
    return this;
  }
  destroy(): this {
    return this;
  }
  async *[Symbol.asyncIterator](): AsyncGenerator<Uint8Array> {
    if (this.rawBody.length) yield this.rawBody;
  }
}

export class ServerResponse extends EventEmitter {
  statusCode = 200;
  statusMessage = "";
  headersSent = false;
  finished = false;
  writableEnded = false;
  locals: Record<string, unknown> = {};
  req: IncomingMessage;
  private hdrs = new Map<string, HeaderValue>();
  private chunks: Uint8Array[] = [];
  private resolve: (r: KairoResponse) => void;

  constructor(req: IncomingMessage, resolve: (r: KairoResponse) => void) {
    super();
    this.req = req;
    this.resolve = resolve;
  }

  setHeader(name: string, value: HeaderValue): this {
    this.hdrs.set(name.toLowerCase(), value);
    return this;
  }
  getHeader(name: string): HeaderValue | undefined {
    return this.hdrs.get(name.toLowerCase());
  }
  getHeaders(): Record<string, HeaderValue> {
    return Object.fromEntries(this.hdrs);
  }
  getHeaderNames(): string[] {
    return [...this.hdrs.keys()];
  }
  hasHeader(name: string): boolean {
    return this.hdrs.has(name.toLowerCase());
  }
  removeHeader(name: string): void {
    this.hdrs.delete(name.toLowerCase());
  }
  writeHead(status: number, msgOrHeaders?: string | Record<string, HeaderValue>, headers?: Record<string, HeaderValue>): this {
    this.statusCode = status;
    if (typeof msgOrHeaders === "string") this.statusMessage = msgOrHeaders;
    const h = typeof msgOrHeaders === "object" ? msgOrHeaders : headers;
    if (h) for (const [k, v] of Object.entries(h)) this.setHeader(k, v);
    this.headersSent = true;
    return this;
  }
  flushHeaders(): void {
    this.headersSent = true;
  }
  write(chunk: string | Uint8Array, cb?: () => void): boolean {
    this.headersSent = true;
    this.chunks.push(toBytes(chunk));
    cb?.();
    return true;
  }
  end(chunk?: string | Uint8Array | (() => void), cb?: () => void): this {
    if (this.finished) return this;
    if (typeof chunk === "function") cb = chunk;
    else if (chunk !== undefined && chunk !== null) this.chunks.push(toBytes(chunk));
    this.finished = true;
    this.writableEnded = true;
    this.headersSent = true;
    const headers = new KairoHeaders();
    for (const [k, v] of this.hdrs) {
      if (Array.isArray(v)) for (const x of v) headers.append(k, x);
      else headers.set(k, String(v));
    }
    const body = this.chunks.length ? concat(this.chunks) : null;
    this.resolve(new KairoResponse(body, { status: this.statusCode, statusText: this.statusMessage || undefined, headers }));
    cb?.();
    this.emit("finish");
    this.emit("close");
    return this;
  }
}

export type RequestListener = (req: IncomingMessage, res: ServerResponse) => void;

export class Server extends EventEmitter {
  private inner: HttpServer | null = null;
  private _port = 0;
  listening = false;

  constructor(listener?: RequestListener) {
    super();
    if (listener) this.on("request", listener);
  }

  listen(port?: number | { port?: number }, a?: string | (() => void), b?: () => void): this {
    const p = typeof port === "object" ? (port.port ?? 80) : (port ?? 80);
    const cb = typeof a === "function" ? a : b;
    this._port = p;
    this.inner = serve({
      port: p,
      fetch: (request, info) => this.dispatch(request, info),
      onListen: () => {
        this.listening = true;
        this.emit("listening");
        cb?.();
      },
    });
    return this;
  }

  private dispatch(request: KairoRequest, info: { remoteAddr: string; remotePort: number }): Promise<KairoResponse> {
    return new Promise((resolve) => {
      const url = new KairoURL(request.url);
      const req = new IncomingMessage(info.remoteAddr, info.remotePort);
      req.method = request.method;
      req.url = url.pathname + url.search;
      for (const [k, v] of request.headers.rawEntries()) {
        req.rawHeaders.push(k, v);
        req.headers[k] = req.headers[k] ? `${req.headers[k]}, ${v}` : v;
      }
      const res = new ServerResponse(req, resolve);
      if (!this.listenerCount("request")) {
        res.statusCode = 404;
        res.end("not found");
        return;
      }
      req.pushBody(request.body ?? new Uint8Array(0));
      this.emit("request", req, res);
    });
  }

  address(): { port: number; address: string; family: string } {
    return { port: this._port, address: "0.0.0.0", family: "IPv4" };
  }

  close(cb?: (err?: Error) => void): this {
    this.inner?.close();
    this.inner = null;
    this.listening = false;
    this.emit("close");
    cb?.();
    return this;
  }

  setTimeout(): this {
    return this;
  }
}

export function createServer(optsOrListener?: RequestListener | object, listener?: RequestListener): Server {
  return new Server(typeof optsOrListener === "function" ? (optsOrListener as RequestListener) : listener);
}

export interface RequestOptions {
  protocol?: string;
  host?: string;
  hostname?: string;
  port?: number | string;
  path?: string;
  method?: string;
  headers?: Record<string, HeaderValue>;
  timeout?: number;
}

export class ClientRequest extends EventEmitter {
  private chunks: Uint8Array[] = [];
  private hdrs: Record<string, HeaderValue> = {};
  private url: string;
  private method: string;
  private timeoutMs: number | undefined;
  private sent = false;

  constructor(url: string, opts: RequestOptions) {
    super();
    this.url = url;
    this.method = (opts.method ?? "GET").toUpperCase();
    this.timeoutMs = opts.timeout;
    if (opts.headers) for (const [k, v] of Object.entries(opts.headers)) this.setHeader(k, v);
  }

  setHeader(k: string, v: HeaderValue): this {
    this.hdrs[k.toLowerCase()] = v;
    return this;
  }
  getHeader(k: string): HeaderValue | undefined {
    return this.hdrs[k.toLowerCase()];
  }
  removeHeader(k: string): void {
    delete this.hdrs[k.toLowerCase()];
  }
  setTimeout(ms: number, cb?: () => void): this {
    this.timeoutMs = ms;
    if (cb) this.once("timeout", cb);
    return this;
  }
  write(chunk: string | Uint8Array, cb?: () => void): boolean {
    this.chunks.push(toBytes(chunk));
    cb?.();
    return true;
  }
  end(chunk?: string | Uint8Array | (() => void), cb?: () => void): this {
    if (this.sent) return this;
    this.sent = true;
    if (typeof chunk === "function") cb = chunk;
    else if (chunk !== undefined && chunk !== null) this.chunks.push(toBytes(chunk));
    const body = this.chunks.length ? concat(this.chunks) : null;
    const headers: Record<string, string> = {};
    for (const [k, v] of Object.entries(this.hdrs)) headers[k] = Array.isArray(v) ? v.join(", ") : String(v);
    kairoFetch(this.url, { method: this.method, headers, body, timeoutMs: this.timeoutMs, redirect: "manual" })
      .then((r) => {
        const res = new IncomingMessage();
        res.statusCode = r.status;
        res.statusMessage = r.statusText;
        for (const [k, v] of r.headers.rawEntries()) {
          res.rawHeaders.push(k, v);
          res.headers[k] = res.headers[k] ? `${res.headers[k]}, ${v}` : v;
        }
        res.pushBody(r.body ?? new Uint8Array(0));
        this.emit("response", res);
        cb?.();
      })
      .catch((e) => {
        if (String(e?.message).includes("timeout")) this.emit("timeout");
        this.emit("error", e);
      });
    return this;
  }
  abort(): void {}
  destroy(): this {
    return this;
  }
}

function toUrl(input: string | KairoURL | RequestOptions, defaultProtocol: string): { url: string; opts: RequestOptions } {
  if (typeof input === "string" || input instanceof KairoURL) return { url: String(input), opts: {} };
  const proto = input.protocol ?? defaultProtocol;
  const host = input.hostname ?? input.host ?? "localhost";
  const port = input.port ? `:${input.port}` : "";
  return { url: `${proto}//${host}${port}${input.path ?? "/"}`, opts: input };
}

export function request(
  input: string | KairoURL | RequestOptions,
  optsOrCb?: RequestOptions | ((res: IncomingMessage) => void),
  cb?: (res: IncomingMessage) => void,
): ClientRequest {
  const { url, opts } = toUrl(input, "http:");
  const extra = typeof optsOrCb === "object" ? optsOrCb : {};
  const callback = typeof optsOrCb === "function" ? optsOrCb : cb;
  const req = new ClientRequest(url, { ...opts, ...extra });
  if (callback) req.once("response", callback);
  return req;
}

export function get(
  input: string | KairoURL | RequestOptions,
  optsOrCb?: RequestOptions | ((res: IncomingMessage) => void),
  cb?: (res: IncomingMessage) => void,
): ClientRequest {
  const req = request(input, optsOrCb, cb);
  req.end();
  return req;
}

export class Agent {
  constructor(_opts?: unknown) {}
  destroy(): void {}
}
export const globalAgent = new Agent();

export default { createServer, request, get, Server, IncomingMessage, ServerResponse, ClientRequest, STATUS_CODES, METHODS, Agent, globalAgent };
