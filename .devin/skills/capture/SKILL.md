---
name: capture
description: Take deterministic screenshots (REPGAME_CAPTURE), diff captures against a baseline, run WASM captures via tools/wasm_capture.cjs, or run FLY/WALKTEST stress runs. Use when verifying rendering changes or reproducing a visual bug.
---

# Capture mode

Deterministic screenshots of exact camera views, env-var driven. With no env
vars set, behaviour is unchanged.

- `REPGAME_CAPTURE="x,y,z,yaw,pitch[,flags];x,y,z,yaw,pitch[,flags];..."` —
  one view per `;` group. yaw/pitch are raw `camera.angle_H`/`angle_V`.
  Presence enables capture mode: the camera, world time and draw quality are
  pinned each tick, HUD/ImGui/input are suppressed, and once the loader and
  light queues sit idle the view is screenshotted to
  `screenshots/capture_<i>_display.tga` (plus the other framebuffer dumps).
  The block-selection raycast still runs against the pinned camera, so the
  selection outline is captured exactly as seen when the view was F12'd.
  The process exits by itself after the last view. `player.dat` is not
  written.
- `REPGAME_CAPTURE_QUALITY` — `0..3` (LOW, MEDIUM, HIGH, X_HIGH), default 1.
- `REPGAME_CAPTURE_TIME` — world_time in ticks, default noon
  (`DAY_LENGTH_TICKS / 4`).
- `REPGAME_CAPTURE_SETTLE` — consecutive idle ticks required before shooting,
  default 60.
- `REPGAME_CAPTURE_TIMEOUT` — max ticks per view before shooting anyway,
  default `30 * UPS_RATE`.
- `REPGAME_CAPTURE_VIEW_TICKS` — `n>0`: views after the first shoot exactly
  `n` ticks after the camera jumps to them, without waiting for queue idle.
  View 0 keeps the normal idle-settle so the world is loaded before the
  sweep begins. Use with a `;` list of views at increasing yaw to emulate a
  fast mouse whip — each shot lands a fixed number of ticks after its jump,
  catching transition transients (stale culling/occlusion verdicts) that
  idle-settling would smooth over.
- `REPGAME_CAPTURE_ANOMALY` / `REPGAME_CAPTURE_PERIODIC` — forensic frame
  dumps: `ANOMALY=1` scans each frame's display for isolated bright
  outliers (`=2` additionally scans the scene attachment alpha, heavy),
  `PERIODIC=n` dumps all FBO attachments + display every n frames.

Per-view `flags` is a bitmask bound to `u_DebugFlags` in the chunk shader
(all platforms, including the WASM capture harness):

| Bit | Effect |
|-----|--------|
| 1   | Lighting off (`scene_light = 1`) |
| 2   | Texture detail off (resample at mip 4) |
| 4   | Per-block random rotation off |
| 8   | Fog off |
| 16  | Face shading off (`v_face_light = 1`) |
| 32  | Footprint-mipmapped light off (A/B the moire fix) |
| 64  | Screen-space face grow off (vertex-side flag applied by the C++ draw loop; works on all platforms) |
| 128 | Hashed alpha test + brightness dither off — plain 0.1 cutoff |
| 256 | Brightness dither off only (hashed alpha test still on) |

Pressing F12 in normal play logs the exact render camera in `REPGAME_CAPTURE`
format (`Capture view: REPGAME_CAPTURE="..."`) with full precision
(round-trip doubles/floats); capture mode renders that camera exactly, so the
captured frame reproduces what you saw down to the sub-pixel.

Example — same view twice, second with lighting disabled:

```sh
REPGAME_CAPTURE="-591.51,35.12,-407.47,0,0;-591.51,35.12,-407.47,0,0,1" \
    ./out/linux/release/RepGame World1 localhost
```

Compare `screenshots/capture_<i>_display.tga` between runs to diff.

**Bistability caveat:** `capture_0` flat captures are bistable run-to-run
(pre-existing nondeterminism, reproducible on the unmodified binary), so a
capture diff should be rerun once before being treated as a regression.

## WASM capture

`tools/wasm_capture.cjs` drives the same capture mode in a headless browser:
it serves `out/wasm/delivery` via `wasm_server.py` (COOP/COEP), loads the
page in Chrome, clicks play, and saves the views the game pushes onto
`window.repgameCaptures` as PNGs (plus a `console.log` of all browser
output).

```sh
node tools/wasm_capture.cjs --views "<REPGAME_CAPTURE spec>" --out <dir> \
    [--port 8091] [--msaa 0|1] [--env KEY=VAL ...] [--timeout 240] \
    [--webgpu swiftshader|vulkan|off]
```

Omit `--views` for fly/stress mode: no `REPGAME_CAPTURE` is set, the
`--env REPGAME_FLY*` vars drive the game, and the harness waits for the
exit-screen navigation (or `--timeout`) then writes `console.log` with the
FLYMET telemetry and any aborts. `--webgpu vulkan` drops the
`--use-webgpu-adapter=swiftshader` override so Dawn uses the real GPU
adapter in headless — needed to exercise the hardware ("amd") WebGPU path
that software adapters can't reproduce. Since the game's WebGPU backend is
opt-in (`REPGAME_WEBGPU=1`), the harness passes it whenever `--webgpu`
isn't `off`; `--env REPGAME_WEBGPU=0` overrides for a CPU-mapgen run.

- On WASM every `REPGAME_*` URL query parameter becomes an environment
  variable (`src/wasm/index.js` `preRun` copies them into Emscripten's
  `ENV`), so all the env vars work via `--env` or direct query params.
- `REPGAME_MSAA` — the WASM build requests a multisampled WebGL context
  (antialias, normally 4x) by default; `REPGAME_MSAA=0`/`--msaa 0` disables
  it for A/B comparison.
- `REPGAME_WEBGPU` — `0|1`: opt-in switch for the WebGPU terrain-gen/mesh
  backend (default 0 = CPU mapgen). `REPGAME_NO_WEBGPU_MAPGEN`/
  `REPGAME_NO_WEBGPU_MESH` still kill each side individually when the
  backend is on.
- WASM has no offscreen framebuffer, so captures read the default
  framebuffer and are delivered to JS instead of writing TGA files; the
  game keeps running on the last view instead of exiting (a WASM exit
  reloads the page).

## FLY stress runs

`REPGAME_FLY="x,y,z,yaw,pitch[,speed]"` pins the camera to a scripted
no-clip flight for load-in/lighting stress tests; `REPGAME_FLY_TICKS=n`
exits at tick n, `REPGAME_FLY_RD` applies a render-distance schedule, and
FLYMET telemetry lines log the loader/light counters (glossary and the
settle knobs `REPGAME_FLY_SETTLE`/`REPGAME_FLY_SETTLE_TIMEOUT`/
`REPGAME_FLY_FLY_TICKS` are in `docs/rendering.md`). Combine with
`SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy` for headless runs.
Prefer `rep-dbg fly` (debug-tools skill) — same env under the hood plus a
queryable `result.json`, so nobody has to parse FLYMET text.

Deterministic mob/item staging for captures: `REPGAME_MOB_TEST` /
`REPGAME_ITEM_TEST` (docs/mobs.md, docs/items-crafting.md); capture mode
freezes mob AI/physics and item integration. Movement-level debugging uses
`REPGAME_WALKTEST` (+ `REPGAME_TELEPORT`, `REPGAME_WALKTEST_TICKS`,
`REPGAME_SCREENSHOT_TICK`) — see `docs/env-vars.md`.
