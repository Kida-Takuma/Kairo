import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { basename, dirname, join, relative, resolve, sep } from "node:path";
import { createInterface } from "node:readline";
import { parseFlags, pkgDir, usage } from "./util.mjs";

function workspaceRootOf(dir) {
  let d = dir;
  while (dirname(d) !== d) {
    if (existsSync(join(d, "pnpm-workspace.yaml"))) return d;
    d = dirname(d);
  }
  return null;
}

function globCovers(pattern, rel) {
  const re = new RegExp(
    "^" +
      pattern
        .split("*")
        .map((p) => p.replace(/[.+^${}()|[\]\\]/g, "\\$&"))
        .join("[^/]*") +
      "$",
  );
  return re.test(rel);
}

function ensureWorkspaceEntry(root, appDir) {
  const yaml = join(root, "pnpm-workspace.yaml");
  const text = readFileSync(yaml, "utf8");
  const rel = relative(root, appDir).split(sep).join("/");
  const patterns = [...text.matchAll(/^\s*-\s*["']?([^"'\n]+?)["']?\s*$/gm)].map((m) => m[1]);
  if (patterns.some((p) => globCovers(p, rel))) return null;
  const pattern = rel.includes("/") ? `${rel.slice(0, rel.lastIndexOf("/"))}/*` : rel;
  const updated = text.replace(/^packages:\n((?:\s*-.*\n)*)/m, (m) => `${m}  - ${pattern}\n`);
  writeFileSync(yaml, updated.includes(pattern) ? updated : `${text.trimEnd()}\n  - ${pattern}\n`);
  return pattern;
}

function prompter() {
  const rl = createInterface({ input: process.stdin, output: process.stdin.isTTY ? process.stdout : undefined, terminal: !!process.stdin.isTTY });
  const lines = [];
  const waiters = [];
  let closed = false;
  rl.on("line", (l) => (waiters.length ? waiters.shift()(l) : lines.push(l)));
  rl.on("close", () => {
    closed = true;
    while (waiters.length) waiters.shift()("");
  });
  const next = () => (lines.length ? Promise.resolve(lines.shift()) : closed ? Promise.resolve("") : new Promise((ok) => waiters.push(ok)));
  return {
    async ask(question, fallback) {
      process.stdout.write(`${question}${fallback !== undefined ? ` (${fallback})` : ""}: `);
      const answer = (await next()).trim();
      if (!process.stdin.isTTY) process.stdout.write(`${answer || fallback || ""}\n`);
      return answer || fallback;
    },
    async askYesNo(question, fallback) {
      process.stdout.write(`${question} ${fallback ? "[Y/n]" : "[y/N]"}: `);
      const answer = (await next()).trim().toLowerCase();
      if (!process.stdin.isTTY) process.stdout.write(`${answer || (fallback ? "y" : "n")}\n`);
      if (!answer) return fallback;
      return answer === "y" || answer === "yes";
    },
    close: () => rl.close(),
  };
}

function validName(name) {
  return /^(@[a-z0-9-~][a-z0-9-._~]*\/)?[a-z0-9-~][a-z0-9-._~]*$/.test(name);
}

const TEMPLATE_MAIN = `import { gpio, sleep } from "kairo";

const led = gpio.output(25);

console.log("hello from kairo");
for (let i = 0; i < 10; i++) {
  led.toggle();
  await sleep(200);
}
`;

const TEMPLATE_MAIN_NATIVE = `import { gpio, sleep } from "kairo";
import { add, boardName } from "kairo:native/hello";

const led = gpio.output(25);

console.log("hello from kairo on", boardName(), "- native add(2, 3) =", add(2, 3));
for (let i = 0; i < 10; i++) {
  led.toggle();
  await sleep(200);
}
`;

const TEMPLATE_NATIVE_C = `#include "kairo/bridge.h"

static JSValue add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    double a, b;
    if (kairo_arg_number(ctx, argc, argv, 0, &a) || kairo_arg_number(ctx, argc, argv, 1, &b)) return JS_EXCEPTION;
    return kairo_number(ctx, a + b);
}

static JSValue board_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return kairo_string(ctx, hal_board_name());
}

KAIRO_NATIVE_MODULE(hello)
{
    kairo_export(m, "add", add, 2);
    kairo_export(m, "boardName", board_name, 0);
}
`;

const TEMPLATE_NATIVE_DTS = `declare module "kairo:native/hello" {
  export function add(a: number, b: number): number;
  export function boardName(): string;
}
`;

