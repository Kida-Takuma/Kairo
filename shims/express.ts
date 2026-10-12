import { IncomingMessage, Server, ServerResponse, createServer } from "./http";
import { KairoURLSearchParams } from "../src/web";

export type NextFunction = (err?: unknown) => void;
export interface Request extends IncomingMessage {
  app: Application;
  res: Response;
  baseUrl: string;
  originalUrl: string;
  path: string;
  query: Record<string, string | string[]>;
  params: Record<string, string>;
  body: unknown;
  ip: string;
  protocol: string;
  secure: boolean;
  hostname: string;
  xhr: boolean;
  get(name: string): string | undefined;
  header(name: string): string | undefined;
  is(type: string): string | false | null;
  accepts(...types: string[]): string | false;
}
export interface Response extends ServerResponse {
  app: Application;
  req: Request;
  status(code: number): Response;
  sendStatus(code: number): Response;
  send(body?: unknown): Response;
  json(body: unknown): Response;
  set(field: string | Record<string, string>, value?: string): Response;
  header(field: string | Record<string, string>, value?: string): Response;
  get(field: string): string | undefined;
  type(t: string): Response;
  location(url: string): Response;
  redirect(statusOrUrl: number | string, url?: string): void;
  append(field: string, value: string | string[]): Response;
  cookie(name: string, value: string, opts?: CookieOptions): Response;
  clearCookie(name: string, opts?: CookieOptions): Response;
  vary(field: string): Response;
  links(links: Record<string, string>): Response;
}
export interface CookieOptions {
  maxAge?: number;
  expires?: Date;
  httpOnly?: boolean;
  path?: string;
  domain?: string;
  secure?: boolean;
  sameSite?: "strict" | "lax" | "none" | boolean;
}
export type RequestHandler = (req: Request, res: Response, next: NextFunction) => unknown;
export type ErrorRequestHandler = (err: unknown, req: Request, res: Response, next: NextFunction) => unknown;
type AnyHandler = RequestHandler | ErrorRequestHandler | Router | Application;

interface Layer {
  method: string | null;
  regexp: RegExp | null;
  keys: string[];
  prefix: boolean;
  handle: AnyHandler;
}

const METHODS = ["get", "post", "put", "patch", "delete", "options", "head"] as const;
type Method = (typeof METHODS)[number];

const MIME: Record<string, string> = {
  html: "text/html; charset=utf-8",
  json: "application/json; charset=utf-8",
  text: "text/plain; charset=utf-8",
  txt: "text/plain; charset=utf-8",
  js: "application/javascript; charset=utf-8",
  css: "text/css; charset=utf-8",
  xml: "application/xml",
  png: "image/png",
  jpg: "image/jpeg",
  svg: "image/svg+xml",
  bin: "application/octet-stream",
  form: "application/x-www-form-urlencoded",
};

function compile(path: string | RegExp, prefix: boolean): { regexp: RegExp; keys: string[] } {
  const keys: string[] = [];
  if (path instanceof RegExp) return { regexp: path, keys };
  if (path === "/" && prefix) return { regexp: /^(?=\/|$)/, keys };
  let src = path.replace(/\/+$/, "").replace(/[.+^${}()|[\]\\]/g, "\\$&");
  src = src.replace(/\\\*|\*/g, () => {
    keys.push(String(keys.length));
    return "(.*)";
  });
  src = src.replace(/:(\w+)(\?)?/g, (_m, name: string, opt?: string) => {
    keys.push(name);
    return opt ? "(?:/)?([^/]+)?" : "([^/]+)";
  });
  src = src.replace(/\/\(\?:\/\)\?/g, "(?:/");
  return { regexp: new RegExp(`^${src}${prefix ? "(?=/|$)" : "/?$"}`, "i"), keys };
}

function decode(s: string): string {
  try {
    return decodeURIComponent(s);
  } catch {
    return s;
  }
}

export class Router {
  readonly stack: Layer[] = [];
  readonly isRouter = true;

  private add(method: string | null, path: string | RegExp | AnyHandler, handlers: AnyHandler[], prefix: boolean): this {
    let p: string | RegExp = "/";
    if (typeof path === "string" || path instanceof RegExp) p = path;
    else handlers.unshift(path);
    const { regexp, keys } = compile(p, prefix);
    for (const h of handlers) this.stack.push({ method, regexp: p === "/" && prefix ? null : regexp, keys, prefix, handle: h });
    return this;
  }

