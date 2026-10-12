import { spawn } from "node:child_process";
import { mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { crc32 as zlibCrc32 } from "node:zlib";
import { ensureSimulator } from "./native.mjs";

const USB_VENDORS = new Set(["303a", "10c4", "1a86", "2e8a", "0403", "0483"]);

export async function openSerial(f) {
  const { SerialPort } = await import("serialport");
  let path = f.port;
  if (!path) {
    const ports = await SerialPort.list();
    const usb = ports.filter((p) => p.vendorId && USB_VENDORS.has(p.vendorId.toLowerCase()));
    const candidates = usb.length ? usb : ports.filter((p) => /usb|acm|serial/i.test(p.path));
    if (candidates.length !== 1) {
      console.error(candidates.length ? "several serial ports found; choose one with --port:" : "no serial port found; specify --port");
      for (const p of candidates) console.error(`  ${p.path}  ${p.manufacturer ?? ""} ${p.vendorId ?? ""}:${p.productId ?? ""}`);
      process.exit(1);
    }
    path = candidates[0].path;
  }
  const port = new SerialPort({ path, baudRate: f.baud, autoOpen: false });
  await new Promise((ok, fail) => port.open((e) => (e ? fail(e) : ok())));
  await new Promise((ok) => port.set({ dtr: false, rts: false }, () => ok()));
  console.log(`serial: ${path} @ ${f.baud}`);
  return {
    onData: (cb) => port.on("data", cb),
    write: (data) => new Promise((ok, fail) => port.write(data, (e) => (e ? fail(e) : port.drain((e2) => (e2 ? fail(e2) : ok()))))),
    send: (data) => port.write(data),
    close: () => port.close(),
    onClose: (cb) => port.on("close", cb),
  };
}

export function openSimulator(appRoot) {
  const bin = ensureSimulator(appRoot);
  const image = join(appRoot, ".kairo", "app.img");
  mkdirSync(dirname(image), { recursive: true });
  const child = spawn(bin, ["--device", image], {
    stdio: ["pipe", "pipe", "inherit"],
    env: { ...process.env, KAIRO_SIM_KV: join(appRoot, ".kairo", "kv.bin") },
  });
  console.log(`simulator: ${image}`);
  return {
    onData: (cb) => child.stdout.on("data", cb),
    write: (data) => new Promise((ok, fail) => child.stdin.write(data, (e) => (e ? fail(e) : ok()))),
    send: (data) => child.stdin.write(data),
    close: () => child.kill(),
    onClose: (cb) => child.on("exit", cb),
    restart: () => openSimulator(appRoot),
  };
}

export function lineReader(link, { echo = false } = {}) {
  let buf = "";
  const waiters = [];
  link.onData((chunk) => {
    buf += chunk.toString("latin1");
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, i).replace(/\r$/, "");
      buf = buf.slice(i + 1);
      const w = waiters.findIndex((x) => x.match(line));
      if (w >= 0) waiters.splice(w, 1)[0].resolve(line);
      else if (echo || !line.startsWith("KAIRO ")) console.log(line);
    }
  });
  return {
    wait(prefix, timeoutMs) {
      return new Promise((resolve, reject) => {
        const match = (l) => l.startsWith(prefix) || l.startsWith("KAIRO ERR");
        const t = setTimeout(() => {
          const i = waiters.findIndex((x) => x.resolve === resolve);
          if (i >= 0) waiters.splice(i, 1);
          reject(new Error(`timeout waiting for '${prefix}'`));
        }, timeoutMs);
        waiters.push({
          match,
          resolve: (l) => {
            clearTimeout(t);
            if (l.startsWith("KAIRO ERR")) reject(new Error(l.slice(10)));
            else resolve(l);
          },
        });
      });
    },
  };
}

function crc32(buf) {
  if (typeof zlibCrc32 === "function") return zlibCrc32(buf) >>> 0;
  let c = ~0;
  for (const b of buf) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}

export async function flashImage(link, image) {
  const rx = lineReader(link);
  const crc = crc32(image);
  await link.write("\nKAIRO INFO\n");
  let info = await rx.wait("KAIRO INFO", 3000).catch(() => null);
  if (!info) {
    await link.write("\nKAIRO INFO\n");
    info = await rx.wait("KAIRO INFO", 3000);
  }
  console.log(info.slice(11));
  await link.write(`KAIRO FLASH ${image.length} ${crc.toString(16)}\n`);
  const ready = await rx.wait("KAIRO READY", 10000);
  const chunk = Number(ready.split(" ")[2]) || 1024;
  const t0 = Date.now();
  for (let off = 0; off < image.length; off += chunk) {
    await link.write(image.subarray(off, Math.min(off + chunk, image.length)));
    await rx.wait("KAIRO ACK", 10000);
    process.stdout.write(`\r${Math.min(off + chunk, image.length)} / ${image.length} bytes`);
  }
  await rx.wait("KAIRO DONE", 10000);
  console.log(`\nflashed ${image.length} bytes in ${((Date.now() - t0) / 1000).toFixed(1)}s; device rebooting`);
}

export async function monitor(link) {
  console.log("monitoring (Ctrl-C to quit)");
  process.stdin.resume();
  process.stdin.on("data", (d) => link.send(d));
  await new Promise((ok) => {
    link.onClose(ok);
    process.on("SIGINT", () => {
      link.close();
      ok();
    });
  });
}