function packageManager() {
  const ua = process.env.npm_config_user_agent ?? "";
  if (ua.startsWith("pnpm/")) return "pnpm";
  if (ua.startsWith("yarn/")) return "yarn";
  if (ua.startsWith("bun/")) return "bun";
  if (ua.startsWith("npm/")) return "npm";
  return spawnSync("pnpm", ["--version"], { stdio: "ignore" }).status === 0 ? "pnpm" : "npm";
}

export async function cmdInit(args) {
  const f = parseFlags(args);
  const pm = packageManager();
  const runCmd = (script) => (pm === "npm" ? `npm run ${script}` : `${pm} ${script}`);
  const rl = f.yes ? null : prompter();
  const defaultName = f.name ?? (f.rest[0] ? basename(resolve(f.rest[0])) : "my-kairo-app");
  const name = f.name ?? (rl ? await rl.ask("Project name", defaultName) : defaultName);
  if (!validName(name)) {
    rl?.close();
    console.error(`invalid package name: ${name}`);
    process.exit(1);
  }
  const defaultDir = f.rest[0] ?? `./${name.replace(/^@[^/]+\//, "")}`;
  const dir = resolve(rl && !f.rest[0] ? await rl.ask("Directory", defaultDir) : defaultDir);
  const native = f.native ?? (rl ? await rl.askYesNo("Include a native (C) module scaffold?", false) : false);
  rl?.close();

  if (existsSync(dir) && readdirSync(dir).length && !f.force) {
    console.error(`${dir} is not empty (use --force to write into it anyway)`);
    process.exit(1);
  }
  mkdirSync(dir, { recursive: true });

  const root = workspaceRootOf(dir);
  const insideKairoRepo = root && existsSync(join(root, "bin", "kairo.mjs")) && existsSync(join(root, "native", "Makefile"));
  const kairoDep = insideKairoRepo ? `link:${relative(dir, root).split(sep).join("/")}` : `file:${pkgDir}`;
  const tsconfig = insideKairoRepo
    ? { extends: relative(dir, join(root, "tsconfig.base.json")).split(sep).join("/"), compilerOptions: { noEmit: true }, include: ["*.ts"] }
    : {
        compilerOptions: {
          target: "ES2022",
          module: "ESNext",
          moduleResolution: "Bundler",
          lib: ["ES2022"],
          strict: true,
          noUncheckedIndexedAccess: true,
          skipLibCheck: true,
          noEmit: true,
          types: [],
        },
        include: ["*.ts"],
      };

  const files = {
    "package.json":
      JSON.stringify(
        {
          name,
          private: true,
          type: "module",
          scripts: { dev: "kairo run main.ts", build: "kairo build main.ts --minify", flash: "kairo flash main.ts --monitor", check: "kairo check ." },
          devDependencies: { kairo: kairoDep },
        },
        null,
        2,
      ) + "\n",
    "tsconfig.json": JSON.stringify(tsconfig, null, 2) + "\n",
    "main.ts": native ? TEMPLATE_MAIN_NATIVE : TEMPLATE_MAIN,
    ".gitignore": ".kairo/\nnode_modules/\n",
    "README.md": `# ${name}\n\n\`\`\`sh\n${pm} install\n${runCmd("dev")}          # run on the host simulator\n${runCmd("flash")}        # flash to a connected board\n\`\`\`\n${native ? "\nNative modules live in `native/`; their TypeScript types are in `native.d.ts`.\n" : ""}`,
  };
  if (native) {
    files["native/hello.c"] = TEMPLATE_NATIVE_C;
    files["native.d.ts"] = TEMPLATE_NATIVE_DTS;
  }
  for (const [rel, content] of Object.entries(files)) {
    const target = join(dir, rel);
    mkdirSync(dirname(target), { recursive: true });
    writeFileSync(target, content);
  }

  let addedPattern = null;
  if (insideKairoRepo) addedPattern = ensureWorkspaceEntry(root, dir);
  else console.log(`kairo is referenced as ${kairoDep}`);

  const shown = relative(process.cwd(), dir) || ".";
  console.log(`\ncreated ${name} in ${shown}${native ? " (with native module scaffold)" : ""}`);
  for (const rel of Object.keys(files)) console.log(`  ${rel}`);
  if (addedPattern) console.log(`added "${addedPattern}" to pnpm-workspace.yaml`);
  console.log(`\nnext:\n  cd ${shown}\n  ${pm} install\n  ${runCmd("dev")}`);
}
