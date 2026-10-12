import "./globals";
import { concat, toBytes } from "./bytes";
import { TcpSocket } from "./net";
import { parseChunked, parseHead } from "./http_common";
import { KairoHeaders, KairoRequest, KairoResponse, KairoURL, type RequestInitLike } from "./web";

const g = globalThis as Record<string, unknown>;
const MAX_REDIRECTS = 5;

export interface FetchOptions extends RequestInitLike {
  timeoutMs?: number;
}

async function once(req: KairoRequest, timeoutMs: number): Promise<KairoResponse> {
  const url = new KairoURL(req.url);
  if (url.protocol !== "http:" && url.protocol !== "https:") throw new TypeError(`fetch: unsupported protocol ${url.protocol}`);
  const tls = url.protocol === "https:";
  const port = url.port ? Number(url.port) : tls ? 443 : 80;
  const socket = await TcpSocket.connect(url.hostname, port, { tls, timeoutMs });
  return new Promise<KairoResponse>((resolve, reject) => {
    const chunks: Uint8Array[] = [];
    let total = 0;
    let head: ReturnType<typeof parseHead> = null;
    let status = 0;
    let statusText = "";
    let settled = false;
    const timer = timeoutMs ? setTimeout(() => fail(new Error("fetch: timeout")), timeoutMs) : undefined;
    const abort = () => fail(req.signal.reason instanceof Error ? req.signal.reason : new Error("fetch: aborted"));
    req.signal.addEventListener("abort", abort, { once: true });

    function cleanup() {
      settled = true;
      if (timer !== undefined) clearTimeout(timer);
      req.signal.removeEventListener("abort", abort);
      socket.destroy();
    }
    function fail(e: Error) {
      if (settled) return;
      cleanup();
      reject(e);
    }
    function finish(body: Uint8Array | null) {
      if (settled) return;
      cleanup();
      const res = new KairoResponse(body, { status, statusText, headers: head!.headers });
      res.url = req.url;
      resolve(res);
    }
    function tryParse() {
      const buf = concat(chunks, total);
      if (!head) {
        head = parseHead(buf);
        if (!head) return;
        const m = /^HTTP\/1\.[01] (\d{3}) ?(.*)$/.exec(head.startLine);
        if (!m) return fail(new Error(`fetch: bad status line '${head.startLine}'`));
        status = Number(m[1]);
        statusText = m[2] ?? "";
      }
      const body = buf.subarray(head.bodyStart);
      if (status === 204 || status === 304 || req.method === "HEAD") return finish(null);
      const te = head.headers.get("transfer-encoding");
      if (te && te.toLowerCase().includes("chunked")) {
        const st = parseChunked(body);
        if (st.done) finish(concat(st.data));
        return;
      }
      const cl = head.headers.get("content-length");
      if (cl !== null) {
        const n = Number(cl);
        if (body.length >= n) finish(body.subarray(0, n));
      }
    }

    socket.on("data", (c: Uint8Array) => {
      chunks.push(c);
      total += c.length;
      try {
        tryParse();
      } catch (e) {
        fail(e as Error);
      }
    });
    socket.on("close", (hadError: boolean) => {
      if (settled) return;
      if (hadError) return fail(new Error("fetch: connection error"));
      if (!head) return fail(new Error("fetch: connection closed before response"));
      const buf = concat(chunks, total);
      finish(buf.subarray(head.bodyStart));
    });
    socket.on("error", () => {});

    const headers = new KairoHeaders(req.headers);
    if (!headers.has("host")) headers.set("host", url.host);
    if (!headers.has("user-agent")) headers.set("user-agent", "kairo/0.0.1");
    if (!headers.has("accept")) headers.set("accept", "*/*");
    headers.set("connection", "close");
    const body = req.body;
    if (body && !headers.has("content-length")) headers.set("content-length", String(body.length));
    let text = `${req.method} ${url.pathname}${url.search} HTTP/1.1\r\n`;
    for (const [k, v] of headers.rawEntries()) text += `${k}: ${v}\r\n`;
    text += "\r\n";
    socket.write(toBytes(text));
    if (body) socket.write(body);
  });
}

export async function fetch(input: string | KairoURL | KairoRequest, init: FetchOptions = {}): Promise<KairoResponse> {
  let req = new KairoRequest(input, init);
  const timeoutMs = init.timeoutMs ?? 30000;
  let redirected = false;
  for (let i = 0; i <= MAX_REDIRECTS; i++) {
    req.signal.throwIfAborted();
    const res = await once(req, timeoutMs);
    res.redirected = redirected;
    const loc = res.headers.get("location");
    if (!loc || ![301, 302, 303, 307, 308].includes(res.status) || req.redirect !== "follow") {
      if (loc && req.redirect === "error" && [301, 302, 303, 307, 308].includes(res.status)) throw new TypeError("fetch: redirect not allowed");
      return res;
    }
    const next = new KairoURL(loc, req.url).href;
    const keepMethod = res.status === 307 || res.status === 308;
    req = new KairoRequest(next, {
      method: keepMethod ? req.method : "GET",
      headers: req.headers,
      body: keepMethod ? req.body : null,
      signal: req.signal,
      redirect: req.redirect,
    });
    redirected = true;
  }
  throw new TypeError("fetch: too many redirects");
}

if (typeof g.fetch === "undefined") g.fetch = fetch;

declare global {
  function fetch(input: string | KairoURL | KairoRequest, init?: FetchOptions): Promise<KairoResponse>;
}
