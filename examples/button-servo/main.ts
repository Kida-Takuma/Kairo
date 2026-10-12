import { board, gpio, halt, sleep } from "kairo";
import { pwm, servo } from "kairo/pwm";

const button = gpio.input(4, { pull: "up" });
const led = pwm.output(25, { frequency: 5000 });
const arm = servo.attach(18);

const positions = [0, 90, 180];
let index = 0;
let lastPress = 0;

button.on("fall", (_level, timeUs) => {
  if (timeUs - lastPress < 200_000) return;
  lastPress = timeUs;
  index = (index + 1) % positions.length;
  arm.write(positions[index]!);
  led.write((index + 1) / positions.length);
  console.log(`servo -> ${positions[index]} deg, led ${Math.round(led.duty * 100)}%`);
});

if (board.name === "host") {
  await sleep(700);
  halt();
}
