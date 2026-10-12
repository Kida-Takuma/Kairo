import express from "express";
import { board, gpio, halt } from "kairo";
import "kairo/fetch";

const led = gpio.output(25);
const app = express();
app.use(express.json());
app.use((req, _res, next) => {
  console.log(`${req.method} ${req.originalUrl}`);
  next();
});

app.get("/", (_req, res) => res.json({ board: board.name, led: led.value }));
app.put("/led/:state", (req, res) => {
  led.write(req.params.state === "on");
  res.json({ led: led.value });
});
app.post("/blink", async (req, res) => {
  const { times = 3, intervalMs = 100 } = (req.body ?? {}) as { times?: number; intervalMs?: number };
  for (let i = 0; i < times * 2; i++) {
    led.toggle();
    await new Promise((r) => setTimeout(r, intervalMs));
  }
  res.json({ blinked: times });
});

const server = app.listen(8081, () => console.log("listening on :8081"));

if (board.name === "host") {
  const base = "http://127.0.0.1:8081";
  console.log(await (await fetch(`${base}/`)).json());
  console.log(await (await fetch(`${base}/led/on`, { method: "PUT" })).json());
  console.log(
    await (
      await fetch(`${base}/blink`, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ times: 2, intervalMs: 10 }) })
    ).json(),
  );
  server.close();
  halt();
}
