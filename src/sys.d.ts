declare module "kairo:sys" {
  export function gpio_mode(pin: number, mode: 0 | 1 | 2 | 3): void;
  export function gpio_write(pin: number, level: boolean | number): void;
  export function gpio_read(pin: number): 0 | 1;
  export function time_us(): number;
  export function timer_start(delayMs: number, periodMs: number, fn: () => void): number;
  export function timer_cancel(id: number): boolean;
  export function console_write(text: string): void;
  export function halt(): void;
  export function board(): string;
}
declare module "kairo:sys" {
  export function random_bytes(n: number): Uint8Array;
  export function rtc_get(): number;
  export function rtc_set(epochMs: number): void;

  export function net_socket(type: 0 | 1): number;
  export function net_connect(fd: number, host: string, port: number): boolean;
  export function net_tls_connect(fd: number, host: string, port: number): boolean;
  export function net_connect_status(fd: number): boolean;
  export function net_bind(fd: number, port: number): void;
  export function net_listen(fd: number, port: number, backlog?: number): void;
  export function net_accept(fd: number): { fd: number; addr: string; port: number } | undefined;
  export function net_send(fd: number, data: string | Uint8Array | ArrayBuffer): number;
  export function net_recv(fd: number, max?: number): Uint8Array | null | undefined;
  export function net_sendto(fd: number, host: string, port: number, data: string | Uint8Array | ArrayBuffer): number;
  export function net_recvfrom(fd: number, max?: number): { data: Uint8Array; addr: string; port: number } | undefined;
  export function net_close(fd: number): void;
  export function net_watch(fd: number, events: number, cb: (revents: number) => void): void;
  export function net_unwatch(fd: number): void;
  export function net_resolve(host: string): string;
  export function net_local_ip(): string | null;

  export function wifi_connect(ssid: string, password?: string): void;
  export function wifi_status(): 0 | 1 | 2 | 3;
  export function wifi_disconnect(): void;
  export function wifi_scan_start(): void;
  export function wifi_scan_results(): { ssid: string; rssi: number; channel: number; secure: boolean }[] | undefined;
  export function wifi_rssi(): number;

  export function ble_init(): void;
  export function ble_scan_start(durationMs: number): void;
  export function ble_scan_stop(): void;
  export function ble_scan_results(): { devices: { addr: string; name: string; rssi: number; connectable: boolean }[]; done: boolean };
  export function ble_connect(addr: string): number;
  export function ble_conn_status(conn: number): 0 | 1 | 2;
  export function ble_pair(conn: number): void;
  export function ble_pair_status(conn: number): 0 | 1 | 2;
  export function ble_disconnect(conn: number): void;

  export function uart_open(id: number, tx: number, rx: number, baud: number): void;
  export function uart_write(id: number, data: string | Uint8Array | ArrayBuffer): number;
  export function uart_read(id: number, max?: number): Uint8Array | undefined;
  export function uart_close(id: number): void;

  export function adc_read_mv(channel: number): number;
}
declare module "kairo:sys" {
  export function pwm_set(pin: number, freqHz: number, duty: number): void;
  export function pwm_stop(pin: number): void;
  export function gpio_watch(pin: number, edge: 1 | 2 | 3, cb: (level: number, timeUs: number) => void): void;
  export function gpio_unwatch(pin: number): void;
  export function gpio_pulse_in(pin: number, level: boolean | number, timeoutUs?: number): number;
  export function i2c_open(bus: number, sda: number, scl: number, freqHz?: number): void;
  export function i2c_write(bus: number, addr: number, data: string | Uint8Array | ArrayBuffer): boolean;
  export function i2c_read(bus: number, addr: number, length: number): Uint8Array | null;
  export function i2c_write_read(bus: number, addr: number, data: string | Uint8Array | ArrayBuffer, length: number): Uint8Array | null;
  export function i2c_close(bus: number): void;
  export function spi_open(bus: number, sck: number, mosi: number, miso: number, freqHz?: number, mode?: number): void;
  export function spi_transfer(bus: number, data: string | Uint8Array | ArrayBuffer): Uint8Array;
  export function spi_close(bus: number): void;
  export function kv_get(key: string): Uint8Array | null;
  export function kv_set(key: string, data: string | Uint8Array | ArrayBuffer): void;
  export function kv_delete(key: string): boolean;
  export function kv_keys(): string[];
  export function deep_sleep(ms: number): void;
  export function reboot(): void;
}
