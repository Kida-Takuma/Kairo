declare module "node:events" {
  export * from "kairo/shims/events";
  export { default } from "kairo/shims/events";
}
declare module "events" {
  export * from "kairo/shims/events";
  export { default } from "kairo/shims/events";
}
declare module "node:util" {
  export * from "kairo/shims/util";
  export { default } from "kairo/shims/util";
}
declare module "util" {
  export * from "kairo/shims/util";
  export { default } from "kairo/shims/util";
}
declare module "node:assert" {
  export * from "kairo/shims/assert";
  export { default } from "kairo/shims/assert";
}
declare module "assert" {
  export * from "kairo/shims/assert";
  export { default } from "kairo/shims/assert";
}
declare module "node:assert/strict" {
  export { default } from "kairo/shims/assert";
}
declare module "node:timers" {
  export * from "kairo/shims/timers";
  export { default } from "kairo/shims/timers";
}
declare module "timers" {
  export * from "kairo/shims/timers";
  export { default } from "kairo/shims/timers";
}
declare module "node:timers/promises" {
  export * from "kairo/shims/timers_promises";
  export { default } from "kairo/shims/timers_promises";
}
declare module "timers/promises" {
  export * from "kairo/shims/timers_promises";
  export { default } from "kairo/shims/timers_promises";
}
declare module "node:process" {
  export { default } from "kairo/shims/process";
}
declare module "node:buffer" {
  export * from "buffer";
}
declare module "node:path" {
  import path = require("path-browserify");
  export = path;
}
declare module "node:http" {
  export * from "kairo/shims/http";
  export { default } from "kairo/shims/http";
}
declare module "http" {
  export * from "kairo/shims/http";
  export { default } from "kairo/shims/http";
}
declare module "node:https" {
  export * from "kairo/shims/https";
  export { default } from "kairo/shims/https";
}
declare module "https" {
  export * from "kairo/shims/https";
  export { default } from "kairo/shims/https";
}
declare module "node:url" {
  export * from "kairo/shims/url";
  export { default } from "kairo/shims/url";
}
declare module "url" {
  export * from "kairo/shims/url";
  export { default } from "kairo/shims/url";
}
declare module "node:querystring" {
  export * from "kairo/shims/querystring";
  export { default } from "kairo/shims/querystring";
}
declare module "querystring" {
  export * from "kairo/shims/querystring";
  export { default } from "kairo/shims/querystring";
}
declare module "node:crypto" {
  export * from "kairo/shims/crypto";
  export { default } from "kairo/shims/crypto";
}
declare module "crypto" {
  export * from "kairo/shims/crypto";
  export { default } from "kairo/shims/crypto";
}
declare module "express" {
  export * from "kairo/shims/express";
  export { default } from "kairo/shims/express";
}
