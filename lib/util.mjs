import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const require = createRequire(import.meta.url);

export const pkgDir = resolve(dirname(fileURLToPath(import.meta.url)), "..");

export const nativeDir = join(pkgDir, "native");

export function parseSize(s) {
  const m = /^(\d+(?:\.\d+)?)([kKmM]?)$/.exec(String(s ?? ""));
  if (!m) usage();
  const n = parseFloat(m[1]);
  return Math.round(m[2].toLowerCase() === "m" ? n * 1024 * 1024 : m[2] ? n * 1024 : n);
}

export function parseFlags(args) {
  const f = {
    minify: false,
    out: null,
    heap: null,
    stats: false,
    port: null,
    baud: 115200,
    monitor: false,
    sim: false,
    board: "host",
    app: null,
    name: null,
    native: null,
    yes: false,
    force: false,
    rest: [],
  };
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === "--minify") f.minify = true;
    else if (a === "--stats") f.stats = true;
    else if (a === "--monitor") f.monitor = true;
    else if (a === "--sim") f.sim = true;
    else if (a === "-o") f.out = args[++i];
    else if (a === "--heap") f.heap = parseSize(args[++i]);
    else if (a === "--port" || a === "-p") f.port = args[++i];
    else if (a === "--baud" || a === "-b") f.baud = Number(args[++i]);
    else if (a === "--board") f.board = args[++i];
    else if (a === "--app") f.app = args[++i];
    else if (a === "--name") f.name = args[++i];
    else if (a === "--native") f.native = true;
    else if (a === "--no-native") f.native = false;
    else if (a === "--yes" || a === "-y") f.yes = true;
    else if (a === "--force") f.force = true;
    else f.rest.push(a);
  }
  return f;
}

export function appRootOf(entry) {
  let dir = dirname(resolve(entry));
  while (!existsSync(join(dir, "package.json")) && dirname(dir) !== dir) dir = dirname(dir);
  return dir;
}

export function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { stdio: "inherit", ...opts });
  if (r.error) {
    console.error(`${cmd}: ${r.error.message}`);
    process.exit(1);
  }
  return r.status ?? 1;
}

export function need(cmd, hint) {
  const r = spawnSync(cmd, ["--version"], { stdio: "ignore" });
  if (r.error) {
    console.error(`'${cmd}' not found. ${hint}`);
    process.exit(1);
  }
}

export const BOARDS = {
  host: { family: "host" },
  esp32: { family: "esp32", target: "esp32" },
  esp32s3: { family: "esp32", target: "esp32s3" },
  esp32c3: { family: "esp32", target: "esp32c3" },
  esp32c6: { family: "esp32", target: "esp32c6" },
  pico: { family: "rp2", picoBoard: "pico" },
  pico_w: { family: "rp2", picoBoard: "pico_w" },
  pico2: { family: "rp2", picoBoard: "pico2" },
  pico2_w: { family: "rp2", picoBoard: "pico2_w" },
};

export function usage(code = 1) {
  console.error(`usage:
  kairo init    [dir] [--name <name>] [--native | --no-native] [--yes] [--force]
  kairo build   <entry.ts> [-o out.js] [--minify]
  kairo run     <entry.ts> [--minify] [--heap 512k] [--stats]
  kairo flash   <entry.ts> [--port /dev/cu.usbmodem*] [--baud 115200] [--minify] [--monitor]
  kairo flash   <entry.ts> --sim [--monitor]
  kairo monitor [--port ...] [--baud 115200]
  kairo info    [--port ...]
  kairo check   [project-dir]
  kairo native  [--board ${Object.keys(BOARDS).join("|")}] [build|flash|clean] [--port ...] [--app <dir>]

  Put C files under <app>/native/ to add native modules (import "kairo:native/<name>").`);
  process.exit(code);
}
