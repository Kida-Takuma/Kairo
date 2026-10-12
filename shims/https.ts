import * as http from "./http";
import type { IncomingMessage, RequestOptions } from "./http";
import { KairoURL } from "../src/web";

export { Agent, ClientRequest, IncomingMessage, METHODS, STATUS_CODES, Server, ServerResponse, createServer, globalAgent } from "./http";

function withHttps(input: string | KairoURL | RequestOptions): string | KairoURL | RequestOptions {
  if (typeof input === "string" || input instanceof KairoURL) return input;
  return { ...input, protocol: input.protocol ?? "https:" };
}

export function request(
  input: string | KairoURL | RequestOptions,
  optsOrCb?: RequestOptions | ((res: IncomingMessage) => void),
  cb?: (res: IncomingMessage) => void,
) {
  return http.request(withHttps(input), optsOrCb, cb);
}

export function get(
  input: string | KairoURL | RequestOptions,
  optsOrCb?: RequestOptions | ((res: IncomingMessage) => void),
  cb?: (res: IncomingMessage) => void,
) {
  return http.get(withHttps(input), optsOrCb, cb);
}

export default { ...http.default, request, get };
