import "./globals";
import { concat, toBytes } from "./bytes";
import { TcpServer, TcpSocket } from "./net";
import { contentLength, httpDate, parseChunked, parseHead } from "./http_common";
import { KairoRequest, KairoResponse, STATUS_TEXT } from "./web";

export type Handler = (request: KairoRequest, info: { remoteAddr: string; remotePort: number }) => KairoResponse | Promise<KairoResponse>;

export interface ServeOptions {
  port?: number;
  hostname?: string;
  idleTimeoutMs?: number;
  maxBodyBytes?: number;
  maxHeaderBytes?: number;
  fetch?: Handler;
  onListen?: (info: { port: number; hostname: string }) => void;
  onError?: (err: unknown) => KairoResponse | Promise<KairoResponse>;
}

export interface HttpServer {
  readonly port: number;
  close(): void;
}

export function encodeResponse(res: KairoResponse, keepAlive: boolean, head = false): Uint8Array {
  const body = res.body;
  const status = res.status || 200;
  let text = `HTTP/1.1 ${status} ${res.statusText || STATUS_TEXT[status] || ""}\r\n`;
  const seen = new Set<string>();
  for (const [k, v] of res.headers.rawEntries()) {
    seen.add(k);
    text += `${k}: ${v}\r\n`;
  }
  if (!seen.has("date")) text += `date: ${httpDate()}\r\n`;
  if (!seen.has("content-length") && !seen.has("transfer-encoding") && status !== 204 && status !== 304)
    text += `content-length: ${body ? body.length : 0}\r\n`;
  if (!seen.has("connection")) text += `connection: ${keepAlive ? "keep-alive" : "close"}\r\n`;
  text += "\r\n";
  const headBytes = toBytes(text);
  if (head || !body || status === 204 || status === 304) return headBytes;
  return concat([headBytes, body]);
}

export function serve(options: ServeOptions | Handler, handler?: Handler): HttpServer {
  const opts: ServeOptions = typeof options === "function" ? { fetch: options } : options;
  const fetchHandler = handler ?? opts.fetch;
  if (!fetchHandler) throw new TypeError("serve: a fetch handler is required");
  const handle: Handler = fetchHandler;
  const port = opts.port ?? 80;
  const idle = opts.idleTimeoutMs ?? 15000;
  const maxBody = opts.maxBodyBytes ?? 256 * 1024;
  const maxHeader = opts.maxHeaderBytes ?? 16 * 1024;
  const server = new TcpServer();

  server.on("connection", (socket: TcpSocket) => {
    let buf: Uint8Array = new Uint8Array(0);
    let busy = false;
    let idleTimer: number | undefined;
    const armIdle = () => {
      if (idleTimer !== undefined) clearTimeout(idleTimer);
      idleTimer = setTimeout(() => socket.destroy(), idle);
    };
    armIdle();
    socket.on("close", () => {
      if (idleTimer !== undefined) clearTimeout(idleTimer);
    });
    socket.on("error", () => {});
    function reject(status: number, message: string): void {
      if (!socket.closed) socket.write(encodeResponse(new KairoResponse(message, { status }), false));
      socket.end();
    }

    socket.on("data", (chunk: Uint8Array) => {
      buf = buf.length ? concat([buf, chunk]) : chunk;
      if (buf.length > maxBody + maxHeader) {
        reject(413, "payload too large");
        return;
      }
      armIdle();
      void pump();
    });

    async function pump(): Promise<void> {
      if (busy) return;
      busy = true;
      try {
        for (;;) {
          let head: ReturnType<typeof parseHead>;
          try {
            head = parseHead(buf);
          } catch {
            return reject(400, "bad request");
          }
          if (!head) {
            if (buf.length > maxHeader) return reject(431, "request header fields too large");
            break;
          }
          if (head.bodyStart > maxHeader) return reject(431, "request header fields too large");
          const m = /^([A-Z]+) (\S+) HTTP\/1\.([01])$/.exec(head.startLine);
          if (!m) return reject(400, "bad request");
          const [, method, target, minor] = m;
          let body: Uint8Array | null = null;
          let consumed = head.bodyStart;
          const te = head.headers.get("transfer-encoding");
          const cl = contentLength(head.headers);
          if (te && te.toLowerCase().includes("chunked")) {
            let st: ReturnType<typeof parseChunked>;
            try {
              st = parseChunked(buf.subarray(head.bodyStart));
            } catch {
              return reject(400, "bad request");
            }
            if (!st.done) break;
            body = concat(st.data);
            consumed += st.consumed;
          } else if (cl !== null) {
            if (Number.isNaN(cl)) return reject(400, "bad request");
            if (cl > maxBody) return reject(413, "payload too large");
            if (buf.length < head.bodyStart + cl) break;
            body = buf.subarray(head.bodyStart, head.bodyStart + cl);
            consumed += cl;
          }
          buf = buf.subarray(consumed);
          const connHdr = (head.headers.get("connection") ?? "").toLowerCase();
          const keepAlive = connHdr.includes("close") ? false : minor === "1" || connHdr.includes("keep-alive");
          const hostHeader = head.headers.get("host") ?? "";
          const host = /^[A-Za-z0-9.\-[\]:]+$/.test(hostHeader) ? hostHeader : `${opts.hostname ?? "localhost"}:${port}`;
          let req: KairoRequest;
          try {
            req = new KairoRequest(`http://${host}${target!.startsWith("/") ? target : "/" + target}`, {
              method,
              headers: head.headers,
              body: method === "GET" || method === "HEAD" ? null : body,
            });
          } catch {
            return reject(400, "bad request");
          }
          req.remoteAddr = socket.remoteAddr;
          let res: KairoResponse;
          try {
            res = await handle(req, { remoteAddr: socket.remoteAddr, remotePort: socket.remotePort });
            if (!(res instanceof KairoResponse)) throw new TypeError("handler did not return a Response");
          } catch (e) {
            if (opts.onError) res = await opts.onError(e);
            else {
              console.error("http: handler error:", e);
              res = new KairoResponse("internal server error", { status: 500 });
            }
          }
          if (socket.closed) return;
          socket.write(encodeResponse(res, keepAlive, method === "HEAD"));
          if (!keepAlive) {
            socket.end();
            return;
          }
          armIdle();
        }
      } finally {
        busy = false;
      }
    }
  });

  server.listen(port, () => opts.onListen?.({ port, hostname: opts.hostname ?? "0.0.0.0" }));
  return {
    get port() {
      return port;
    },
    close: () => server.close(),
  };
}
