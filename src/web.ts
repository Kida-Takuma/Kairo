import "./globals";
import { toBytes } from "./bytes";

type HeadersInitLike = Headers | Record<string, string> | [string, string][];

const g = globalThis as Record<string, unknown>;

export class KairoEvent {
  readonly type: string;
  target: unknown = null;
  defaultPrevented = false;
  constructor(type: string) {
    this.type = type;
  }
  preventDefault(): void {
    this.defaultPrevented = true;
  }
}

export class KairoEventTarget {
  private listeners = new Map<string, Set<(e: KairoEvent) => void>>();
  addEventListener(type: string, fn: ((e: KairoEvent) => void) | null, opts?: { once?: boolean }): void {
    if (!fn) return;
    const wrapped = opts?.once
      ? (e: KairoEvent) => {
          this.removeEventListener(type, wrapped);
          fn(e);
        }
      : fn;
    (wrapped as { original?: unknown }).original = fn;
    let set = this.listeners.get(type);
    if (!set) this.listeners.set(type, (set = new Set()));
    set.add(wrapped);
  }
  removeEventListener(type: string, fn: ((e: KairoEvent) => void) | null): void {
    const set = this.listeners.get(type);
    if (!set || !fn) return;
    for (const l of set) if (l === fn || (l as { original?: unknown }).original === fn) set.delete(l);
  }
  dispatchEvent(e: KairoEvent): boolean {
    e.target = this;
    const handler = (this as Record<string, unknown>)[`on${e.type}`];
    if (typeof handler === "function") handler.call(this, e);
    for (const l of [...(this.listeners.get(e.type) ?? [])]) l(e);
    return !e.defaultPrevented;
  }
}

export class KairoAbortSignal extends KairoEventTarget {
  aborted = false;
  reason: unknown = undefined;
  onabort: ((e: KairoEvent) => void) | null = null;
  throwIfAborted(): void {
    if (this.aborted) throw this.reason;
  }
  static abort(reason?: unknown): KairoAbortSignal {
    const c = new KairoAbortController();
    c.abort(reason);
    return c.signal;
  }
  static timeout(ms: number): KairoAbortSignal {
    const c = new KairoAbortController();
    setTimeout(() => c.abort(new DOMExceptionLike("The operation timed out", "TimeoutError")), ms);
    return c.signal;
  }
  static any(signals: KairoAbortSignal[]): KairoAbortSignal {
    const c = new KairoAbortController();
    for (const s of signals) {
      if (s.aborted) {
        c.abort(s.reason);
        break;
      }
      s.addEventListener("abort", () => c.abort(s.reason), { once: true });
    }
    return c.signal;
  }
}

export class DOMExceptionLike extends Error {
  constructor(message: string, name = "Error") {
    super(message);
    this.name = name;
  }
}

export class KairoAbortController {
  readonly signal = new KairoAbortSignal();
  abort(reason?: unknown): void {
    if (this.signal.aborted) return;
    this.signal.aborted = true;
    this.signal.reason = reason ?? new DOMExceptionLike("This operation was aborted", "AbortError");
    this.signal.dispatchEvent(new KairoEvent("abort"));
  }
}

