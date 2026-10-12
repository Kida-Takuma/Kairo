import { spawnSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { BOARDS, nativeDir, need, parseFlags, run, usage } from "./util.mjs";

export function nativeModules(appRoot) {
  const dir = join(appRoot, "native");
  if (!existsSync(dir)) return null;
  const sources = readdirSync(dir)
    .filter((f) => f.endsWith(".c"))
    .map((f) => join(dir, f));
  if (!sources.length) return null;
  const names = [];
  for (const src of sources) {
    for (const m of readFileSync(src, "utf8").matchAll(/KAIRO_NATIVE_MODULE\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)/g)) names.push(m[1]);
  }
  const genDir = join(appRoot, ".kairo", "native");
  mkdirSync(genDir, { recursive: true });
  const table = join(genDir, "modules.c");
  const code = [
    '#include "kairo/bridge.h"',
    ...names.map((n) => `void kairo_native_init_${n}(kairo_module_t *m);`),
    "void kairo_native_register_all(void)",
    "{",
    ...names.map((n) => `    kairo_native_module("${n}", kairo_native_init_${n});`),
    "}",
    "",
  ].join("\n");
  if (!existsSync(table) || readFileSync(table, "utf8") !== code) writeFileSync(table, code);
  return { dir, names, sources: [...sources, table], buildDir: join(appRoot, ".kairo", "native", "build") };
}

export function ensureSimulator(appRoot) {
  const native = appRoot ? nativeModules(appRoot) : null;
  const buildDir = native ? native.buildDir : join(nativeDir, "build");
  const bin = join(buildDir, "host", "kairo-sim");
  const args = ["-s", "BOARD=host", `BUILD=${buildDir}`];
  if (native) {
    args.push(`NATIVE_SRCS=${native.sources.join(" ")}`, `NATIVE_INC=${native.dir}`);
    console.log(`native modules: ${native.names.map((n) => `kairo:native/${n}`).join(", ")}`);
  }
  if (run("make", args, { cwd: nativeDir }) !== 0 || !existsSync(bin)) {
    console.error("failed to build the host simulator (make BOARD=host in native/)");
    process.exit(1);
  }
  return bin;
}

export function cmdNative(args) {
  const f = parseFlags(args);
  const action = f.rest[0] ?? "build";
  const board = BOARDS[f.board];
  if (!board) usage();
  const appRoot = f.app ? resolve(f.app) : null;
  const native = appRoot ? nativeModules(appRoot) : null;
  const nativeDefs = native ? [`-DKAIRO_NATIVE_SOURCES=${native.sources.join(";")}`, `-DKAIRO_NATIVE_INCLUDE=${native.dir}`] : [];
  if (native) console.log(`native modules: ${native.names.map((n) => `kairo:native/${n}`).join(", ")}`);
  if (board.family === "host") {
    if (action === "clean") process.exit(run("make", ["-s", "BOARD=host", "clean"], { cwd: nativeDir }));
    ensureSimulator(appRoot);
    process.exit(0);
  }
  if (board.family === "esp32") {
    need("idf.py", "Install ESP-IDF 5.3+ and run its export script first (https://docs.espressif.com/projects/esp-idf/).");
    const dir = join(nativeDir, "boards", "esp32");
    const buildDir = native ? join(native.buildDir, f.board) : join(nativeDir, "build", f.board);
    const base = ["-C", dir, "-B", buildDir, `-DSDKCONFIG=${join(buildDir, "sdkconfig")}`, `-DIDF_TARGET=${board.target}`, ...nativeDefs];
    if (action === "clean") process.exit(run("idf.py", [...base, "fullclean"]));
    if (run("idf.py", [...base, "build"]) !== 0) process.exit(1);
    if (action === "flash") process.exit(run("idf.py", [...base, ...(f.port ? ["-p", f.port] : []), "flash"]));
    console.log(`runtime built: ${buildDir}/kairo.bin`);
    process.exit(0);
  }
  if (board.family === "rp2") {
    need("cmake", "Install cmake and the Raspberry Pi Pico SDK, and set PICO_SDK_PATH.");
    if (!process.env.PICO_SDK_PATH) {
      console.error("PICO_SDK_PATH is not set");
      process.exit(1);
    }
    const dir = join(nativeDir, "boards", "rp2");
    const buildDir = native ? join(native.buildDir, f.board) : join(nativeDir, "build", f.board);
    if (action === "clean") process.exit(run("rm", ["-rf", buildDir]));
    if (run("cmake", ["-S", dir, "-B", buildDir, `-DPICO_BOARD=${board.picoBoard}`, "-DCMAKE_BUILD_TYPE=MinSizeRel", ...nativeDefs]) !== 0) process.exit(1);
    if (run("cmake", ["--build", buildDir, "--parallel"]) !== 0) process.exit(1);
    const uf2 = join(buildDir, "kairo.uf2");
    if (action === "flash") {
      if (spawnSync("picotool", ["version"], { stdio: "ignore" }).status === 0) process.exit(run("picotool", ["load", "-f", "-x", uf2]));
      const vol = ["/Volumes/RPI-RP2", "/Volumes/RP2350", "/media/RPI-RP2", "/media/RP2350"].find((v) => existsSync(v));
      if (!vol) {
        console.error(`hold BOOTSEL while plugging the board in, then copy ${uf2} to the RPI-RP2 drive (or install picotool)`);
        process.exit(1);
      }
      copyFileSync(uf2, join(vol, "kairo.uf2"));
      console.log(`copied ${uf2} -> ${vol}`);
      process.exit(0);
    }
    console.log(`runtime built: ${uf2}`);
    process.exit(0);
  }
}