  use(...handlers: RequestHandler[]): this;
  use(handler: ErrorRequestHandler): this;
  use(...routers: (Router | Application)[]): this;
  use(path: string | RegExp, ...handlers: (RequestHandler | Router | Application)[]): this;
  use(path: string | RegExp | AnyHandler, ...handlers: AnyHandler[]): this {
    return this.add(null, path, handlers, true);
  }
  all(path: string | RegExp, ...handlers: RequestHandler[]): this {
    return this.add(null, path, handlers, false);
  }
  route(path: string): Record<Method, (...h: RequestHandler[]) => Router> {
    const r = {} as Record<Method, (...h: RequestHandler[]) => Router>;
    for (const m of METHODS) r[m] = (...h) => this.add(m.toUpperCase(), path, h, false);
    return r;
  }

  handle(req: Request, res: Response, done: NextFunction): void {
    let idx = 0;
    const baseUrl = req.baseUrl;
    const url = req.url;
    const params = req.params;
    const next: NextFunction = (err?: unknown) => {
      req.baseUrl = baseUrl;
      req.url = url;
      req.params = params;
      req.path = url.split("?")[0] ?? "/";
      if (err === "route" || err === "router") err = undefined;
      const layer = this.stack[idx++];
      if (!layer) return done(err);
      const isErrorHandler = typeof layer.handle === "function" && !(layer.handle as Router).isRouter && layer.handle.length === 4;
      if ((err !== undefined) !== isErrorHandler && !(layer.handle as Router).isRouter) return next(err);
      if (err !== undefined && (layer.handle as Router).isRouter) return next(err);
      if (layer.method && layer.method !== req.method && !(layer.method === "GET" && req.method === "HEAD")) return next(err);
      let matched = "";
      if (layer.regexp) {
        const m = layer.regexp.exec(req.path);
        if (!m) return next(err);
        matched = m[0];
        const p: Record<string, string> = { ...params };
        layer.keys.forEach((k, i) => {
          const v = m[i + 1];
          if (v !== undefined) p[k] = decode(v);
        });
        req.params = p;
      }
      const target = layer.handle;
      if ((target as Router).isRouter) {
        if (layer.prefix && matched) {
          req.baseUrl = baseUrl + matched;
          req.url = url.slice(matched.length) || "/";
          if (!req.url.startsWith("/")) req.url = "/" + req.url;
          req.path = req.url.split("?")[0] ?? "/";
        }
        return (target as Router).handle(req, res, next);
      }
      try {
        const out = isErrorHandler ? (target as ErrorRequestHandler)(err, req, res, next) : (target as RequestHandler)(req, res, next);
        if (out && typeof (out as Promise<unknown>).then === "function") (out as Promise<unknown>).then(undefined, next);
      } catch (e) {
        next(e);
      }
    };
    next();
  }
}

for (const m of METHODS) {
  (Router.prototype as unknown as Record<string, unknown>)[m] = function (this: Router, path: string | RegExp, ...handlers: RequestHandler[]) {
    return (this as unknown as { add(method: string, path: string | RegExp, h: AnyHandler[], prefix: boolean): Router }).add(
      m.toUpperCase(),
      path,
      handlers,
      false,
    );
  };
}

export interface Router {
  get(path: string | RegExp, ...handlers: RequestHandler[]): this;
  post(path: string | RegExp, ...handlers: RequestHandler[]): this;
  put(path: string | RegExp, ...handlers: RequestHandler[]): this;
  patch(path: string | RegExp, ...handlers: RequestHandler[]): this;
  delete(path: string | RegExp, ...handlers: RequestHandler[]): this;
  options(path: string | RegExp, ...handlers: RequestHandler[]): this;
  head(path: string | RegExp, ...handlers: RequestHandler[]): this;
}

function serializeCookie(name: string, value: string, o: CookieOptions = {}): string {
  let s = `${encodeURIComponent(name)}=${encodeURIComponent(value)}`;
  if (o.maxAge !== undefined) s += `; Max-Age=${Math.floor(o.maxAge / 1000)}`;
  if (o.expires) s += `; Expires=${o.expires.toUTCString()}`;
  s += `; Path=${o.path ?? "/"}`;
  if (o.domain) s += `; Domain=${o.domain}`;
  if (o.httpOnly) s += "; HttpOnly";
  if (o.secure) s += "; Secure";
  if (o.sameSite) s += `; SameSite=${o.sameSite === true ? "Strict" : o.sameSite[0]!.toUpperCase() + o.sameSite.slice(1)}`;
  return s;
}

