import { CRLFCRLF, indexOfSeq, latin1 } from "./bytes";
import { KairoHeaders } from "./web";

export interface ParsedHead {
  startLine: string;
  headers: KairoHeaders;
  bodyStart: number;
}

export function parseHead(buf: Uint8Array): ParsedHead | null {
  const end = indexOfSeq(buf, CRLFCRLF);
  if (end < 0) return null;
  const text = latin1(buf.subarray(0, end));
  const lines = text.split("\r\n");
  const startLine = lines.shift() ?? "";
  const headers = new KairoHeaders();
  for (const line of lines) {
    const i = line.indexOf(":");
    if (i <= 0) continue;
    headers.append(line.slice(0, i).trim(), decodeLatin1Utf8(line.slice(i + 1).trim()));
  }
  return { startLine, headers, bodyStart: end + 4 };
}

function decodeLatin1Utf8(s: string): string {
  const bytes = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) bytes[i] = s.charCodeAt(i) & 0xff;
  return new TextDecoder().decode(bytes);
}

export interface ChunkedState {
  done: boolean;
  consumed: number;
  data: Uint8Array[];
}

export function parseChunked(buf: Uint8Array): ChunkedState {
  const out: Uint8Array[] = [];
  let pos = 0;
  for (;;) {
    const lineEnd = indexOfSeq(buf, new Uint8Array([13, 10]), pos);
    if (lineEnd < 0) return { done: false, consumed: pos, data: out };
    const sizeStr = latin1(buf.subarray(pos, lineEnd)).split(";")[0]!.trim();
    const size = parseInt(sizeStr, 16);
    if (Number.isNaN(size)) throw new Error("bad chunk size");
    const dataStart = lineEnd + 2;
    if (size === 0) {
      const trailerEnd = indexOfSeq(buf, CRLFCRLF, pos);
      if (trailerEnd < 0) {
        if (buf.length >= dataStart + 2) return { done: true, consumed: dataStart + 2, data: out };
        return { done: false, consumed: pos, data: out };
      }
      return { done: true, consumed: trailerEnd + 4, data: out };
    }
    if (buf.length < dataStart + size + 2) return { done: false, consumed: pos, data: out };
    out.push(buf.subarray(dataStart, dataStart + size));
    pos = dataStart + size + 2;
  }
}

export function contentLength(headers: KairoHeaders): number | null {
  const raw = headers.get("content-length");
  if (raw === null) return null;
  if (!/^\d{1,15}$/.test(raw)) return NaN;
  return Number(raw);
}

export function httpDate(d = new Date()): string {
  return d.toUTCString();
}
