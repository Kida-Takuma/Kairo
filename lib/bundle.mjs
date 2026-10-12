import { build as esbuild } from "esbuild";
import { mkdirSync, statSync } from "node:fs";
import { basename, dirname, join } from "node:path";
import { appRootOf, pkgDir, require } from "./util.mjs";

const NODE_SHIMS = {
  events: join(pkgDir, "shims/events.ts"),
  util: join(pkgDir, "shims/util.ts"),
  assert: join(pkgDir, "shims/assert.ts"),
  "assert/strict": join(pkgDir, "shims/assert.ts"),
  timers: join(pkgDir, "shims/timers.ts"),
  "timers/promises": join(pkgDir, "shims/timers_promises.ts"),
  process: join(pkgDir, "shims/process.ts"),
  http: join(pkgDir, "shims/http.ts"),
  https: join(pkgDir, "shims/https.ts"),
  url: join(pkgDir, "shims/url.ts"),
  querystring: join(pkgDir, "shims/querystring.ts"),
  crypto: join(pkgDir, "shims/crypto.ts"),
  express: join(pkgDir, "shims/express.ts"),
  buffer: require.resolve("buffer/"),
  path: require.resolve("path-browserify"),
  "path/posix": require.resolve("path-browserify"),
};

const NODE_UNAVAILABLE = [
  "fs",
  "fs/promises",
  "net",
  "http2",
  "tls",
  "dns",
  "dgram",
  "child_process",
  "cluster",
  "os",
  "stream",
  "stream/promises",
  "stream/web",
  "zlib",
  "readline",
  "repl",
  "vm",
  "worker_threads",
  "perf_hooks",
  "async_hooks",
  "string_decoder",
  "module",
  "inspector",
  "tty",
  "v8",
  "wasi",
];

function kairoPlugin({ onUnavailable }) {
  return {
    name: "kairo",
    setup(build) {
      build.onResolve({ filter: /^node:|^(events|util|assert|timers|process|buffer|path|http|https|url|querystring|crypto|express)(\/.*)?$/ }, (args) => {
        const name = args.path.replace(/^node:/, "");
        if (NODE_SHIMS[name]) return { path: NODE_SHIMS[name] };
        if (NODE_UNAVAILABLE.includes(name)) {
          onUnavailable(name, args.importer);
          return { path: join(pkgDir, "shims/empty.ts") };
        }
        return null;
      });
      build.onResolve({ filter: new RegExp(`^(${NODE_UNAVAILABLE.map((s) => s.replace("/", "\\/")).join("|")})$`) }, (args) =>
        build.resolve(args.path, { kind: args.kind, resolveDir: args.resolveDir, pluginName: "kairo-passthrough" }).then((r) => {
          if (r.errors.length === 0 && !r.external) return r;
          onUnavailable(args.path, args.importer);
          return { path: join(pkgDir, "shims/empty.ts") };
        }),
      );
    },
  };
}

export function defaultOut(entry) {
  const name = basename(entry).replace(/\.[cm]?[jt]sx?$/, "");
  return join(appRootOf(entry), ".kairo", `${name}.js`);
}

export async function bundle(entry, outfile, { minify = false } = {}) {
  mkdirSync(dirname(outfile), { recursive: true });
  const unavailable = new Map();
  const result = await esbuild({
    entryPoints: [entry],
    outfile,
    bundle: true,
    format: "esm",
    platform: "neutral",
    target: "es2022",
    mainFields: ["module", "main"],
    conditions: ["import", "module", "default"],
    external: ["kairo:*"],
    inject: [join(pkgDir, "src/globals.ts")],
    define: {
      "process.env.NODE_ENV": JSON.stringify(minify ? "production" : "development"),
      global: "globalThis",
    },
    plugins: [kairoPlugin({ onUnavailable: (name, importer) => unavailable.set(name, importer) })],
    minify,
    treeShaking: true,
    legalComments: "none",
    logLevel: "warning",
    metafile: true,
  });
  for (const [name, importer] of unavailable) {
    console.warn(`warning: Node built-in '${name}' is not available on a microcontroller (imported from ${importer}); it was replaced by an empty module`);
  }
  const inputs = Object.keys(result.metafile.inputs);
  const fromNodeModules = inputs.filter((p) => p.includes("node_modules/")).length;
  const bytes = statSync(outfile).size;
  console.log(`bundled ${inputs.length} modules (${fromNodeModules} from node_modules) -> ${outfile} (${(bytes / 1024).toFixed(1)} KB)`);
  return outfile;
}