function augment(app: Application, rawReq: IncomingMessage, rawRes: ServerResponse): { req: Request; res: Response } {
  const req = rawReq as Request;
  const res = rawRes as Response;
  const q = req.url.indexOf("?");
  req.app = app;
  req.res = res;
  res.app = app;
  res.req = req;
  req.baseUrl = "";
  req.originalUrl = req.url;
  req.path = q < 0 ? req.url : req.url.slice(0, q);
  req.params = {};
  req.body = undefined;
  const query: Record<string, string | string[]> = {};
  for (const [k, v] of new KairoURLSearchParams(q < 0 ? "" : req.url.slice(q))) {
    const cur = query[k];
    if (cur === undefined) query[k] = v;
    else if (Array.isArray(cur)) cur.push(v);
    else query[k] = [cur, v];
  }
  req.query = query;
  req.ip = req.socket.remoteAddress;
  req.protocol = "http";
  req.secure = false;
  req.hostname = (req.headers.host ?? "").split(":")[0] ?? "";
  req.xhr = (req.headers["x-requested-with"] ?? "").toLowerCase() === "xmlhttprequest";
  req.get = req.header = (name: string) => req.headers[name.toLowerCase()];
  req.is = (type: string) => {
    const ct = (req.headers["content-type"] ?? "").split(";")[0]!.trim();
    if (!ct) return null;
    const full = MIME[type]?.split(";")[0] ?? (type.includes("/") ? type : `application/${type}`);
    return ct === full || (type.endsWith("/*") && ct.startsWith(type.slice(0, -1))) || ct.endsWith(`/${type}`) || ct.endsWith(`+${type}`) ? ct : false;
  };
  req.accepts = (...types: string[]) => {
    const accept = req.headers.accept ?? "*/*";
    for (const t of types) {
      const full = MIME[t]?.split(";")[0] ?? t;
      if (accept.includes("*/*") || accept.includes(full) || accept.includes(`${full.split("/")[0]}/*`)) return t;
    }
    return false;
  };

  res.status = (code: number) => {
    res.statusCode = code;
    return res;
  };
  res.set = res.header = (field: string | Record<string, string>, value?: string) => {
    if (typeof field === "object") for (const [k, v] of Object.entries(field)) res.setHeader(k, v);
    else res.setHeader(field, value ?? "");
    return res;
  };
  res.get = (field: string) => {
    const v = res.getHeader(field);
    return Array.isArray(v) ? v.join(", ") : v === undefined ? undefined : String(v);
  };
  res.type = (t: string) => res.set("Content-Type", MIME[t] ?? (t.includes("/") ? t : `application/${t}`));
  res.send = (body?: unknown) => {
    if (body === undefined || body === null) return (res.end(), res);
    if (typeof body === "object" && !(body instanceof Uint8Array)) return res.json(body);
    if (typeof body === "string" && !res.hasHeader("content-type")) res.type("html");
    if (body instanceof Uint8Array && !res.hasHeader("content-type")) res.type("bin");
    if (typeof body === "number") {
      res.statusCode = body;
      body = STATUS_TEXTS[body] ?? String(body);
    }
    res.end(body as string | Uint8Array);
    return res;
  };
  res.json = (body: unknown) => {
    if (!res.hasHeader("content-type")) res.type("json");
    res.end(JSON.stringify(body));
    return res;
  };
  res.sendStatus = (code: number) =>
    res
      .status(code)
      .type("text")
      .send(STATUS_TEXTS[code] ?? String(code));
  res.location = (url: string) => res.set("Location", url);
  res.redirect = (a: number | string, b?: string) => {
    const status = typeof a === "number" ? a : 302;
    const url = typeof a === "number" ? (b ?? "/") : a;
    res.status(status).location(url).type("text").end(`Redirecting to ${url}`);
  };
  res.append = (field: string, value: string | string[]) => {
    const prev = res.getHeader(field);
    const list = [...(prev === undefined ? [] : Array.isArray(prev) ? prev : [String(prev)]), ...(Array.isArray(value) ? value : [value])];
    res.setHeader(field, list);
    return res;
  };
  res.cookie = (name: string, value: string, opts?: CookieOptions) => res.append("Set-Cookie", serializeCookie(name, value, opts));
  res.clearCookie = (name: string, opts?: CookieOptions) => res.cookie(name, "", { ...opts, expires: new Date(0), maxAge: undefined });
  res.vary = (field: string) => res.append("Vary", field);
  res.links = (links: Record<string, string>) =>
    res.set(
      "Link",
      Object.entries(links)
        .map(([rel, url]) => `<${url}>; rel="${rel}"`)
        .join(", "),
    );
  return { req, res };
}

const STATUS_TEXTS: Record<number, string> = {
  200: "OK",
  201: "Created",
  204: "No Content",
  301: "Moved Permanently",
  302: "Found",
  304: "Not Modified",
  400: "Bad Request",
  401: "Unauthorized",
  403: "Forbidden",
  404: "Not Found",
  405: "Method Not Allowed",
  409: "Conflict",
  422: "Unprocessable Entity",
  500: "Internal Server Error",
};

