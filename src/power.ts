import "./globals";
import * as sys from "kairo:sys";

export const power = {
  deepSleep(ms: number): never {
    sys.deep_sleep(ms);
    throw new Error("deep sleep did not happen");
  },
  reboot(): never {
    sys.reboot();
    throw new Error("reboot did not happen");
  },
};

export default power;
