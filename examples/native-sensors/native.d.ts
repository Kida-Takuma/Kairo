declare module "kairo:native/dht" {
  export function read(pin: number, dht22?: boolean): { temperature: number; humidity: number };
}

declare module "kairo:native/ws2812" {
  export function show(pin: number, grb: Uint8Array): number;
}
