import { board, halt, sleep } from "kairo";
import { clock } from "kairo/clock";
import "kairo/fetch";
import { storage } from "kairo/storage";
import { wifi } from "kairo/wifi";

const SSID = storage.get("wifi.ssid") ?? "my-network";
const PASSWORD = storage.get("wifi.password") ?? "my-password";
const URL = board.name === "host" ? "http://example.com/" : "https://example.com/";

const { ip } = await wifi.connect(SSID, PASSWORD, { timeoutMs: 20000 });
console.log(`connected to ${SSID} as ${ip} (${wifi.rssi()} dBm)`);

if (!clock.isSet) {
  try {
    await clock.syncNtp();
  } catch (e) {
    console.log("ntp failed:", (e as Error).message);
  }
}
console.log("time:", clock.now().toISOString());

const response = await fetch(URL, { timeoutMs: 10000 });
const body = await response.text();
console.log(response.status, response.headers.get("content-type"), `${body.length} bytes`);

storage.setJSON("lastFetch", { url: URL, status: response.status, at: clock.now().toISOString() });
console.log("stored:", storage.getJSON("lastFetch"));

if (board.name === "host") halt();
else {
  await sleep(60_000);
  wifi.disconnect();
}
