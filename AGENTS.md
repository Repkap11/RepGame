# RepGame

Voxel game (C++17, SDL3, OpenGL). Build artifacts go to `out/`; the Linux
binary lands at `out/linux/release/RepGame`.

## Build / run

- `make linux` — release build for Linux
- `make linux-run` — build and launch (fullscreen)
- `make linux_debug` / `make linux_debug-run` — debug build
- `make android` — Android assets + build; `make android-run` to deploy to a device
- `make deploy` — build the portable Linux AppImage and rsync it to repkap11.com
- Run: `./out/linux/release/RepGame <WorldName> <host>` (e.g. `RepGame World1 localhost`)

## Shaders

Shaders in `src/shaders/*.glsl` are preprocessed at build time with
`gcc -E -P -x assembler-with-cpp -nostdinc -I include -DREPGAME_<PLATFORM>`
into `out/<platform>/shaders/`. All platforms compile the same shaders — the
`-DREPGAME_<PLATFORM>` flag only picks the chunk-dimension constants via
`include/common/chunk_dimensions.hpp`. LOW/MEDIUM/HIGH/X_HIGH are runtime
quality tiers, not shader variants; Android/WASM have no offscreen
framebuffer (`SUPPORTS_FRAME_BUFFER=0`), so they always render the forward
LOW-tier path (no reflections/post-process).

## Capture mode

Deterministic screenshots of exact camera views, env-var driven. With no env
vars set, behaviour is unchanged.

- `REPGAME_CAPTURE="x,y,z,yaw,pitch[,flags];x,y,z,yaw,pitch[,flags];..."` —
  one view per `;` group. yaw/pitch are raw `camera.angle_H`/`angle_V`.
  Presence enables capture mode: the camera, world time and draw quality are
  pinned each tick, HUD/ImGui/input are suppressed, and once the loader and
  light queues sit idle the view is screenshotted to
  `screenshots/capture_<i>_display.tga` (plus the other framebuffer dumps).
  The process exits by itself after the last view. `player.dat` is not
  written.
- `REPGAME_CAPTURE_QUALITY` — `0..3` (LOW, MEDIUM, HIGH, X_HIGH), default 1.
- `REPGAME_CAPTURE_TIME` — world_time in ticks, default noon
  (`DAY_LENGTH_TICKS / 4`).
- `REPGAME_CAPTURE_SETTLE` — consecutive idle ticks required before shooting,
  default 60.
- `REPGAME_CAPTURE_TIMEOUT` — max ticks per view before shooting anyway,
  default `30 * UPS_RATE`.
- `REPGAME_FACE_GROW_PX` — override the screen-space face grow in pixels
  (default 0.125, `CHUNK_FACE_GROW_PX`).
- `REPGAME_LIGHT_LOD_BIAS` / `REPGAME_LIGHT_FAR_AO` — override the
  footprint-mipmapped light tunables (ImGui "Light LOD bias" / "Distant AO"
  sliders, defaults 3.0 / 0.35). These apply at startup even without
  `REPGAME_CAPTURE`.
- `REPGAME_LIGHT_MIPMAPS` — `0|1`: runtime switch for the mipmapped light
  volume (RGBA8 + lit-cell mips + footprint-LOD shader sampling) vs the flat
  RG8 volume. Default: on for desktop builds, off on Android/WASM; the env
  var overrides on every platform (on WASM it's a `?REPGAME_LIGHT_MIPMAPS=1`
  URL param).
- `REPGAME_TEX_MIPMAPS` — `0|1`: block-atlas mipmaps (default 1; `0`
  restores NEAREST minification for A/B).

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

Pressing F12 in normal play logs the exact render camera in `REPGAME_CAPTURE`
format (`Capture view: REPGAME_CAPTURE="..."`) with full precision
(round-trip doubles/floats); capture mode renders that camera exactly, so the
captured frame reproduces what you saw down to the sub-pixel.

Example — same view twice, second with lighting disabled:

```sh
REPGAME_CAPTURE="-591.51,35.12,-407.47,0,0;-591.51,35.12,-407.47,0,0,1" \
    ./out/linux/release/RepGame World1 localhost
```

### WASM capture

`tools/wasm_capture.cjs` drives the same capture mode in a headless browser:
it serves `out/wasm/delivery` via `wasm_server.py` (COOP/COEP), loads the
page in Chrome, clicks play, and saves the views the game pushes onto
`window.repgameCaptures` as PNGs (plus a `console.log` of all browser
output).

```sh
node tools/wasm_capture.cjs --views "<REPGAME_CAPTURE spec>" --out <dir> \
    [--port 8091] [--msaa 0|1] [--env KEY=VAL ...] [--timeout 240]
```

- On WASM every `REPGAME_*` URL query parameter becomes an environment
  variable (`index.js` `preRun` copies them into Emscripten's `ENV`), so all
  the env vars above work via `--env` or direct query params.
- `REPGAME_MSAA` — the WASM build requests a multisampled WebGL context
  (antialias, normally 4x) by default; `REPGAME_MSAA=0`/`--msaa 0` disables
  it for A/B comparison.
- WASM has no offscreen framebuffer, so captures read the default
  framebuffer and are delivered to JS instead of writing TGA files; the
  game keeps running on the last view instead of exiting (a WASM exit
  reloads the page).
