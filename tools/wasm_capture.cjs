#!/usr/bin/env node
// Headless WASM capture harness: serves out/wasm/delivery with COOP/COEP
// headers, loads the game in headless Chrome, clicks play, waits for the
// game to push every requested view onto window.repgameCaptures, and writes
// each one as a PNG (plus the full browser console to console.log).
//
// Usage:
//   node tools/wasm_capture.cjs --views "<REPGAME_CAPTURE spec>" --out <dir>
//       [--port 8091] [--msaa 0|1] [--env KEY=VAL ...] [--timeout 240]
//       [--webgpu swiftshader|vulkan]
//
// With --views omitted (fly/stress mode): REPGAME_CAPTURE is not set, the
// game runs whatever the --env REPGAME_FLY* vars script, and the harness
// waits for the exit-screen navigation or --timeout, then writes
// console.log — the FLYMET telemetry and any aborts/pageerrors.
const fs = require("fs");
const path = require("path");
const os = require("os");
const zlib = require("zlib");
const { spawn } = require("child_process");

function loadPlaywright() {
    try {
        return require("playwright");
    } catch (e) {}
    const npxDir = path.join(os.homedir(), ".npm", "_npx");
    for (const d of fs.readdirSync(npxDir)) {
        const p = path.join(npxDir, d, "node_modules", "playwright");
        try {
            return require(p);
        } catch (e) {}
    }
    throw new Error("playwright not resolvable (not in node_modules or ~/.npm/_npx)");
}

// ---- minimal PNG encoder (built-in zlib only) ------------------------------
const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c >>> 0;
    }
    return t;
})();
function crc32(buf) {
    let c = 0xffffffff;
    for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}
function pngChunk(type, data) {
    const out = Buffer.alloc(12 + data.length);
    out.writeUInt32BE(data.length, 0);
    out.write(type, 4);
    data.copy(out, 8);
    out.writeUInt32BE(crc32(Buffer.concat([Buffer.from(type), data])), 8 + data.length);
    return out;
}
function encodePNG(width, height, rgba /*Uint8Array, top-down rows*/) {
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(width, 0);
    ihdr.writeUInt32BE(height, 4);
    ihdr[8] = 8; // bit depth
    ihdr[9] = 6; // RGBA
    const stride = width * 4;
    const raw = Buffer.alloc((stride + 1) * height);
    for (let y = 0; y < height; y++) {
        raw[y * (stride + 1)] = 0; // filter: none
        Buffer.from(rgba.buffer, rgba.byteOffset + y * stride, stride).copy(raw, y * (stride + 1) + 1);
    }
    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        pngChunk("IHDR", ihdr),
        pngChunk("IDAT", zlib.deflateSync(raw, { level: 6 })),
        pngChunk("IEND", Buffer.alloc(0)),
    ]);
}

