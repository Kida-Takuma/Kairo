import { board, every, gpio, halt, sleep } from "kairo";

const trigger = gpio.output(5);
const echo = gpio.input(18);

function measureCm(): number | null {
  trigger.low();
  trigger.high();
  trigger.low();
  const us = echo.pulseIn(true, 30000);
  return us ? us / 58 : null;
}

const timer = every(500, () => {
  const cm = measureCm();
  console.log(cm === null ? "out of range" : `${cm.toFixed(1)} cm`);
});

if (board.name === "host") {
  await sleep(1100);
  timer.stop();
  halt();
}
