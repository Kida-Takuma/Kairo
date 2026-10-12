import { mkdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { bundle, defaultOut } from "./bundle.mjs";
import { ensureSimulator } from "./native.mjs";
import { flashImage, lineReader, monitor, openSerial, openSimulator } from "./serial.mjs";
import { appRootOf, parseFlags, require, run, usage } from "./util.mjs";

export async function cmdBuild(args) {
  const f = parseFlags(args);
  if (!f.rest[0]) usage();
  await bundle(f.rest[0], f.out ?? defaultOut(f.rest[0]), { minify: f.minify });
}

export async function cmdRun(args) {
  const f = parseFlags(args);
  if (!f.rest[0]) usage();
  const out = await bundle(f.rest[0], defaultOut(f.rest[0]), { minify: f.minify });
  const bin = ensureSimulator(appRootOf(f.rest[0]));
  const simArgs = [];
  if (f.heap) simArgs.push("--heap", String(f.heap));
  if (f.stats) simArgs.push("--stats");
  const kv = join(appRootOf(f.rest[0]), ".kairo", "kv.bin");
  mkdirSync(dirname(kv), { recursive: true });
  process.exit(run(bin, [...simArgs, out], { env: { ...process.env, KAIRO_SIM_KV: process.env.KAIRO_SIM_KV ?? kv } }));
}

export function cmdCheck(args) {
  const tsc = join(dirname(require.resolve("typescript/package.json")), "bin", "tsc");
  process.exit(run(process.execPath, [tsc, "--noEmit", "-p", args[0] ?? "."]));
}

export async function cmdFlash(args) {
  const f = parseFlags(args);
  if (!f.rest[0]) usage();
  const out = await bundle(f.rest[0], defaultOut(f.rest[0]), { minify: f.minify });
  const image = readFileSync(out);
  let link = f.sim ? openSimulator(appRootOf(f.rest[0])) : await openSerial(f);
  try {
    await flashImage(link, image);
  } catch (e) {
    console.error(`\nflash failed: ${e.message}`);
    link.close();
    process.exit(1);
  }
  if (!f.monitor) {
    link.close();
    return;
  }
  if (f.sim) {
    await new Promise((ok) => link.onClose(ok));
    link = link.restart();
  }
  await monitor(link);
}

export async function cmdMonitor(args) {
  const f = parseFlags(args);
  const link = f.sim ? openSimulator(process.cwd()) : await openSerial(f);
  lineReader(link, { echo: true });
  await monitor(link);
}

export async function cmdInfo(args) {
  const f = parseFlags(args);
  const link = f.sim ? openSimulator(process.cwd()) : await openSerial(f);
  const rx = lineReader(link);
  try {
    await link.write("\nKAIRO INFO\n");
    console.log((await rx.wait("KAIRO INFO", 3000)).slice(11));
  } catch (e) {
    console.error(e.message);
    process.exit(1);
  } finally {
    link.close();
  }
}
