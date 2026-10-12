import { halt } from "kairo";
import { ble } from "kairo/ble";

const TARGET_NAME = "Kairo Sensor";

const devices = await ble.scan({ durationMs: 3000, onDevice: (d) => console.log(`${d.addr}  ${d.rssi} dBm  ${d.name || "(no name)"}`) });
console.log(`${devices.length} devices`);

const target = devices.find((d) => d.name === TARGET_NAME && d.connectable);
if (!target) {
  console.log(`${TARGET_NAME} not found`);
  halt();
}

const connection = await ble.connect(target!.addr, { timeoutMs: 10000 });
console.log("connected, pairing...");
await connection.pair({ timeoutMs: 30000 });
console.log("paired:", connection.paired);
connection.disconnect();
halt();
