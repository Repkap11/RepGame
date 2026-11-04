# Rendering, shaders and the pause menu

## Shaders

Shaders in `src/shaders/*.glsl` are preprocessed at build time with
`gcc -E -P -x assembler-with-cpp -nostdinc -I include -DREPGAME_<PLATFORM>`
into `out/<platform>/shaders/`. All platforms compile the same shaders — the
`-DREPGAME_<PLATFORM>` flag only picks the chunk-dimension constants via
`include/common/chunk_dimensions.hpp`. LOW/MEDIUM/HIGH/X_HIGH are runtime
quality tiers, not shader variants; Android/WASM have no offscreen
framebuffer (`SUPPORTS_FRAME_BUFFER=0`), so they always render the forward
LOW-tier path (no reflections/post-process).

## Pause menu

- Escape (desktop) or the "Menu" button / Back (Android) opens the pause menu
  — it no longer quits directly. Pressing it again resumes; Back/inv while
  the inventory is open closes the inventory instead.
- Buttons are grouped into lines of one or two side-by-side cells: Resume;
  Graphics quality | Shadows (Graphics is the `r` key quality, absent where
  `SUPPORTS_FRAME_BUFFER` is 0 — Android/WASM always render the LOW path,
  leaving Shadows full-width); Render Distance `-`/`+` (clamped to
  `1..RENDER_RADIUS_MAX`; clicking the label resets to
  `RENDER_RADIUS_DEFAULT`, same as `0`); Cave Brightness `-`/`+` (5% steps,
  clamped 0..100% of `min_ambient`; clicking the label resets to the 5%
  default — the value persists in `player.dat` and `REPGAME_MIN_AMBIENT`
  overrides the saved value); Game Mode | Flying; Block Debug View (`m`) |
  Debug Overlay (only where `SUPPORTS_IMGUI_OVERLAY`, else Block Debug
  View alone); and Exit Game (`input.exitGame`).
- In-game text uses ProggyClean (`fonts/proggyclean.ttf`) drawn at integer
  multiples of 13px with nearest filtering (`FontRenderer::pixelScale`).

## Render distance, fog and light

The chunk lifecycle behind render distance (torus, parking, streaming)
is `docs/chunks.md`; the light model these tunables feed is
`docs/lighting.md`.

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
  `CHUNK_PARKED`, so light/draw/tick paths treat them as unloaded). Parking
  retains NOTHING: dirty chunks persist through the normal work queue, then
  every owned buffer is released (`Chunk::release_terrain`), so memory
  follows the active window instead of accumulating toward the torus
  capacity. Re-entry reloads the save or regenerates. Adjust live with
  `=`/`+` and `-` (numpad too); `0` resets to
  `RENDER_RADIUS_DEFAULT`. The fog edge
  tracks the active radius, not the torus capacity. The y radius is fixed
  at compile time. The chosen radius persists in `player.dat`
  (`PlayerData::render_distance`, clamped to 1..max on load — absent in
  old saves); `REPGAME_RENDER_DISTANCE` overrides it when set.
- `LIGHT_LOD_BIAS` / `LIGHT_FAR_AO` — footprint-mipmapped light tunables
  (3.0 / 0.1), compile-time `#define`s at the top of
  `src/shaders/chunk_fragment.glsl`. The effective mip level is capped at
  `LIGHT_LOD_MAX`, so raising the bias past the cap has no further effect.
- `REPGAME_NIGHT_AMBIENT` / `REPGAME_MIN_AMBIENT` — night brightness floors
  (0-1, defaults 0.20 / 0.05). `night_ambient` is the moonlight floor on the
  daylight factor (outdoor night: terrain, sky, mobs, fog all scale by it);
  `min_ambient` is the cave light floor so caves aren't pure black. It is a
  linear lift, not a clamp: `light = mix(min_ambient, 1, raw)`, so falloff
  gradients stay smooth at high settings and full light stays 1.0. It is
  also adjustable live from the pause menu (Cave Brightness row) and
  persisted in `player.dat`. Not
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
  `#define`s at the top of `src/shaders/chunk_fragment.glsl`.
- `REPGAME_LIGHT_CLAIM_RADIUS` — `1..4` (default 1): Chebyshev distance
  between light-job center columns that counts as a claim conflict.
- `REPGAME_LIGHT_WORKERS` — `1..8` (default 4): light worker pool size.
- `REPGAME_OCCLUSION` — `0|1` (default 1): kill switch for the hardware
  occlusion-culling probe pass.
- `REPGAME_WINDOW_MSAA` — `0` requests a non-multisampled desktop window
  (default on, `MSAA_SAMPLES` samples).
- `REPGAME_VALIDATE_INSTANCES` — when set, sanity-check every BlockCoords
  instance before GPU upload (catches host-memory corruption).
- `REPGAME_NO_HIP` / `REPGAME_NO_HIP_MAPGEN` / `REPGAME_NO_HIP_MESH` —
  force the CPU path for the HIP GPU backend, or for its mapgen / mesh
  halves individually (used to isolate GPU memory corruption).
- `REPGAME_NO_WEBGPU_MAPGEN` / `REPGAME_NO_WEBGPU_MESH` — same idea for the
  WASM WebGPU backend (only consulted when `REPGAME_WEBGPU=1`).

## Fly mode (load-in / lighting stress runs)

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
  cap refusals — see the backlog cap in `docs/lighting-thread-plan.md`,
  `def`=write-hot dirty-upload deferrals — see the upload debounce in
  `docs/lighting-thread-plan.md`) and per-phase µs). `rd` is the active
  render radius and `cl` the count of slots with terrain work in flight
  (a nonzero `cl` keeps the settle gate from opening).
  `REPGAME_FLY_RD="tick:radius,tick:radius"` applies scripted
  render-distance changes at those fly ticks (e.g. `200:2,500:8`) — the
  schedule evaluates against flight time, so it exercises the park/release
  and re-entry paths while streaming continues.
  `REPGAME_FLY_TICKS=n` exits at tick n via the normal
  cleanup path. `player.dat` is not written. Combine with
  `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy` for headless runs
  (the dummy audio driver keeps test runs from playing music/sfx).
- `REPGAME_FLY_SETTLE` — consecutive idle loader/light ticks required at
  the start position before the flight begins (default 0 = fly
  immediately). `REPGAME_FLY_SETTLE_TIMEOUT` caps the wait (default
  `30 * UPS_RATE`).
- `REPGAME_FLY_FLY_TICKS` — flight duration in ticks; the camera then holds
  its final position (default -1 = fly until `REPGAME_FLY_TICKS` exits).
