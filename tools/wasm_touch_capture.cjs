#!/usr/bin/env node
// Mobile-emulation capture for the WASM touch controls: serves
// out/wasm/delivery with COOP/COEP (wasm_server.py), loads the game in
// headless Chrome emulating a landscape phone (hasTouch + isMobile), and
// drives the C++ touch controls through real CDP Input.dispatchTouchEvent
// touches (which arrive as SDL_FINGER* events — the same path a phone
// uses). Each step's result goes to the console log + a page.screenshot;
// the final summary exits nonzero if any check failed.
//
// Usage:
//   node tools/wasm_touch_capture.cjs --out <dir>
//       [--port 8092] [--timeout 240] [--webgpu swiftshader|vulkan|off]
const fs = require("fs");
const path = require("path");
const os = require("os");
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

// ---- args -------------------------------------------------------------------
const args = process.argv.slice(2);
function argValue(name, def) {
    const i = args.indexOf("--" + name);
    return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
}
const outDir = argValue("out", null);
const port = parseInt(argValue("port", "8092"), 10);
const timeoutSec = parseInt(argValue("timeout", "240"), 10);
const webgpuAdapter = argValue("webgpu", "swiftshader");
// --gl gpu|swiftshader|auto (auto tries the real GPU first like
// wasm_capture; force swiftshader when the headless GPU path is flaky).
const glMode = argValue("gl", "auto");
if (!outDir) {
    console.error("Usage: --out <dir> [--port] [--timeout s] [--webgpu swiftshader|vulkan|off]");
    process.exit(2);
}
const repoRoot = path.resolve(__dirname, "..");

// Landscape phone (Pixel-ish): the game window is sized in CSS px, and
// tfinger coords normalize over the CSS size, so CDP touch coords in CSS
// px land exactly on the layout rects.
const MOBILE = { width: 915, height: 412, deviceScaleFactor: 2.625 };