export interface Application extends Router {
  (req: IncomingMessage, res: ServerResponse, next?: NextFunction): void;
  locals: Record<string, unknown>;
  settings: Record<string, unknown>;
  mountpath: string;
  listen(port?: number, hostOrCb?: string | (() => void), cb?: () => void): Server;
  set(name: string, value?: unknown): unknown;
  enable(name: string): Application;
  disable(name: string): Application;
  enabled(name: string): boolean;
  disabled(name: string): boolean;
  handle(req: IncomingMessage, res: ServerResponse, next?: NextFunction): void;
}

function finalHandler(req: Request, res: Response, err?: unknown): void {
  if (err !== undefined) {
    console.error("express: unhandled error:", err);
    if (res.finished) return;
    const status = typeof (err as { status?: number })?.status === "number" ? (err as { status: number }).status : 500;
    res
      .status(status)
      .type("text")
      .end(status === 500 ? "Internal Server Error" : String((err as Error)?.message ?? err));
    return;
  }
  if (res.finished) return;
  res.status(404).type("text").end(`Cannot ${req.method} ${req.originalUrl}`);
}

function express(): Application {
  const router = new Router();
  const app = function (rawReq: IncomingMessage, rawRes: ServerResponse, next?: NextFunction) {
    const { req, res } = augment(app, rawReq, rawRes);
    router.handle(req, res, (err) => (next ? next(err) : finalHandler(req, res, err)));
  } as unknown as Application;
  Object.setPrototypeOf(app, Object.assign(Object.create(Function.prototype), Router.prototype));
  Object.defineProperty(app, "stack", { get: () => router.stack });
  Object.defineProperty(app, "isRouter", { value: true });
  (app as unknown as Record<string, unknown>).add = (router as unknown as Record<string, unknown>).add;
  for (const k of ["use", "all", "route", "handle", ...METHODS]) {
    (app as unknown as Record<string, unknown>)[k] = (...args: unknown[]) => {
      const r = (router as unknown as Record<string, (...a: unknown[]) => unknown>)[k]!(...args);
      return r === router ? app : r;
    };
  }
  (app as unknown as Record<string, unknown>).handle = (rawReq: IncomingMessage, rawRes: ServerResponse, next?: NextFunction) => app(rawReq, rawRes, next);
  app.locals = {};
  app.settings = { env: process.env.NODE_ENV ?? "development", "x-powered-by": false, etag: false };
  app.mountpath = "/";
  app.set = (name: string, value?: unknown) => {
    if (value === undefined) return app.settings[name];
    app.settings[name] = value;
    return app;
  };
  app.enable = (name: string) => (app.set(name, true), app);
  app.disable = (name: string) => (app.set(name, false), app);
  app.enabled = (name: string) => !!app.settings[name];
  app.disabled = (name: string) => !app.settings[name];
  app.listen = (port?: number, hostOrCb?: string | (() => void), cb?: () => void) => createServer(app).listen(port ?? 80, hostOrCb, cb);
  return app;
}

function readText(req: Request): string {
  return new TextDecoder().decode(req.rawBody);
}

express.json =
  (opts: { strict?: boolean; type?: string } = {}): RequestHandler =>
  (req, res, next) => {
    if (req.body !== undefined || !req.is(opts.type ?? "json") || !req.rawBody.length) return next();
    try {
      req.body = JSON.parse(readText(req));
      next();
    } catch (e) {
      res
        .status(400)
        .type("text")
        .end(`invalid JSON: ${(e as Error).message}`);
    }
  };
express.urlencoded =
  (_opts: { extended?: boolean } = {}): RequestHandler =>
  (req, _res, next) => {
    if (req.body !== undefined || !req.is("application/x-www-form-urlencoded")) return next();
    const body: Record<string, string | string[]> = {};
    for (const [k, v] of new KairoURLSearchParams(readText(req))) {
      const cur = body[k];
      if (cur === undefined) body[k] = v;
      else if (Array.isArray(cur)) cur.push(v);
      else body[k] = [cur, v];
    }
    req.body = body;
    next();
  };
express.text =
  (opts: { type?: string } = {}): RequestHandler =>
  (req, _res, next) => {
    if (req.body === undefined && req.is(opts.type ?? "text/plain")) req.body = readText(req);
    next();
  };
express.raw =
  (opts: { type?: string } = {}): RequestHandler =>
  (req, _res, next) => {
    if (req.body === undefined && req.is(opts.type ?? "application/octet-stream")) req.body = req.rawBody;
    next();
  };
express.Router = (): Router => new Router();
express.static = (_root: string): RequestHandler => {
  throw new Error("express.static: there is no filesystem on this device; serve assets from code instead");
};
express.application = Router.prototype;

export { express };
export default express;
