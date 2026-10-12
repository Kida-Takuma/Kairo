import { board, every, halt, sleep } from "kairo";
import { battery } from "kairo/battery";
import { gps } from "kairo/gps";
import { storage } from "kairo/storage";

battery.configure({ adc: 0, divider: 2 });
const receiver = gps.open({ uart: 1, rx: 16, tx: 17, baud: 9600 });

receiver.on("fix", (fix) => {
  const { voltage, percent } = battery.read();
  const record = { lat: fix.latitude, lon: fix.longitude, alt: fix.altitude, sats: fix.satellites, at: fix.time?.toISOString() ?? null, battery: percent };
  storage.setJSON("lastFix", record);
  console.log(
    `${record.lat.toFixed(5)}, ${record.lon.toFixed(5)} alt ${record.alt ?? "?"} m sats ${record.sats ?? "?"} battery ${voltage.toFixed(2)} V (${percent}%)`,
  );
});

const watchdog = every(10_000, () => {
  if (!receiver.position) console.log("waiting for fix...", storage.getJSON("lastFix") ? "last known:" : "", storage.getJSON("lastFix") ?? "");
});

if (board.name === "host") {
  await receiver.waitForFix(3000).catch(() => console.log("no fix on host; set KAIRO_SIM_UART1 to an NMEA file"));
  await sleep(300);
  watchdog.stop();
  receiver.close();
  halt();
}
