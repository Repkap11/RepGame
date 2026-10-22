# RepGame

Voxel game (C++17, SDL3, OpenGL). Build artifacts go to `out/`; the Linux
binary lands at `out/linux/release/RepGame`.

## Build / run

- `make linux` — release build for Linux
- `make linux-run` — build and launch (fullscreen)
- `make linux_debug` / `make linux_debug-run` — debug build
- `make android` — Android assets + build; `make android-run` to deploy to a device
- `make deploy` — build the portable Linux AppImage and rsync it to repkap11.com
- `make flatpak` — build + install the flatpak (`flatpak run com.repkap11.repgame`).
  The flatpak `command:` is `flatpak/RepGameRun`, a wrapper like the AppImage's
  AppRun: no args → `RepGame_uncompressed World1 repkap11.com` (public server);
  explicit args pass through.
- `make gpu-prebuilt` — compile HIP/CUDA objects + harvest runtime libs in Docker
  into `prebuilt/gpu` (gitignored). `make flatpak` depends on this: the flatpak
  sandbox has no hipcc/nvcc, so its manifest links the prebuilt objects via
  `GPU_PREBUILT_DIR` (see hip.mk/cuda.mk) and installs the runtime libs the
  freedesktop runtime lacks into `/app/lib` (see `flatpak/com.repkap11.repgame.yml`
  for the whitelist — do NOT bundle runtime-provided libs like libdrm_amdgpu,
  they shadow the GL extension's copies and break EGL).
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
- `REPGAME_FACE_GROW_PX` — override the screen-space face grow in pixels
  (default 0.125, `CHUNK_FACE_GROW_PX`).
- `REPGAME_FOG_RANGE` — width of the distance-fog band in blocks (default
  96). The band ends at the render edge so pop-in stays hidden, and is
  capped at 38% of render distance so small render distances look
  unchanged.
- `REPGAME_RENDER_DISTANCE` — initial horizontal render distance in chunks
  (1..`CHUNK_RADIUS_XZ`, default `CHUNK_RADIUS_XZ_DEFAULT` = half the max). The chunk torus is always allocated
  at the compile-time `CHUNK_RADIUS_*`; the runtime radius is an active
  window inside it — out-of-window slots park (`load_state` =
  `CHUNK_PARKED`/`CHUNK_PARKED_UNFINALIZED`, so light/draw/tick paths treat
  them as unloaded) and reactivate on growth (`CHUNK_PARKED_UNFINALIZED`
  finalizes without regenerating when terrain already streamed in while
  parked). Adjust live with `=`/`+` and `-` (numpad too); `0` resets to
  `RENDER_RADIUS_DEFAULT`. The fog edge
  tracks the active radius, not the torus capacity. The y radius is fixed
  at compile time. The chosen radius persists in `player.dat`
  (`PlayerData::render_distance`, clamped to 1..max on load — absent in
  old saves); `REPGAME_RENDER_DISTANCE` overrides it when set.
- `LIGHT_LOD_BIAS` / `LIGHT_FAR_AO` — footprint-mipmapped light tunables
  (3.0 / 0.1), compile-time `#define`s at the top of
  `src/shaders/chunk_fragment.glsl` (previously env vars / ImGui sliders —
  folded to constants so the shader compiler collapses them into the lod
  expression). The effective mip level is capped at `LIGHT_LOD_MAX`, so
  raising the bias past the cap has no further effect.
- `REPGAME_NIGHT_AMBIENT` / `REPGAME_MIN_AMBIENT` — night brightness floors
  (0-1, defaults 0.08 / 0.05). `night_ambient` is the moonlight floor on the
  daylight factor (outdoor night: terrain, sky, mobs, fog all scale by it);
  `min_ambient` is the absolute light floor so caves aren't pure black. Not
  HDR-related — these are sRGB-encoded light values, identical in SDR and
  HDR.
- `REPGAME_LIGHT_MIPMAPS` — `0|1`: runtime switch for the mipmapped light
  volume (RGBA8 + air-mask mips + footprint-LOD shader sampling) vs the flat
  RG8 volume. Default: on everywhere; the env var overrides on every
  platform (on WASM it's a `?REPGAME_LIGHT_MIPMAPS=1` URL param).
- `REPGAME_TEX_MIPMAPS` — `0|1`: block-atlas mipmaps (default 1; `0`
  restores NEAREST minification for A/B). Mips are generated on the CPU in
  `texture.cpp` (`fill_transparent_rgb`/`downsample_tile`): transparent
  texels get their rgb filled from coloured neighbours, levels store
  un-premultiplied (alpha-weighted) rgb, and binary cutout tiles (leaves,
  flowers, grass) store box-averaged alpha (exact coverage) with
  whole-texel subsampled rgb so the authored palette survives. Tiles with
  partial alpha (glass, water) keep plain filtered downsampling. The
  fragment shader alpha-tests with a hashed per-texel cutoff — sampled
  alpha IS coverage and survival is linear in it, stable across mip
  blends — and re-injects a hashed brightness dither ramped in past mip 0
  to replace the texel-frequency contrast linear filtering can't carry.
  All hash math must stay `highp`: bare `uint` is mediump in ES 300, which
  is 16-bit on Mesa/AMD and silently collapses the hash to ~0 (the test
  degenerates to the fixed cutoff and the dither pins dark).
- `CUTOUT_FADE_START` / `CUTOUT_FADE_END` — texel-lod window (lod = log2 of
  texels per pixel, so 0 is 1:1 and 3 is 8 texels/px) over which the hashed
  cutout test converges to solid: distant canopy fills in smoothly instead
  of rendering sub-pixel holes as salt-and-pepper noise, and the brightness
  dither fades out on the same schedule. Values 0.3 / 1.2 — compile-time
  `#define`s at the top of `src/shaders/chunk_fragment.glsl` (previously
  `u_CutoutFade` env-tunable uniform).
- `REPGAME_FLY="x,y,z,yaw,pitch[,speed]"` — scripted no-clip flight for
  load-in/lighting stress tests. Each tick pins the camera to
  `start + look*speed*t` (speed in blocks/tick; sprint-fly is 2.0 —
  anything much faster outruns terrain gen and every result returns
  out-of-window, re-enqueueing forever: `tw` stays full, nothing
  finalizes). Emits one `FLYMET` line per `REPGAME_FLY_METRICS` ticks
  (default 1) carrying the loader/light counters (`j`=jobs `p`=pending
  `c`=claims `d`=dirty `x`=dropped `s`=BFS seeds, `pri`/`stv` = open
  priority/starve windows, plus the `light_dbg` counters (of which
  `drop`=pop-time dead targets, `pd`=push-time dead targets, `cap`=far-seed
  cap refusals — see the backlog cap below, `def`=write-hot dirty-upload
  deferrals — see the upload debounce in lighting-thread-plan.md) and
  per-phase µs). `REPGAME_FLY_TICKS=n` exits at tick n via the normal
  cleanup path. `player.dat` is not written. Combine with
  `SDL_VIDEODRIVER=offscreen` for headless runs.
- `REPGAME_LIGHT_CLAIM_RADIUS` — `1..4` (default 1): Chebyshev distance
  between light-job center columns that counts as a claim conflict. 4
  reproduces the old 5x5-key-overlap semantics; 1 lets jobs two columns
  apart run concurrently (their touches in the shared middle chunk stay on
  opposite edges).
- `REPGAME_LIGHT_WORKERS` — `1..8` (default 4): light worker pool size.

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
| 128 | Hashed alpha test + brightness dither off — plain 0.1 cutoff (the old look) |
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
