import { inspect, isDeepStrictEqual } from "./util";

export class AssertionError extends Error {
  code = "ERR_ASSERTION";
  actual: unknown;
  expected: unknown;
  operator?: string;
  constructor(opts: { message?: string; actual?: unknown; expected?: unknown; operator?: string }) {
    super(opts.message ?? `${inspect(opts.actual)} ${opts.operator ?? "=="} ${inspect(opts.expected)}`);
    this.name = "AssertionError";
    this.actual = opts.actual;
    this.expected = opts.expected;
    this.operator = opts.operator;
  }
}

function fail(message: string | Error | undefined, actual: unknown, expected: unknown, operator: string): never {
  if (message instanceof Error) throw message;
  throw new AssertionError({ message, actual, expected, operator });
}

function assert(value: unknown, message?: string | Error): asserts value {
  if (!value) fail(message ?? "The expression evaluated to a falsy value", value, true, "==");
}

assert.ok = assert;
assert.AssertionError = AssertionError;
assert.fail = (message?: string | Error): never => fail(message ?? "Failed", undefined, undefined, "fail");
assert.equal = (a: unknown, b: unknown, m?: string | Error) => {
  if (a != b) fail(m, a, b, "==");
};
assert.notEqual = (a: unknown, b: unknown, m?: string | Error) => {
  if (a == b) fail(m, a, b, "!=");
};
assert.strictEqual = (a: unknown, b: unknown, m?: string | Error) => {
  if (!Object.is(a, b)) fail(m, a, b, "strictEqual");
};
assert.notStrictEqual = (a: unknown, b: unknown, m?: string | Error) => {
  if (Object.is(a, b)) fail(m, a, b, "notStrictEqual");
};
assert.deepStrictEqual = (a: unknown, b: unknown, m?: string | Error) => {
  if (!isDeepStrictEqual(a, b)) fail(m, a, b, "deepStrictEqual");
};
assert.deepEqual = assert.deepStrictEqual;
assert.notDeepStrictEqual = (a: unknown, b: unknown, m?: string | Error) => {
  if (isDeepStrictEqual(a, b)) fail(m, a, b, "notDeepStrictEqual");
};
assert.throws = (fn: () => unknown, m?: string | Error) => {
  try {
    fn();
  } catch {
    return;
  }
  fail(m ?? "Missing expected exception", undefined, undefined, "throws");
};
assert.rejects = async (p: Promise<unknown> | (() => Promise<unknown>), m?: string | Error) => {
  try {
    await (typeof p === "function" ? p() : p);
  } catch {
    return;
  }
  fail(m ?? "Missing expected rejection", undefined, undefined, "rejects");
};
assert.match = (s: string, re: RegExp, m?: string | Error) => {
  if (!re.test(s)) fail(m, s, re, "match");
};

export const strict = assert;
export default assert;
