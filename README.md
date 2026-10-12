# Kairo

Write microcontroller firmware in TypeScript, with your usual `node_modules`.

```ts
import { gpio, sleep } from "kairo";

const led = gpio.output(25);
for (;;) {
  led.toggle();
  await sleep(500);
}
```

A small native runtime (QuickJS, an event loop, a few syscalls) runs on the board. Your app is bundled by esbuild into one file and flashed to its own region. Only what you import ends up on the device.

> Status: the SDK and the host simulator are tested. The ESP32 and RP2 ports compile against stub headers only and **have not run on real hardware yet**.

## Install

```sh
npm i -g github:Kida-Takuma/Kairo      # or pnpm add -g
kairo init my-device               # name, directory, optional native (C) module
cd my-device && npm install && npm run dev
```

Needs Node 20+, a C compiler and `make`. Works with npm and pnpm.

## CLI

| Command                                                     |                                      |
| ----------------------------------------------------------- | ------------------------------------ |
| `kairo init [dir] [--native] [--yes]`                       | Scaffold an app                      |
| `kairo run <entry.ts>`                                      | Bundle and run on the host simulator |
| `kairo build <entry.ts> [--minify]`                         | Bundle only; prints size             |
| `kairo flash <entry.ts> [--port] [--monitor] [--sim]`       | Flash the app over USB serial        |
| `kairo monitor` / `kairo info`                              | Serial console / board info          |
| `kairo check [dir]`                                         | Type-check                           |
| `kairo native --board <board> [build\|flash] [--app <dir>]` | Build or flash the runtime           |

## API

| Module                                           |                                                                                                     |
| ------------------------------------------------ | --------------------------------------------------------------------------------------------------- |
| `kairo`                                          | `gpio` (output, input, `on("rise"\|"fall")`, `pulseIn`), `sleep`, `every`, `after`, `time`, `board` |
| `kairo/pwm` `kairo/i2c` `kairo/spi` `kairo/uart` | Buses and PWM / servo                                                                               |
| `kairo/storage`                                  | Persistent key-value store                                                                          |
| `kairo/net` `kairo/fetch` `kairo/http`           | TCP/UDP, `fetch()`, `serve({ port, fetch })` (Web `Request`/`Response`, so Hono works)              |
| `kairo/wifi` `kairo/ble` `kairo/clock`           | Wi-Fi, BLE scan/connect/pair, RTC + NTP                                                             |
| `kairo/gps` `kairo/battery` `kairo/power`        | NMEA, battery level, deep sleep                                                                     |

Pure-JS npm packages bundle as-is. Node built-ins resolve to shims: `events` `util` `assert` `timers` `url` `querystring` `buffer` `path` `http` `https` `crypto` (sha/HMAC only), and `express` maps to an Express-compatible implementation. `fs`, `net`, `stream`, `zlib` and friends are not available.

## Native bridge

Anything TypeScript cannot do (bit-banged protocols, vendor SDKs) goes in C under `native/` and is imported as `kairo:native/<name>`:

```c
#include "kairo/bridge.h"
static JSValue add(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  double a, b;
  if (kairo_arg_number(ctx, argc, argv, 0, &a) || kairo_arg_number(ctx, argc, argv, 1, &b)) return JS_EXCEPTION;
  return kairo_number(ctx, a + b);
}
KAIRO_NATIVE_MODULE(math) { kairo_export(m, "add", add, 2); }
```

`native/include/kairo/bridge.h` covers arguments, return values, errors, Promises, held callbacks, an ISR-safe event queue, `kairo_delay_us` and critical sections. Native code is compiled into the runtime, so it is flashed with `kairo native --app <dir> flash`.

## Boards

| `--board`                             | Toolchain    | Status                                                                           |
| ------------------------------------- | ------------ | -------------------------------------------------------------------------------- |
| `host`                                | cc, make     | Simulator. Real sockets; Wi-Fi, BLE, I2C, SPI, GPS and ADC are simulated         |
| `esp32` `esp32s3` `esp32c3` `esp32c6` | ESP-IDF 5.3+ | Full feature set, untested on hardware                                           |
| `pico` `pico_w` `pico2` `pico2_w`     | Pico SDK     | GPIO, PWM, I2C, SPI, UART, ADC, storage. No networking yet. Untested on hardware |

Blink needs ~150 KB of JS heap, a Hono server ~450 KB. Networking apps target ESP32-S3 with PSRAM or RP2350.

## Examples

`blink` `button-servo` `i2c-temperature` `ultrasonic-distance` `wifi-http-client` `http-server-hono` `express-api` `ble-scan` `gps-tracker` `native-sensors` — all run on the simulator with `pnpm example:<name>`.

## Repository

```
bin/ lib/   CLI
src/        device-side TypeScript API
shims/      Node built-in replacements
native/     C runtime, HAL per board, vendored quickjs-ng
examples/   one app per directory, also used as the smoke tests
test/       smoke test runner
```

Development: `pnpm install`, `pnpm format` (Prettier; C follows `.clang-format`), `pnpm check` (types), `pnpm test` (runs every example on the simulator). CI does the same on Linux and macOS. Not published to npm; install from GitHub as shown above.

Source files carry no comments by convention. MIT license.
