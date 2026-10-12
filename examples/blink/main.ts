import { every, gpio } from "kairo";

const led = gpio.output(25);
every(500, () => led.toggle());
