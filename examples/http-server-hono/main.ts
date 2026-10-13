import { createHmac, timingSafeEqual } from "node:crypto";
import { Hono } from "hono";
import { board, gpio, halt } from "kairo";
import "kairo/fetch";
import { serve } from "kairo/http";
import { storage } from "kairo/storage";

const SECRET = storage.get("api.secret") ?? "change-me";
if (SECRET === "change-me") console.warn('api.secret is not set; store one with storage.set("api.secret", ...) before exposing this server');
const led = gpio.output(25);
const app = new Hono();

const sign = (payload: string) => createHmac("sha256", SECRET).update(payload).digest("hex");
const authorized = (token: string | undefined) => {
  if (!token) return false;
  const [payload, sig] = token.split(".");
  if (!payload || !sig) return false;
  const expected = sign(payload);
  return (
    expected.length === sig.length && timingSafeEqual(new TextEncoder().encode(expected), new TextEncoder().encode(sig)) && Number(payload) > Date.now() / 1000
  );
};

app.get("/", (c) => c.json({ board: board.name, led: led.value, uptimeMs: Math.round(performance.now()) }));
app.get("/led", (c) => c.json({ led: led.value }));
app.put("/led/:state", (c) => {
  if (!authorized(c.req.header("authorization")?.replace(/^Bearer /, ""))) return c.json({ error: "unauthorized" }, 401);
  led.write(c.req.param("state") === "on");
  return c.json({ led: led.value });
});
app.post("/token", async (c) => {
  const { secret } = await c.req.json<{ secret?: string }>();
  if (secret !== SECRET) return c.json({ error: "bad secret" }, 403);
  const exp = String(Math.floor(Date.now() / 1000) + 3600);
  return c.json({ token: `${exp}.${sign(exp)}` });
});

const server = serve({ port: 8080, fetch: app.fetch, onListen: ({ port }) => console.log(`listening on :${port}`) });

if (board.name === "host") {
  const base = "http://127.0.0.1:8080";
  console.log(await (await fetch(`${base}/`)).json());
  console.log((await fetch(`${base}/led/on`, { method: "PUT" })).status);
  const { token } = await (
    await fetch(`${base}/token`, { method: "POST", body: JSON.stringify({ secret: SECRET }), headers: { "content-type": "application/json" } })
  ).json<{ token: string }>();
  console.log(await (await fetch(`${base}/led/on`, { method: "PUT", headers: { authorization: `Bearer ${token}` } })).json());
  server.close();
  halt();
}
