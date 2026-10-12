import { board, every, halt, sleep } from "kairo";
import { read } from "kairo:native/dht";
import { show } from "kairo:native/ws2812";

const DHT_PIN = 4;
const LED_PIN = 48;
const LED_COUNT = 8;

function colorFor(temperature: number): [number, number, number] {
  const t = Math.min(1, Math.max(0, (temperature - 15) / 20));
  return [Math.round(40 * (1 - t)), Math.round(40 * t), 0];
}

function render(temperature: number): void {
  const [g, r, b] = colorFor(temperature);
  const pixels = new Uint8Array(LED_COUNT * 3);
  for (let i = 0; i < LED_COUNT; i++) pixels.set([g, r, b], i * 3);
  show(LED_PIN, pixels);
}

const timer = every(2000, () => {
  try {
    const { temperature, humidity } = read(DHT_PIN, true);
    console.log(`${temperature.toFixed(1)} C  ${humidity.toFixed(1)} %`);
    render(temperature);
  } catch (e) {
    console.log((e as Error).message);
  }
});

if (board.name === "host") {
  await sleep(100);
  timer.stop();
  console.log("dht and ws2812 need real hardware; this example only checks that the native modules load on the host");
  halt();
}
