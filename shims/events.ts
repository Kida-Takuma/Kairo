type Listener = (...args: any[]) => void;

export class EventEmitter {
  private _events = new Map<string | symbol, Listener[]>();
  private _max = 10;
  static defaultMaxListeners = 10;

  on(event: string | symbol, fn: Listener): this {
    const list = this._events.get(event) ?? [];
    list.push(fn);
    this._events.set(event, list);
    return this;
  }
  addListener(event: string | symbol, fn: Listener): this {
    return this.on(event, fn);
  }
  prependListener(event: string | symbol, fn: Listener): this {
    const list = this._events.get(event) ?? [];
    list.unshift(fn);
    this._events.set(event, list);
    return this;
  }
  once(event: string | symbol, fn: Listener): this {
    const wrapper: Listener & { listener?: Listener } = (...args) => {
      this.off(event, wrapper);
      fn.apply(this, args);
    };
    wrapper.listener = fn;
    return this.on(event, wrapper);
  }
  prependOnceListener(event: string | symbol, fn: Listener): this {
    const wrapper: Listener & { listener?: Listener } = (...args) => {
      this.off(event, wrapper);
      fn.apply(this, args);
    };
    wrapper.listener = fn;
    return this.prependListener(event, wrapper);
  }
  off(event: string | symbol, fn: Listener): this {
    const list = this._events.get(event);
    if (!list) return this;
    const i = list.findIndex((l) => l === fn || (l as { listener?: Listener }).listener === fn);
    if (i >= 0) list.splice(i, 1);
    if (!list.length) this._events.delete(event);
    return this;
  }
  removeListener(event: string | symbol, fn: Listener): this {
    return this.off(event, fn);
  }
  removeAllListeners(event?: string | symbol): this {
    if (event === undefined) this._events.clear();
    else this._events.delete(event);
    return this;
  }
  emit(event: string | symbol, ...args: unknown[]): boolean {
    const list = this._events.get(event);
    if (!list || !list.length) {
      if (event === "error") throw args[0] instanceof Error ? args[0] : new Error(`Unhandled 'error' event: ${String(args[0])}`);
      return false;
    }
    for (const fn of [...list]) fn.apply(this, args);
    return true;
  }
  listenerCount(event: string | symbol): number {
    return this._events.get(event)?.length ?? 0;
  }
  listeners(event: string | symbol): Listener[] {
    return (this._events.get(event) ?? []).map((l) => (l as { listener?: Listener }).listener ?? l);
  }
  rawListeners(event: string | symbol): Listener[] {
    return [...(this._events.get(event) ?? [])];
  }
  eventNames(): (string | symbol)[] {
    return [...this._events.keys()];
  }
  setMaxListeners(n: number): this {
    this._max = n;
    return this;
  }
  getMaxListeners(): number {
    return this._max;
  }
}

export function once(emitter: EventEmitter, event: string | symbol): Promise<unknown[]> {
  return new Promise((resolve, reject) => {
    const onError = (e: unknown) => {
      emitter.off(event, onEvent);
      reject(e);
    };
    const onEvent = (...args: unknown[]) => {
      emitter.off("error", onError);
      resolve(args);
    };
    emitter.once(event, onEvent);
    if (event !== "error") emitter.once("error", onError);
  });
}

export default EventEmitter;
