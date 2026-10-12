import { board, every, halt, sleep } from "kairo";
import { i2c } from "kairo/i2c";

const bus = i2c.open(0, { sda: 21, scl: 22, frequency: 400000 });
const sensor = bus.device(0x48);

if (!bus.probe(sensor.address)) {
  console.log(
    "no sensor at 0x48; devices found:",
    bus.scan().map((a) => `0x${a.toString(16)}`),
  );
  halt();
}

function readCelsius(): number | null {
  const raw = sensor.readRegisterU16(0x00);
  if (raw === null) return null;
  const value = raw >> 4;
  return (value > 0x7ff ? value - 0x1000 : value) * 0.0625;
}

const timer = every(1000, () => {
  const c = readCelsius();
  console.log(c === null ? "read failed" : `${c.toFixed(2)} C`);
});

if (board.name === "host") {
  await sleep(2100);
  timer.stop();
  bus.close();
  halt();
}
