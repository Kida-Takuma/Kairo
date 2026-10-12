import { format, inspect } from "../src/globals";

export { format, inspect };

export function promisify<T = unknown>(fn: (...args: any[]) => void): (...args: any[]) => Promise<T> {
  return (...args: any[]) =>
    new Promise<T>((resolve, reject) => {
      fn(...args, (err: unknown, value: T) => (err ? reject(err) : resolve(value)));
    });
}

export function callbackify(fn: (...args: any[]) => Promise<unknown>): (...args: any[]) => void {
  return (...args: any[]) => {
    const cb = args.pop() as (err: unknown, value?: unknown) => void;
    fn(...args).then(
      (v) => cb(null, v),
      (e) => cb(e),
    );
  };
}

export function inherits(ctor: any, superCtor: any): void {
  Object.setPrototypeOf(ctor.prototype, superCtor.prototype);
  ctor.super_ = superCtor;
}

export function deprecate<T extends (...args: any[]) => unknown>(fn: T, msg: string): T {
  let warned = false;
  return ((...args: unknown[]) => {
    if (!warned) {
      warned = true;
      console.warn("DeprecationWarning:", msg);
    }
    return fn(...args);
  }) as T;
}

export function isDeepStrictEqual(a: unknown, b: unknown): boolean {
  if (Object.is(a, b)) return true;
  if (typeof a !== "object" || typeof b !== "object" || a === null || b === null) return false;
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  const ka = Object.keys(a as object);
  const kb = Object.keys(b as object);
  if (ka.length !== kb.length) return false;
  return ka.every((k) => isDeepStrictEqual((a as Record<string, unknown>)[k], (b as Record<string, unknown>)[k]));
}

export const types = {
  isPromise: (v: unknown): v is Promise<unknown> => v instanceof Promise,
  isDate: (v: unknown): v is Date => v instanceof Date,
  isRegExp: (v: unknown): v is RegExp => v instanceof RegExp,
  isMap: (v: unknown): v is Map<unknown, unknown> => v instanceof Map,
  isSet: (v: unknown): v is Set<unknown> => v instanceof Set,
  isUint8Array: (v: unknown): v is Uint8Array => v instanceof Uint8Array,
  isArrayBuffer: (v: unknown): v is ArrayBuffer => v instanceof ArrayBuffer,
  isTypedArray: (v: unknown): boolean => ArrayBuffer.isView(v) && !(v instanceof DataView),
};

export const TextEncoder = globalThis.TextEncoder;
export const TextDecoder = globalThis.TextDecoder;

export default { format, inspect, promisify, callbackify, inherits, deprecate, isDeepStrictEqual, types, TextEncoder, TextDecoder };