function encodeComponent(s: string): string {
  return encodeURIComponent(s)
    .replace(/%20/g, "+")
    .replace(/[!'()~]/g, (c) => "%" + c.charCodeAt(0).toString(16).toUpperCase());
}

function decodeComponent(s: string): string {
  try {
    return decodeURIComponent(s.replace(/\+/g, " "));
  } catch {
    return s;
  }
}

export class KairoURLSearchParams {
  private list: [string, string][] = [];
  private onChange: (() => void) | null = null;

  constructor(init?: string | Record<string, string> | [string, string][] | KairoURLSearchParams) {
    if (typeof init === "string") this.parse(init);
    else if (init instanceof KairoURLSearchParams) this.list = [...init.list];
    else if (Array.isArray(init)) this.list = init.map(([k, v]) => [String(k), String(v)]);
    else if (init && typeof init === "object") this.list = Object.entries(init).map(([k, v]) => [k, String(v)]);
  }

  private parse(s: string): void {
    this.list = [];
    if (s.startsWith("?")) s = s.slice(1);
    if (!s) return;
    for (const part of s.split("&")) {
      if (!part) continue;
      const i = part.indexOf("=");
      const k = i < 0 ? part : part.slice(0, i);
      const v = i < 0 ? "" : part.slice(i + 1);
      this.list.push([decodeComponent(k), decodeComponent(v)]);
    }
  }

  static bind(params: KairoURLSearchParams, onChange: () => void): void {
    params.onChange = onChange;
  }

  static reparse(params: KairoURLSearchParams, s: string): void {
    params.parse(s);
  }

  private changed(): void {
    this.onChange?.();
  }

  get size(): number {
    return this.list.length;
  }
  append(k: string, v: string): void {
    this.list.push([String(k), String(v)]);
    this.changed();
  }
  delete(k: string, v?: string): void {
    this.list = this.list.filter(([kk, vv]) => !(kk === k && (v === undefined || vv === v)));
    this.changed();
  }
  get(k: string): string | null {
    return this.list.find(([kk]) => kk === k)?.[1] ?? null;
  }
  getAll(k: string): string[] {
    return this.list.filter(([kk]) => kk === k).map(([, v]) => v);
  }
  has(k: string, v?: string): boolean {
    return this.list.some(([kk, vv]) => kk === k && (v === undefined || vv === v));
  }
  set(k: string, v: string): void {
    const i = this.list.findIndex(([kk]) => kk === k);
    this.list = this.list.filter(([kk], idx) => kk !== k || idx === i);
    if (i < 0) this.list.push([k, String(v)]);
    else this.list[i] = [k, String(v)];
    this.changed();
  }
  sort(): void {
    this.list.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
    this.changed();
  }
  forEach(fn: (v: string, k: string, p: KairoURLSearchParams) => void): void {
    for (const [k, v] of this.list) fn(v, k, this);
  }
  keys(): IterableIterator<string> {
    return this.list.map(([k]) => k)[Symbol.iterator]();
  }
  values(): IterableIterator<string> {
    return this.list.map(([, v]) => v)[Symbol.iterator]();
  }
  entries(): IterableIterator<[string, string]> {
    return this.list.map(([k, v]) => [k, v] as [string, string])[Symbol.iterator]();
  }
  [Symbol.iterator](): IterableIterator<[string, string]> {
    return this.entries();
  }
  toString(): string {
    return this.list.map(([k, v]) => `${encodeComponent(k)}=${encodeComponent(v)}`).join("&");
  }
}

const SPECIAL_PORTS: Record<string, string> = { "http:": "80", "https:": "443", "ws:": "80", "wss:": "443", "ftp:": "21" };

function normalizePath(path: string): string {
  const out: string[] = [];
  for (const seg of path.split("/")) {
    if (seg === "..") out.pop();
    else if (seg !== ".") out.push(seg);
  }
  let s = out.join("/");
  if (path.endsWith("/..") || path.endsWith("/.")) s += "/";
  return s.startsWith("/") ? s : "/" + s;
}

export class KairoURL {
  protocol = "";
  username = "";
  password = "";
  hostname = "";
  port = "";
  pathname = "/";
  search = "";
  hash = "";
  readonly searchParams: KairoURLSearchParams;

  constructor(input: string | KairoURL, base?: string | KairoURL) {
    const str = String(input).trim();
    const m = /^([a-zA-Z][a-zA-Z0-9+.-]*:)(.*)$/.exec(str);
    if (m) {
      this.protocol = m[1]!.toLowerCase();
      this.parseAfterScheme(m[2]!);
    } else if (base !== undefined) {
      const b = base instanceof KairoURL ? base : new KairoURL(base);
      this.protocol = b.protocol;
      if (str.startsWith("//")) {
        this.parseAfterScheme(str);
      } else {
        this.username = b.username;
        this.password = b.password;
        this.hostname = b.hostname;
        this.port = b.port;
        if (str.startsWith("/")) this.parsePath(str);
        else if (str.startsWith("?")) {
          this.pathname = b.pathname;
          this.parsePath(b.pathname + str);
        } else if (str.startsWith("#")) {
          this.pathname = b.pathname;
          this.search = b.search;
          this.hash = str;
        } else if (!str) {
          this.pathname = b.pathname;
          this.search = b.search;
        } else {
          const dir = b.pathname.slice(0, b.pathname.lastIndexOf("/") + 1);
          this.parsePath(dir + str);
        }
      }
    } else {
      throw new TypeError(`Invalid URL: ${str}`);
    }
    if (SPECIAL_PORTS[this.protocol] === this.port) this.port = "";
    this.searchParams = new KairoURLSearchParams(this.search);
    KairoURLSearchParams.bind(this.searchParams, () => {
      const q = this.searchParams.toString();
      this.search = q ? "?" + q : "";
    });
  }

  private parseAfterScheme(rest: string): void {
    if (rest.startsWith("//")) {
      rest = rest.slice(2);
      const end = rest.search(/[/?#]/);
      const auth = end < 0 ? rest : rest.slice(0, end);
      rest = end < 0 ? "" : rest.slice(end);
      const at = auth.lastIndexOf("@");
      let hostport = auth;
      if (at >= 0) {
        const cred = auth.slice(0, at);
        hostport = auth.slice(at + 1);
        const c = cred.indexOf(":");
        this.username = c < 0 ? cred : cred.slice(0, c);
        this.password = c < 0 ? "" : cred.slice(c + 1);
      }
      const pm = /^(\[[^\]]*\]|[^:]*)(?::(\d*))?$/.exec(hostport);
      this.hostname = (pm?.[1] ?? hostport).toLowerCase();
      this.port = pm?.[2] ?? "";
    }
    this.parsePath(rest || "/");
  }

  private parsePath(rest: string): void {
    const h = rest.indexOf("#");
    if (h >= 0) {
      this.hash = rest.slice(h);
      rest = rest.slice(0, h);
    } else this.hash = "";
    const q = rest.indexOf("?");
    if (q >= 0) {
      this.search = rest.length - q > 1 ? rest.slice(q) : "";
      rest = rest.slice(0, q);
    } else this.search = "";
    this.pathname = normalizePath(rest || "/");
    if (this.searchParams) KairoURLSearchParams.reparse(this.searchParams, this.search);
  }

  get host(): string {
    return this.port ? `${this.hostname}:${this.port}` : this.hostname;
  }
  set host(v: string) {
    const i = v.lastIndexOf(":");
    if (i > 0 && /^\d*$/.test(v.slice(i + 1))) {
      this.hostname = v.slice(0, i);
      this.port = v.slice(i + 1);
    } else this.hostname = v;
  }
  get origin(): string {
    return `${this.protocol}//${this.host}`;
  }
  get href(): string {
    const cred = this.username ? `${this.username}${this.password ? ":" + this.password : ""}@` : "";
    return `${this.protocol}//${cred}${this.host}${this.pathname}${this.search}${this.hash}`;
  }
  set href(v: string) {
    const u = new KairoURL(v);
    Object.assign(this, {
      protocol: u.protocol,
      username: u.username,
      password: u.password,
      hostname: u.hostname,
      port: u.port,
      pathname: u.pathname,
      search: u.search,
      hash: u.hash,
    });
    KairoURLSearchParams.reparse(this.searchParams, this.search);
  }
  toString(): string {
    return this.href;
  }
  toJSON(): string {
    return this.href;
  }
  static canParse(input: string, base?: string): boolean {
    try {
      new KairoURL(input, base);
      return true;
    } catch {
      return false;
    }
  }
  static parse(input: string, base?: string): KairoURL | null {
    try {
      return new KairoURL(input, base);
    } catch {
      return null;
    }
  }
}

const HEADER_NAME = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
const HEADER_VALUE_BAD = /[\r\n\0]/;

function checkHeader(name: string, value: string): [string, string] {
  const n = String(name);
  const v = String(value).trim();
  if (!HEADER_NAME.test(n)) throw new TypeError(`Invalid header name: ${JSON.stringify(n)}`);
  if (HEADER_VALUE_BAD.test(v)) throw new TypeError(`Invalid header value for ${n}`);
  return [n.toLowerCase(), v];
}

export class KairoHeaders {
  private map = new Map<string, string[]>();

  constructor(init?: HeadersInitLike) {
    if (!init) return;
    if (init instanceof KairoHeaders) init.forEach((v, k) => this.append(k, v));
    else if (Array.isArray(init)) for (const [k, v] of init) this.append(k, v);
    else for (const [k, v] of Object.entries(init)) this.append(k, v);
  }

  private static norm(k: string): string {
    return String(k).toLowerCase();
  }
  append(k: string, v: string): void {
    const [n, value] = checkHeader(k, v);
    const list = this.map.get(n);
    if (list) list.push(value);
    else this.map.set(n, [value]);
  }
  set(k: string, v: string): void {
    const [n, value] = checkHeader(k, v);
    this.map.set(n, [value]);
  }
  get(k: string): string | null {
    const list = this.map.get(KairoHeaders.norm(k));
    return list ? list.join(", ") : null;
  }
  getSetCookie(): string[] {
    return [...(this.map.get("set-cookie") ?? [])];
  }
  has(k: string): boolean {
    return this.map.has(KairoHeaders.norm(k));
  }
  delete(k: string): void {
    this.map.delete(KairoHeaders.norm(k));
  }
  forEach(fn: (v: string, k: string, h: KairoHeaders) => void): void {
    for (const [k] of [...this.map.entries()].sort()) fn(this.get(k)!, k, this);
  }
  *entries(): IterableIterator<[string, string]> {
    for (const k of [...this.map.keys()].sort()) {
      if (k === "set-cookie") for (const v of this.map.get(k)!) yield [k, v];
      else yield [k, this.get(k)!];
    }
  }
  keys(): IterableIterator<string> {
    return [...this.entries()].map(([k]) => k)[Symbol.iterator]();
  }
  values(): IterableIterator<string> {
    return [...this.entries()].map(([, v]) => v)[Symbol.iterator]();
  }
  [Symbol.iterator](): IterableIterator<[string, string]> {
    return this.entries();
  }
  rawEntries(): [string, string][] {
    const out: [string, string][] = [];
    for (const [k, vs] of this.map) for (const v of vs) out.push([k, v]);
    return out;
  }
}

export type BodyInitLike = string | Uint8Array | ArrayBuffer | ArrayBufferView | KairoURLSearchParams | null | undefined;

function bodyToBytes(body: BodyInitLike): { bytes: Uint8Array | null; type: string | null } {
  if (body === null || body === undefined) return { bytes: null, type: null };
  if (typeof body === "string") return { bytes: toBytes(body), type: "text/plain;charset=UTF-8" };
  if (body instanceof KairoURLSearchParams) return { bytes: toBytes(body.toString()), type: "application/x-www-form-urlencoded;charset=UTF-8" };
  return { bytes: toBytes(body as Uint8Array), type: null };
}

class KairoBody {
  protected _body: Uint8Array | null;
  bodyUsed = false;
  headers!: KairoHeaders;

  constructor(bytes: Uint8Array | null) {
    this._body = bytes;
  }

  get body(): Uint8Array | null {
    return this._body;
  }

  protected consume(): Uint8Array {
    if (this.bodyUsed) throw new TypeError("Body has already been consumed");
    this.bodyUsed = true;
    return this._body ?? new Uint8Array(0);
  }

  async arrayBuffer(): Promise<ArrayBuffer> {
    const b = this.consume();
    return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength) as ArrayBuffer;
  }
  async bytes(): Promise<Uint8Array> {
    return this.consume();
  }
  async text(): Promise<string> {
    return new TextDecoder().decode(this.consume());
  }
  async json<T = unknown>(): Promise<T> {
    return JSON.parse(await this.text()) as T;
  }
  async formData(): Promise<KairoURLSearchParams> {
    const type = this.headers.get("content-type") ?? "";
    if (!type.includes("application/x-www-form-urlencoded")) throw new TypeError("formData: only application/x-www-form-urlencoded is supported");
    return new KairoURLSearchParams(await this.text());
  }
}

export interface RequestInitLike {
  method?: string;
  headers?: HeadersInitLike;
  body?: BodyInitLike;
  signal?: KairoAbortSignal | null;
  redirect?: "follow" | "manual" | "error";
}

export class KairoRequest extends KairoBody {
  readonly url: string;
  readonly method: string;
  readonly signal: KairoAbortSignal;
  readonly redirect: "follow" | "manual" | "error";
  remoteAddr = "";

  constructor(input: string | KairoURL | KairoRequest, init: RequestInitLike = {}) {
    const src = input instanceof KairoRequest ? input : null;
    const { bytes, type } = bodyToBytes(init.body !== undefined ? init.body : (src?._body ?? null));
    super(bytes);
    this.url = src ? src.url : new KairoURL(String(input)).href;
    this.method = (init.method ?? src?.method ?? "GET").toUpperCase();
    this.headers = new KairoHeaders(init.headers ?? src?.headers);
    if (type && !this.headers.has("content-type")) this.headers.set("content-type", type);
    this.signal = init.signal ?? src?.signal ?? new KairoAbortSignal();
    this.redirect = init.redirect ?? src?.redirect ?? "follow";
    if (src) this.remoteAddr = src.remoteAddr;
  }

  clone(): KairoRequest {
    return new KairoRequest(this, { body: this._body });
  }
}

export interface ResponseInitLike {
  status?: number;
  statusText?: string;
  headers?: HeadersInitLike;
}

export const STATUS_TEXT: Record<number, string> = {
  100: "Continue",
  101: "Switching Protocols",
  200: "OK",
  201: "Created",
  202: "Accepted",
  204: "No Content",
  206: "Partial Content",
  301: "Moved Permanently",
  302: "Found",
  303: "See Other",
  304: "Not Modified",
  307: "Temporary Redirect",
  308: "Permanent Redirect",
  400: "Bad Request",
  401: "Unauthorized",
  403: "Forbidden",
  404: "Not Found",
  405: "Method Not Allowed",
  408: "Request Timeout",
  409: "Conflict",
  410: "Gone",
  411: "Length Required",
  413: "Payload Too Large",
  415: "Unsupported Media Type",
  422: "Unprocessable Entity",
  429: "Too Many Requests",
  431: "Request Header Fields Too Large",
  500: "Internal Server Error",
  501: "Not Implemented",
  502: "Bad Gateway",
  503: "Service Unavailable",
  504: "Gateway Timeout",
};

export class KairoResponse extends KairoBody {
  readonly status: number;
  readonly statusText: string;
  url = "";
  redirected = false;
  readonly type = "default";

  constructor(body: BodyInitLike = null, init: ResponseInitLike = {}) {
    const { bytes, type } = bodyToBytes(body);
    super(bytes);
    this.status = init.status ?? 200;
    this.statusText = init.statusText ?? STATUS_TEXT[this.status] ?? "";
    this.headers = new KairoHeaders(init.headers);
    if (type && !this.headers.has("content-type")) this.headers.set("content-type", type);
  }

  get ok(): boolean {
    return this.status >= 200 && this.status < 300;
  }

  clone(): KairoResponse {
    const r = new KairoResponse(this._body, { status: this.status, statusText: this.statusText, headers: this.headers });
    r.url = this.url;
    r.redirected = this.redirected;
    return r;
  }

  static json(data: unknown, init: ResponseInitLike = {}): KairoResponse {
    const h = new KairoHeaders(init.headers);
    if (!h.has("content-type")) h.set("content-type", "application/json");
    return new KairoResponse(JSON.stringify(data), { ...init, headers: h });
  }
  static redirect(url: string | KairoURL, status = 302): KairoResponse {
    return new KairoResponse(null, { status, headers: { location: String(url) } });
  }
  static error(): KairoResponse {
    return new KairoResponse(null, { status: 0 });
  }
}

if (typeof g.URL === "undefined") g.URL = KairoURL;
if (typeof g.URLSearchParams === "undefined") g.URLSearchParams = KairoURLSearchParams;
if (typeof g.Headers === "undefined") g.Headers = KairoHeaders;
if (typeof g.Request === "undefined") g.Request = KairoRequest;
if (typeof g.Response === "undefined") g.Response = KairoResponse;
if (typeof g.AbortController === "undefined") g.AbortController = KairoAbortController;
if (typeof g.AbortSignal === "undefined") g.AbortSignal = KairoAbortSignal;
if (typeof g.Event === "undefined") g.Event = KairoEvent;
if (typeof g.EventTarget === "undefined") g.EventTarget = KairoEventTarget;
if (typeof g.DOMException === "undefined") g.DOMException = DOMExceptionLike;

declare global {
  var URL: typeof KairoURL;
  type URL = KairoURL;
  var URLSearchParams: typeof KairoURLSearchParams;
  type URLSearchParams = KairoURLSearchParams;
  var Headers: typeof KairoHeaders;
  type Headers = KairoHeaders;
  var Request: typeof KairoRequest;
  type Request = KairoRequest;
  var Response: typeof KairoResponse;
  type Response = KairoResponse;
  var AbortController: typeof KairoAbortController;
  type AbortController = KairoAbortController;
  var AbortSignal: typeof KairoAbortSignal;
  type AbortSignal = KairoAbortSignal;
  var Event: typeof KairoEvent;
  type Event = KairoEvent;
  var EventTarget: typeof KairoEventTarget;
  type EventTarget = KairoEventTarget;
  var DOMException: typeof DOMExceptionLike;
  type RequestInit = RequestInitLike;
  type ResponseInit = ResponseInitLike;
  type HeadersInit = HeadersInitLike;
  type BodyInit = BodyInitLike;
}
