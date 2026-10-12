#!/usr/bin/env node
import { cmdBuild, cmdCheck, cmdFlash, cmdInfo, cmdMonitor, cmdRun } from "../lib/commands.mjs";
import { cmdInit } from "../lib/init.mjs";
import { cmdNative } from "../lib/native.mjs";
import { usage } from "../lib/util.mjs";

const [cmd, ...rest] = process.argv.slice(2);
switch (cmd) {
  case "init":
    await cmdInit(rest);
    break;
  case "build":
    await cmdBuild(rest);
    break;
  case "run":
    await cmdRun(rest);
    break;
  case "flash":
    await cmdFlash(rest);
    break;
  case "monitor":
    await cmdMonitor(rest);
    break;
  case "info":
    await cmdInfo(rest);
    break;
  case "check":
    cmdCheck(rest);
    break;
  case "native":
    cmdNative(rest);
    break;
  default:
    usage(cmd ? 1 : 0);
}
