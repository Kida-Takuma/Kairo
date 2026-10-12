export function setTimeout<T = void>(ms = 0, value?: T): Promise<T> {
  return new Promise((resolve) => globalThis.setTimeout(() => resolve(value as T), ms));
}
export function setImmediate<T = void>(value?: T): Promise<T> {
  return new Promise((resolve) => globalThis.setImmediate(() => resolve(value as T)));
}
export async function* setInterval<T = void>(ms = 0, value?: T): AsyncGenerator<T> {
  for (;;) {
    await setTimeout(ms);
    yield value as T;
  }
}
export default { setTimeout, setImmediate, setInterval };