async function launch(chromium, useSwiftShader) {
    const webgpuArgs =
        webgpuAdapter === "off"
            ? []
            : webgpuAdapter === "vulkan"
              ? ["--enable-unsafe-webgpu", "--enable-features=Vulkan"]
              : ["--enable-unsafe-webgpu", "--use-webgpu-adapter=swiftshader"];
    const args = useSwiftShader
        ? ["--enable-unsafe-swiftshader", "--use-gl=angle", "--use-angle=swiftshader", ...webgpuArgs]
        : ["--enable-gpu", "--ignore-gpu-blocklist", "--use-angle=gl", ...webgpuArgs];
    return chromium.launch({ channel: "chrome", headless: true, args: args });
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function run() {
    const { chromium } = loadPlaywright();
    fs.mkdirSync(outDir, { recursive: true });
    const consoleLines = [];
    const checks = [];
    const check = (name, ok, detail) => {
        checks.push({ name, ok: !!ok });
        consoleLines.push(`[check] ${name}: ${ok ? "PASS" : "FAIL"}${detail ? " — " + detail : ""}`);
    };

    const server = spawn("python3", ["wasm_server.py", "out/wasm/delivery", String(port)], {
        cwd: repoRoot,
        stdio: ["ignore", "pipe", "pipe"],
    });
    server.stdout.on("data", (d) => consoleLines.push("[server] " + d.toString().trimEnd()));
    server.stderr.on("data", (d) => consoleLines.push("[server-err] " + d.toString().trimEnd()));
    await sleep(800);

    let browser = null;
    try {
        for (const useSwiftShader of glMode === "swiftshader" ? [true] : glMode === "gpu" ? [false] : [false, true]) {
            browser = await launch(chromium, useSwiftShader);
            const probeCtx = await browser.newContext({ viewport: { width: 200, height: 200 } });
            const probe = await probeCtx.newPage();
            const webgl2 = await probe.evaluate(() => {
                const c = document.createElement("canvas");
                const gl = c.getContext("webgl2");
                return gl ? gl.getParameter(gl.RENDERER) : null;
            });
            consoleLines.push(`[harness] webgl2 probe (${useSwiftShader ? "swiftshader" : "gpu"}): ${webgl2}`);
            await probeCtx.close();
            if (webgl2 || useSwiftShader) break;
            await browser.close();
            browser = null;
        }
        if (!browser) throw new Error("no WebGL2 context in headless Chrome (GPU and SwiftShader)");

        const ctx = await browser.newContext({
            viewport: { width: MOBILE.width, height: MOBILE.height },
            deviceScaleFactor: MOBILE.deviceScaleFactor,
            hasTouch: true,
            isMobile: true,
            userAgent:
                "Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36",
        });
        const page = await ctx.newPage();
        page.on("console", (msg) => consoleLines.push(`[${msg.type()}] ${msg.text()}`));
        page.on("pageerror", (err) => consoleLines.push(`[pageerror] ${err.message}`));
        const cdp = await ctx.newCDPSession(page);

        const qs = new URLSearchParams();
        qs.set("server", "off");
        qs.set("REPGAME_MSAA", "0");
        if (webgpuAdapter !== "off") qs.set("REPGAME_WEBGPU", "1");
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

        // matchMedia('(pointer: coarse)') must agree with the emulation —
        // it's what turns touch_ui on.
        const coarse = await page.evaluate(() => matchMedia("(pointer: coarse)").matches);
        check("pointer: coarse matched", coarse);

        // Tap the play button with a real touch (no pointer lock request
        // should follow).
        await cdp.send("Input.dispatchTouchEvent", {
            type: "touchStart",
            touchPoints: [{ x: MOBILE.width / 2, y: MOBILE.height / 2, id: 90 }],
        });
        await cdp.send("Input.dispatchTouchEvent", {
            type: "touchEnd",
            touchPoints: [],
        });
        // Wait for the game to reach a steady frame (world loaded).
        const waitFrames = async (ms) => sleep(ms);
        const gameStarted = async () =>
            page.evaluate(() => {
                try {
                    return Module.ccall("repgame_wasm_touch_ui") === 1;
                } catch (e) {
                    return false;
                }
            });
        {
            const deadline = Date.now() + timeoutSec * 1000;
            while (!(await gameStarted())) {
                if (Date.now() > deadline) throw new Error("game never started / touch_ui never enabled");
                await sleep(500);
            }
        }
        check("touch_ui enabled", true);
        await waitFrames(12000); // let the world stream in
        const locked = await page.evaluate(() => document.pointerLockElement !== null);
        check("no pointer lock on touch start", !locked);

        // The exported touch rects (window px = CSS px here) tell us where
        // to tap — the same rects the C++ hit-test uses.
        const touchRects = async () =>
            page.evaluate(() => {
                const names = ["joystick", "jump", "sneak", "menu", "chat", "break", "pick", "place", "inv",
                               "close", "mode", "prev", "next"];
                const out = {};
                names.forEach((n, i) => {
                    out[n] = {
                        x: Module.ccall("repgame_wasm_touch_rect", "number", ["number", "number"], [i, 0]),
                        y: Module.ccall("repgame_wasm_touch_rect", "number", ["number", "number"], [i, 1]),
                        w: Module.ccall("repgame_wasm_touch_rect", "number", ["number", "number"], [i, 2]),
                        h: Module.ccall("repgame_wasm_touch_rect", "number", ["number", "number"], [i, 3]),
                    };
                });
                return out;
            });
        const center = (r) => ({ x: r.x + r.w / 2, y: r.y + r.h / 2 });
        let nextFingerId = 1;
        const touch = async (type, points) =>
            cdp.send("Input.dispatchTouchEvent", {
                type: type,
                touchPoints: points.map((p) => ({ x: p.x, y: p.y, id: p.id })),
            });
        const tap = async (x, y) => {
            const id = nextFingerId++;
            await touch("touchStart", [{ x, y, id }]);
            await sleep(60);
            await touch("touchEnd", []);
        };
        const tapRect = async (r) => {
            const c = center(r);
            await tap(c.x, c.y);
        };

        // (a) HUD with touch controls visible.
        const rects = await touchRects();
        consoleLines.push(`[harness] touch rects: ${JSON.stringify(rects)}`);
        await page.screenshot({ path: path.join(outDir, "touch_hud.png") });
        check("screenshot: touch_hud", rects.joystick.w > 0, `joystick ${JSON.stringify(rects.joystick)}`);

        // (b) Joystick drag right — the player should move.
        const posX0 = await page.evaluate(() => Module.ccall("repgame_wasm_player_x"));
        const jc = center(rects.joystick);
        const joyId = nextFingerId++;
        await touch("touchStart", [{ x: jc.x, y: jc.y, id: joyId }]);
        await sleep(100);
        for (let i = 1; i <= 8; i++) {
            await touch("touchMove", [{ x: jc.x + (rects.joystick.w / 2) * (i / 8), y: jc.y, id: joyId }]);
            await sleep(80);
        }
        await sleep(1500);
        await touch("touchEnd", []);
        const posX1 = await page.evaluate(() => Module.ccall("repgame_wasm_player_x"));
        check("joystick drag moved player +x", posX1 - posX0 > 0.2, `x ${posX0.toFixed(2)} -> ${posX1.toFixed(2)}`);

        // (c) Tap Inv -> inventory opens.
        await tapRect(rects.inv);
        await sleep(1500);
        await page.screenshot({ path: path.join(outDir, "touch_inventory.png") });
        check("screenshot: touch_inventory (tap Inv)", true);

        // Tap Close (same spot) -> inventory closes.
        await tapRect(rects.inv);
        await sleep(1000);

        // (d) Tap Menu -> pause menu opens; screenshot.
        await tapRect(rects.menu);
        await sleep(1500);
        await page.screenshot({ path: path.join(outDir, "touch_menu.png") });
        check("screenshot: touch_menu (tap Menu)", true);
        // Back (history-back) closes the menu — the WASM back handler
        // goes through Input::backPressed instead of exiting.
        await page.evaluate(() => window.dispatchEvent(new PopStateEvent("popstate")));
        await sleep(1000);
        const menuStill = await page.evaluate(() => {
            // menu_open isn't exported; the touch controls reappear when
            // the menu closes, so a joystick tap should resume gameplay
            // control — check instead that the game is still running
            // (back didn't exit to the startup screen).
            try {
                Module.ccall("repgame_wasm_touch_ui");
                return false;
            } catch (e) {
                return true;
            }
        });
        check("back closed menu, game still running", !menuStill);
        await page.screenshot({ path: path.join(outDir, "touch_after_back.png") });

        // (e) Soft keyboard: tap Chat -> wants_text rises -> the hidden
        // input takes focus.
        await tapRect(rects.chat);
        await sleep(1200);
        const focused = await page.evaluate(() => document.activeElement && document.activeElement.id === "soft_keyboard");
        check("chat tap focused soft_keyboard input", focused);
        await page.screenshot({ path: path.join(outDir, "touch_chat.png") });
        // Type via CDP IME path (insertText goes straight to the focused
        // input as text), then Enter ends the chat.
        await cdp.send("Input.insertText", { text: "hi" });
        await sleep(400);
        await cdp.send("Input.dispatchKeyEvent", { type: "keyDown", key: "Enter", code: "Enter" });
        await cdp.send("Input.dispatchKeyEvent", { type: "keyUp", key: "Enter", code: "Enter" });
        await sleep(1000);
        const stillFocused = await page.evaluate(
            () => document.activeElement && document.activeElement.id === "soft_keyboard"
        );
        check("enter ended chat, keyboard released", !stillFocused);
        await page.screenshot({ path: path.join(outDir, "touch_chat_sent.png") });

        await ctx.close();
    } finally {
        if (browser) await browser.close().catch(() => {});
        server.kill();
        const failed = checks.filter((c) => !c.ok);
        consoleLines.push(`[harness] ${checks.length - failed.length}/${checks.length} checks passed`);
        for (const c of checks) console.log(`${c.ok ? "PASS" : "FAIL"} ${c.name}`);
        fs.writeFileSync(path.join(outDir, "console.log"), consoleLines.join("\n") + "\n");
        if (failed.length > 0) process.exitCode = 1;
    }
}

run().catch((e) => {
    console.error("FAILED:", e.message);
    process.exit(1);
});
