import { spawn } from "node:child_process";
import { readdirSync, existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const cli = join(root, "bin", "kairo.mjs");

const ENV = {
  "button-servo": { KAIRO_SIM_GPIO4_TOGGLE_MS: "150" },
  "ultrasonic-distance": { KAIRO_SIM_GPIO18_PULSE_US: "870" },
  "gps-tracker": { KAIRO_SIM_UART1: join(root, "examples/gps-tracker/nmea.txt"), KAIRO_SIM_ADC0: "1950" },
};
const EXPECT = {
  blink: /gpio 25 -> HIGH/,
  "button-servo": /servo -> 90 deg/,
  "i2c-temperature": /25\.50 C/,
  "ultrasonic-distance": /15\.0 cm/,
  "wifi-http-client": /connected to my-network/,
  "http-server-hono": /\{ led: true \}/,
  "express-api": /blinked: 2/,
  "ble-scan": /paired: true/,
  "gps-tracker": /48\.11730, 11\.51667/,
  "native-sensors": /native modules load/,
};
const FOREVER = new Set(["blink"]);

function runExample(name) {
  return new Promise((done) => {
    const child = spawn(process.execPath, [cli, "run", join(root, "examples", name, "main.ts"), "--heap", "4m"], {
      cwd: root,
      env: { ...process.env, ...(ENV[name] ?? {}) },
    });
    let out = "";
    const timer = setTimeout(() => child.kill("SIGKILL"), FOREVER.has(name) ? 2500 : 30000);
    child.stdout.on("data", (d) => (out += d));
    child.stderr.on("data", (d) => (out += d));
    child.on("exit", (code) => {
      clearTimeout(timer);
      const ok = EXPECT[name].test(out) && (FOREVER.has(name) || code === 0) && !/Uncaught|Assertion failed/.test(out);
      done({ name, ok, out });
    });
  });
}

const names = readdirSync(join(root, "examples")).filter((d) => existsSync(join(root, "examples", d, "main.ts")));
let failed = 0;
for (const name of names) {
  const r = await runExample(name);
  console.log(`${r.ok ? "ok  " : "FAIL"} ${name}`);
  if (!r.ok) {
    failed++;
    console.log(r.out.split("\n").slice(-15).join("\n"));
  }
}
process.exit(failed ? 1 : 0);
