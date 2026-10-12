export function escape(s: string): string {
  return encodeURIComponent(s);
}

export function unescape(s: string): string {
  try {
    return decodeURIComponent(s.replace(/\+/g, " "));
  } catch {
    return s;
  }
}

export function parse(str: string, sep = "&", eq = "="): Record<string, string | string[]> {
  const out: Record<string, string | string[]> = {};
  if (!str) return out;
  for (const part of str.replace(/^\?/, "").split(sep)) {
    if (!part) continue;
    const i = part.indexOf(eq);
    const k = unescape(i < 0 ? part : part.slice(0, i));
    const v = i < 0 ? "" : unescape(part.slice(i + eq.length));
    const cur = out[k];
    if (cur === undefined) out[k] = v;
    else if (Array.isArray(cur)) cur.push(v);
    else out[k] = [cur, v];
  }
  return out;
}

export function stringify(obj: Record<string, unknown>, sep = "&", eq = "="): string {
  const parts: string[] = [];
  for (const [k, v] of Object.entries(obj ?? {})) {
    if (Array.isArray(v)) for (const x of v) parts.push(`${escape(k)}${eq}${escape(String(x))}`);
    else if (v !== undefined) parts.push(`${escape(k)}${eq}${escape(v === null ? "" : String(v))}`);
  }
  return parts.join(sep);
}

export const decode = parse;
export const encode = stringify;
export default { parse, stringify, escape, unescape, decode, encode };
