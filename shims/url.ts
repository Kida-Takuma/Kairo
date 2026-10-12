import { KairoURL, KairoURLSearchParams } from "../src/web";

export { KairoURL as URL, KairoURLSearchParams as URLSearchParams };

export interface UrlObject {
  protocol: string | null;
  slashes: boolean;
  auth: string | null;
  host: string | null;
  port: string | null;
  hostname: string | null;
  hash: string | null;
  search: string | null;
  query: string | Record<string, string> | null;
  pathname: string | null;
  path: string | null;
  href: string;
}

export function parse(input: string, parseQuery = false): UrlObject {
  const u = KairoURL.canParse(input) ? new KairoURL(input) : new KairoURL(input, "http://localhost");
  const relative = !KairoURL.canParse(input);
  const query = parseQuery ? Object.fromEntries(u.searchParams) : u.search ? u.search.slice(1) : null;
  return {
    protocol: relative ? null : u.protocol,
    slashes: !relative,
    auth: u.username ? (u.password ? `${u.username}:${u.password}` : u.username) : null,
    host: relative ? null : u.host,
    port: relative || !u.port ? null : u.port,
    hostname: relative ? null : u.hostname,
    hash: u.hash || null,
    search: u.search || null,
    query,
    pathname: u.pathname,
    path: u.pathname + u.search,
    href: relative ? u.pathname + u.search + u.hash : u.href,
  };
}

export function format(obj: Partial<UrlObject> | KairoURL | string): string {
  if (typeof obj === "string") return obj;
  if (obj instanceof KairoURL) return obj.href;
  const proto = obj.protocol ? (obj.protocol.endsWith(":") ? obj.protocol : obj.protocol + ":") : "";
  const auth = obj.auth ? `${obj.auth}@` : "";
  const host = obj.host ?? (obj.hostname ? obj.hostname + (obj.port ? `:${obj.port}` : "") : "");
  const search = obj.search ?? (obj.query ? (typeof obj.query === "string" ? `?${obj.query}` : `?${new KairoURLSearchParams(obj.query)}`) : "");
  return `${proto}${host || obj.slashes ? "//" : ""}${auth}${host}${obj.pathname ?? ""}${search}${obj.hash ?? ""}`;
}

export function resolve(from: string, to: string): string {
  return new KairoURL(to, from).href;
}

export function fileURLToPath(_url: string | KairoURL): string {
  throw new Error("fileURLToPath: there is no filesystem on this device");
}

export function pathToFileURL(path: string): KairoURL {
  return new KairoURL(`file://${path}`);
}

export function domainToASCII(domain: string): string {
  return domain.toLowerCase();
}

export function domainToUnicode(domain: string): string {
  return domain;
}

export default { URL: KairoURL, URLSearchParams: KairoURLSearchParams, parse, format, resolve, fileURLToPath, pathToFileURL, domainToASCII, domainToUnicode };