// ---- args -------------------------------------------------------------------
const args = process.argv.slice(2);
function argValue(name, def) {
    const i = args.indexOf("--" + name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
}
const views = argValue("views", null);
const outDir = argValue("out", null);
const port = parseInt(argValue("port", "8091"), 10);
const msaa = argValue("msaa", "1");
const timeoutSec = parseInt(argValue("timeout", "240"), 10);
const webgpuAdapter = argValue("webgpu", "swiftshader");
const extraEnvs = [];
for (let i = 0; i < args.length; i++) {
    if (args[i] === "--env" && i + 1 < args.length) extraEnvs.push(args[i + 1]);
}
if (!outDir) {
    console.error("Usage: --views <spec> --out <dir> [--port] [--msaa] [--env K=V] [--timeout s] [--webgpu]");
    process.exit(2);
}
const nViews = views ? views.split(";").filter((s) => s.trim().length > 0).length : 0;
const repoRoot = path.resolve(__dirname, "..");

async function launch(chromium, useSwiftShader) {
    // --use-webgpu-adapter=swiftshader gives a working WebGPU adapter even in
    // GPU-less headless environments (software Vulkan); --enable-unsafe-webgpu
    // removes the platform guard. Together they exercise the real WebGPU path
    // in CI even without a hardware adapter. --webgpu vulkan drops the
    // override so Dawn picks the real GPU adapter (the "amd" path that hit
    // the heap-growth OOM); it needs a working GPU in headless.
    const webgpuArgs =
        webgpuAdapter === "vulkan"
            ? ["--enable-unsafe-webgpu", "--enable-features=Vulkan"]
            : ["--enable-unsafe-webgpu", "--use-webgpu-adapter=swiftshader"];
    const args = useSwiftShader
        ? ["--enable-unsafe-swiftshader", "--use-gl=angle", "--use-angle=swiftshader", ...webgpuArgs]
        : ["--enable-gpu", "--ignore-gpu-blocklist", "--use-angle=gl", ...webgpuArgs];
    return chromium.launch({
        channel: "chrome",
        headless: true,
        args: args,
    });
}

async function run() {
    const { chromium } = loadPlaywright();
    fs.mkdirSync(outDir, { recursive: true });
    const consoleLines = [];

    // Serve the delivery dir with COOP/COEP (required for SharedArrayBuffer).
    const server = spawn("python3", ["wasm_server.py", "out/wasm/delivery", String(port)], {
        cwd: repoRoot,
        stdio: ["ignore", "pipe", "pipe"],
    });
    server.stdout.on("data", (d) => consoleLines.push("[server] " + d.toString().trimEnd()));
    server.stderr.on("data", (d) => consoleLines.push("[server-err] " + d.toString().trimEnd()));
    await new Promise((r) => setTimeout(r, 800));

    let browser = null;
    let captured = null;
    try {
        const userDir = fs.mkdtempSync(path.join(os.tmpdir(), "repgame-chrome-"));
        // Try GPU-backed WebGL first; if the context can't be created, retry
        // with SwiftShader.
        for (const useSwiftShader of [false, true]) {
            browser = await launch(chromium, useSwiftShader);
            const ctx = await browser.newContext({
                viewport: { width: 1600, height: 800 },
                deviceScaleFactor: 1,
                userDataDir: undefined, // persistent context not needed; fresh profile per launch
            });
            const page = await ctx.newPage();
            const webgl2 = await page.evaluate(() => {
                const c = document.createElement("canvas");
                const gl = c.getContext("webgl2");
                return gl ? gl.getParameter(gl.RENDERER) : null;
            });
            consoleLines.push(`[harness] webgl2 probe (${useSwiftShader ? "swiftshader" : "gpu"}): ${webgl2}`);
            await page.close();
            await ctx.close();
            if (webgl2 || useSwiftShader) {
                break;
            }
            await browser.close();
            browser = null;
        }
        if (!browser) throw new Error("no WebGL2 context in headless Chrome (GPU and SwiftShader)");

        const ctx = await browser.newContext({ viewport: { width: 1600, height: 800 }, deviceScaleFactor: 1 });
        const page = await ctx.newPage();
        page.on("console", (msg) => consoleLines.push(`[${msg.type()}] ${msg.text()}`));
        page.on("pageerror", (err) => consoleLines.push(`[pageerror] ${err.message}`));

        const qs = new URLSearchParams();
        qs.set("server", "off");
        if (views) qs.set("REPGAME_CAPTURE", views);
        qs.set("REPGAME_MSAA", msaa);
        for (const kv of extraEnvs) {
            const eq = kv.indexOf("=");
            if (eq > 0) qs.set(kv.slice(0, eq), kv.slice(eq + 1));
        }
        const url = `http://localhost:${port}/?${qs.toString()}`;
        consoleLines.push(`[harness] url: ${url}`);

        const moduleReady = new Promise((resolve) => {
            page.on("console", (msg) => {
                if (msg.text().includes("Module ready")) resolve();
            });
        });
        await page.goto(url, { waitUntil: "domcontentloaded", timeout: 60000 });
        await Promise.race([
            moduleReady,
            new Promise((_, rej) => setTimeout(() => rej(new Error("timeout waiting for 'Module ready'")), 90000)),
        ]);
        await page.click("#play_button");

        if (nViews === 0) {
            // Fly/stress mode: no captures expected. The game's exit path
            // calls show_exit_screen() -> location.replace(), which lands a
            // fresh navigation — wait for that or the deadline, whichever
            // comes first. FLYMET/crash output is in console.log.
            try {
                await page.waitForNavigation({ waitUntil: "load", timeout: timeoutSec * 1000 });
                consoleLines.push("[harness] game exited (exit-screen navigation)");
            } catch (e) {
                consoleLines.push(`[harness] deadline hit after ${timeoutSec}s without exit`);
            }
        } else {
            const deadline = Date.now() + timeoutSec * 1000;
            while (Date.now() < deadline) {
                const n = await page.evaluate(() => (window.repgameCaptures ? window.repgameCaptures.length : 0));
                if (n >= nViews) break;
                await new Promise((r) => setTimeout(r, 500));
            }
            const n = await page.evaluate(() => (window.repgameCaptures ? window.repgameCaptures.length : 0));
            if (n < nViews) {
                throw new Error(`timeout: got ${n}/${nViews} captures after ${timeoutSec}s`);
            }
            captured = await page.evaluate(() =>
                window.repgameCaptures.map((c) => {
                    let s = "";
                    const u = c.rgba;
                    for (let i = 0; i < u.length; i += 8192) {
                        s += String.fromCharCode.apply(null, u.subarray(i, Math.min(i + 8192, u.length)));
                    }
                    return { name: c.name, width: c.width, height: c.height, b64: btoa(s) };
                })
            );
        }
        await ctx.close();
    } finally {
        if (browser) await browser.close().catch(() => {});
        server.kill();
        fs.writeFileSync(path.join(outDir, "console.log"), consoleLines.join("\n") + "\n");
    }

    for (const c of captured || []) {
        const rgba = Buffer.from(c.b64, "base64");
        const stride = c.width * 4;
        // glReadPixels rows are bottom-up; flip to top-down.
        const top = Buffer.alloc(rgba.length);
        for (let y = 0; y < c.height; y++) {
            rgba.copy(top, y * stride, (c.height - 1 - y) * stride, (c.height - y) * stride);
        }
        const png = encodePNG(c.width, c.height, top);
        const file = path.join(outDir, `${c.name}.png`);
        fs.writeFileSync(file, png);
        console.log(`wrote ${file} (${c.width}x${c.height})`);
    }
}

run().catch((e) => {
    console.error("FAILED:", e.message);
    process.exit(1);
});
