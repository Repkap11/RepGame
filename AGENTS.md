# RepGame

Voxel game (C++17, SDL3, OpenGL). Build artifacts go to `out/`; the Linux
binary lands at `out/linux/release/RepGame`.

## Build / run

- `make linux` — release build for Linux
- `make arm` — cross-compile the Linux client for the aarch64 Yocto board
  (PowerVR, GLES 3.2, Weston/Wayland); binary lands at `out/arm/release/RepGame`.
  See "ARM build" below.
- `make linux-run` — build and launch (fullscreen)
- `make linux-run-blocks` — build and launch the block world
  (`REPGAME_BLOCK_WORLD=1`, see "Blocks & textures")
- `make linux_debug` / `make linux_debug-run` — debug build
- `make android` — Android assets + build; `make android-run` to deploy to a device
- `make deploy` — build the portable Linux AppImage and rsync it to repkap11.com
- `make steamframe` / `make steamframe_debug` — aarch64 GLES build for Valve's
  Steam Frame against the pinned Steam Runtime sysroot; binary lands at
  `out/steamframe/<release|debug>/RepGame`. Static SDL3 + static OpenXR loader,
  and the output is checked to stay glibc ≤ 2.31 with no `libstdc++`/`libgcc_s`
  NEEDED entries. See "Steam Frame build" below.
- `make steamframe-dist` — `out/steamframe/dist/` payload for upload.
- `make steamframe-sysroot` / `make steamframe-sdl3` / `make clean-steamframe-deps`
  — fetch the sysroot, (re)build SDL3, wipe both.
- `make openxr-host` / `make openxr-steamframe` — static OpenXR-SDK 1.1.63
  loader installs under `out/openxr-{host,steamframe}/install`.
- `make linux REPGAME_OPENXR=1` — desktop VR build at `out/linux-openxr/`;
  needs an OpenXR runtime offering `XR_MNDX_egl_enable` (e.g. Monado —
  SteamVR for Linux x64 lacks it). See "VR (OpenXR)" below.
- `make flatpak` — build + install the flatpak (`flatpak run com.repkap11.repgame`).
  The flatpak `command:` is `flatpak/RepGameRun`, a wrapper like the AppImage's
  AppRun: no args → `RepGameBin World1 repkap11.com` (public server);
  explicit args pass through.
- `make gpu-prebuilt` — compile HIP/CUDA objects + harvest runtime libs in Docker
  into `prebuilt/gpu` (gitignored). `make flatpak` depends on this: the flatpak
  sandbox has no hipcc/nvcc, so its manifest links the prebuilt objects via
  `GPU_PREBUILT_DIR` (see hip.mk/cuda.mk) and installs the runtime libs the
  freedesktop runtime lacks into `/app/lib` (see `flatpak/com.repkap11.repgame.yml`
  for the whitelist — do NOT bundle runtime-provided libs like libdrm_amdgpu,
  they shadow the GL extension's copies and break EGL).
- Run: `./out/linux/release/RepGame <WorldName> <host>` (e.g. `RepGame World1 localhost`)

## Pause menu

- Escape (desktop) or the "Menu" button / Back (Android) opens the pause menu
  — it no longer quits directly. Pressing it again resumes; Back/inv while
  the inventory is open closes the inventory instead.
- Rows: Resume, Graphics quality (same as `r`; hidden where
  `SUPPORTS_FRAME_BUFFER` is 0 — Android/WASM always render the LOW path),
  Render Distance `-`/`+` (clamped to `1..RENDER_RADIUS_MAX`; clicking the
  label resets to `RENDER_RADIUS_DEFAULT`, same as `0`), Block Debug
  View (`m`), Game Mode, Flying, Debug Overlay (only where
  `SUPPORTS_IMGUI_OVERLAY`), and Exit Game (`input.exitGame`).
- In-game text uses ProggyClean (`fonts/proggyclean.ttf`) drawn at integer
  multiples of 13px with nearest filtering (`FontRenderer::pixelScale`).

## ARM build (aarch64 Yocto board, GLES/Wayland)

Cross-compiles the Linux client for a Yocto aarch64 board (PowerVR GLES 3.2,
Weston) using a Yocto SDK on the host. `REPGAME_GLES` selects the GLES port:
GLES headers instead of glew, an ES 3.2 context, `glTexStorage2DMultisample`
resizes + `glCopyImageSubData` for the depth snapshot, and the small chunk
radii (same as Android/WASM) since the board can't carry the desktop torus.
SDL3 is built static from source under `out/arm-sdl3` with the SDK toolchain.
arm.mk is a standalone platform makefile, so arm can be built in the same
invocation as other platforms (e.g. `make linux arm`).

- `make arm` / `make arm_debug` — release/debug binaries in `out/arm/`.
- `make arm-sdl3` — (re)build the static SDL3.
- `make arm-deploy` — rsync the release binary to `$(ARM_DEVICE):$(ARM_DEPLOY_DIR)`.
- `make arm-run` / `make arm_debug-run` — deploy + run over ssh.
- Overrides: `ARM_SDK` (Yocto SDK path), `ARM_DEVICE` (default
  `root@renesas-v4h`), `ARM_DEPLOY_DIR`, `ARM_SDL3_VERSION`.
- Running over ssh needs Wayland env vars:
  `XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1`.
- The device has no working DNS, so `arm-run`'s public-server connect fails —
  use `arm_debug-run` (single-player) or plain ssh.

## Steam Frame build (aarch64, GLES, OpenXR)

Cross-compiles the Linux client for the Steam Frame (aarch64, GLES) against a
pinned Steam Runtime 3 ARM64 sysroot. The sysroot snapshot is
`3.0.20260602.240810` (`com.valvesoftware.SteamRuntime.Sdk-arm64-sniper-sysroot`)
— later steamrt3 snapshots publish no arm64 images, so `SF_SNAPSHOT` should not
be bumped blindly. A host `wayland-scanner` 1.18 is built to match the sysroot's
wayland version. SDL3 and the OpenXR loader are linked static; the makefile
verifies the result is glibc ≤ 2.31 and pulls no `libstdc++`/`libgcc_s`.

- `make steamframe` / `make steamframe_debug` — `out/steamframe/<release|debug>/RepGame`.
- `make steamframe-dist` — `out/steamframe/dist/RepGame` upload payload.
- Deploy: upload `out/steamframe/dist` with the SteamOS Devkit Client, runtime
  "Steam Linux Runtime 3.0 ARM64 (Sniper)", start command
  `RepGame World1 repkap11.com`.
- Overrides: `SF_SNAPSHOT`, `SF_SYSROOT_DIR`, `SF_SDL3_VERSION`, `SF_OPENXR=0`.

## VR (OpenXR)

Builds compiled with `REPGAME_OPENXR` try OpenXR at startup unless
`REPGAME_VR=0` or `REPGAME_CAPTURE` is set; failure logs
`VR unavailable, running flat` and continues as the normal flat client.
The session binds OpenGL through `XR_MNDX_egl_enable` + the SDL EGL context
(`SDL_HINT_VIDEO_FORCE_EGL`), so the runtime must offer that extension.

Tracking runs in LOCAL space: the head pose folds into `angle_H`/`angle_V`
and a `body_yaw` that aims the body, controller thumbsticks/buttons land on the
shared `Input` fields each tick (`process_vr_input`, edge-detected), the aim
pose drives both the block-selection raycast and the UI laser pointer.
`draw_vr` renders each eye into its swapchain FBO via
`FrameBuffer::set_display`; eye 0 runs the occlusion probe pass and eye 1
reuses its results. The HUD lives on an `XrCompositionLayerQuad`: head-locked
while playing, pinned world-locked in front of the head while the inventory is
open; the aim ray is projected onto the quad so clicks hit inventory slots.

- `REPGAME_VR` — `0` disables VR entirely (default on in OpenXR builds).
- `REPGAME_VR_QUALITY` — world draw quality in VR, `0..3` (default 0 = LOW);
  the flat preference is kept and restored, `r` still cycles live.
- `REPGAME_VR_RENDER_SCALE` — per-eye resolution scale (default 1.0, clamped
  to the runtime max).
- `REPGAME_VR_MIRROR` — blit eye 0 to the desktop window (default 1 on
  desktop, 0 on Steam Frame).
- `REPGAME_VR_SNAP_DEGREES` — snap-turn angle (default 30).
- `REPGAME_VR_SMOOTH_TURN` — smooth turn rate in deg/sec (default 0 = snap).
- `REPGAME_VR_UI_WIDTH` / `REPGAME_VR_UI_DIST` — UI quad size and distance in
  meters (default 1.0 / 1.0).

Controller actions → bindings (suggested per profile, see
`create_action_set` in `src/linux/openxr.cpp`):

| Action | Frame controller | Touch | khr simple |
|--------|------------------|-------|------------|
| move | L thumbstick | L thumbstick | — |
| turn | R thumbstick | R thumbstick | — |
| jump | R a/click | R a/click | — |
| sneak | L stick/click | L stick/click | — |
| sprint | L squeeze | L squeeze | — |
| break_block | R trigger | R trigger | R select |
| place_block | L trigger | L trigger | L select |
| pick_block | R squeeze | R squeeze | — |
| hotbar_next | R b/click | R b/click | — |
| hotbar_prev | R x/click | L x/click | — |
| inventory | R y/click | L y/click | R menu |
| toggle_fly | L dpad_up | R stick/click | — |
| aim | R aim/pose | R aim/pose | R aim/pose |
| haptic | R haptic | R haptic | R haptic |

Note: `capture_0` flat captures are bistable run-to-run (pre-existing
nondeterminism, reproducible on the unmodified binary), so a capture diff
should be rerun once before being treated as a regression.

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
  `src/shaders/chunk_fragment.glsl` (previously env vars / ImGui sliders —
  folded to constants so the shader compiler collapses them into the lod
  expression). The effective mip level is capped at `LIGHT_LOD_MAX`, so
  raising the bias past the cap has no further effect.
- `REPGAME_NIGHT_AMBIENT` / `REPGAME_MIN_AMBIENT` — night brightness floors
  (0-1, defaults 0.20 / 0.05). `night_ambient` is the moonlight floor on the
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
  per-phase µs). `rd` is the active render radius and `cl` the count of
  slots with terrain work in flight (a nonzero `cl` keeps the settle gate
  from opening). `REPGAME_FLY_RD="tick:radius,tick:radius"` applies
  scripted render-distance changes at those fly ticks (e.g. `200:2,500:8`)
  — the schedule evaluates against flight time, so it exercises the
  park/release and re-entry paths while streaming continues.
  `REPGAME_FLY_TICKS=n` exits at tick n via the normal
  cleanup path. `player.dat` is not written. Combine with
  `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy` for headless runs
  (the dummy audio driver keeps test runs from playing music/sfx).
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
  variable (`index.js` `preRun` copies them into Emscripten's `ENV`), so all
  the env vars above work via `--env` or direct query params.
- `REPGAME_MSAA` — the WASM build requests a multisampled WebGL context
  (antialias, normally 4x) by default; `REPGAME_MSAA=0`/`--msaa 0` disables
  it for A/B comparison.
- `REPGAME_WEBGPU` — `0|1`: opt-in switch for the WebGPU terrain-gen/mesh
  backend (default 0 = CPU mapgen). Set `?REPGAME_WEBGPU=1` in the page URL
  to enable; `REPGAME_NO_WEBGPU_MAPGEN`/`REPGAME_NO_WEBGPU_MESH` still kill
  each side individually when the backend is on.
- WASM has no offscreen framebuffer, so captures read the default
  framebuffer and are delivered to JS instead of writing TGA files; the
  game keeps running on the last view instead of exiting (a WASM exit
  reloads the page).

## Audio

OGG Vorbis decoded by vendored `stb_vorbis` (`include/common/vendor/`).
Two backends in `src/common/audio.cpp`: SDL3 device-bound `SDL_AudioStream`s
(Linux, Windows, ARM, SteamFrame, embedded blobs) and an SDL2 main-thread
mix callback (WASM — no blob embedding, sounds ship in the WASMFS preload).
Only Android compiles the no-op stubs from `include/common/audio.hpp`.

Pipeline (mirrors the bitmap blobs): `tools/sounds_manifest.txt` lists
`name|source|type|title` per sound (`source` is a file under `sounds/`,
which is gitignored — the music is copyrighted and stays local). `make`
runs `tools/convert_sounds.sh convert <name>` per entry (ffmpeg →
`out/sounds/<name>.ogg`, needs the `ffmpeg` package), embeds each as a
`MK_BLOB(out_sounds, <name>, ogg)` object (WASM instead copies it into the
`out/wasm/fs/sounds/` preload tree), and regenerates
`out/include/sounds_manifest.hpp` (music/sfx tables included by
`src/common/audio.cpp` — `REPGAME_WASM` selects path- instead of
blob-carrying tables). If `sounds/` is absent, `REPGAME_AUDIO` defaults
to `0` and the build compiles the stub — set `REPGAME_AUDIO=0` to force it
even with sources present. The flatpak sandbox has no ffmpeg, so
`make flatpak` runs `sounds-prebuilt` on the host first (into
`prebuilt/sounds/`, same pattern as `gpu-prebuilt`) and the sandbox links
those via `SOUND_OGG_DIR=prebuilt/sounds`.

Runtime: `audio_init` opens the default playback device (failure → silent,
logged once). Music is streamed — decoded frames are pulled from stb_vorbis
on demand (SDL3: the audio thread fills the persistent music stream; WASM:
the mix callback, which Emscripten runs on the main thread) — `audio_tick`
picks the next random track after a uniform `[gap/4, gap]` gap (the first
track starts ~3s in). SFX are one-shots (SDL3 binds a stream per play, WASM
claims a voice slot); manifest names `dig_stone_1..4` form
the `dig_stone` pool, so `audio_play_sfx("dig_stone")` picks a random
variant. `audio_block_dig_pool(block_id)` maps BlockID → pool
(dig_stone/grass/gravel/sand/snow/wood/coral, glass) — the break and place
hooks both use it; `audio_block_step_pool(block_id)` is the same map for
footsteps (step_* pools, glass → step_stone). Currently wired: block break +
place → per-material dig pool, footsteps → per-material step pool (every
~0.7 blocks of grounded horizontal motion + on landing, in
process_movement), chest open → `chest`, close → `chest_close`, TNT ignite
→ `fuse`, detonation → `explode`. The ImGui overlay shows `Music: <title>`
while a track plays.

IMPORTANT: the device is opened with `SDL_OpenAudioDevice` and the music
stream is *bound* to it — do NOT use `SDL_OpenAudioDeviceStream`, whose
device refuses further stream bindings and silently kills all SFX. On WASM
the browser suspends the AudioContext until the first user gesture; SDL2's
Emscripten driver auto-resumes it (the callback keeps running into a
throwaway buffer meanwhile).

Sources: `sounds/mc_pack` is a Minecraft assets tree (WAV) — only the
overworld music (game/, water/, creative/) is in the rotation; nether/end/
menu/records are excluded. `sounds/music` (the older MP3/FLAC soundtrack)
and `sounds/sfx` (the earlier Kenney stand-ins) are not referenced by the
manifest anymore.

- `REPGAME_AUDIO` — `0` disables all audio (default on when built with
  audio). Audio is also skipped entirely in capture mode.
- `REPGAME_MUSIC` — `0` mutes music only (SFX still play).
- `REPGAME_MUSIC_VOLUME` / `REPGAME_SFX_VOLUME` — linear gains 0..1
  (defaults 0.5 / 1.0).
- `REPGAME_MUSIC_GAP` — max seconds of silence between tracks (default
  240; gap is uniform in `[gap/4, gap]`, `0` = continuous shuffle).

## Blocks & textures (data-driven)

Block definitions live one JSON file per `BlockID` in `data/blocks/<name>.json`.
`id` is the persisted/network block id — never renumber; `LAST_BLOCK_ID` is
2047 (the maximum `BlockCell` can hold via `BLOCK_CELL_ID_MASK`). Ids may
contain gaps — `block_id_defined()`/`BLOCK_DEFINED_BITS` reports which ids
have a file; undefined ids get the C++ defaults (`is_pickable=false`, all
faces `TEX_AIR`) so creative skips them. **Id 1249 is permanently reserved**:
it was the empty-slot sentinel in pre-2047 saves and the importer fails if
any file claims it; load paths map it to empty (`save_slot_id_is_empty`) /
`AIR` (`save_chunk_block_id`), and `NET_PROTOCOL_VERSION` was bumped for the
new id space. Removing a block frees its id for reuse — old saves holding a
removed id get the AIR/empty migration paths (the file name must equal the
lowercased `name`). Face keys in `"textures"`: `all`, `side`,
`top`, `bottom`, `left`, `right`, `front`, `back` (specific beats `side` beats
`all`, mapped to the `FACE_*` constants); `"icon"` names the non-isometric
inventory texture and implies `icon_is_isometric=false`. Remaining keys are
snake_case `Block` fields — unknown keys are a hard error; fields equal to the
defaults are omitted. `"aliases"` lists extra enum names for the same id.

Texture layers are independent `TextureID`s. `data/textures/<name>.png` holds
repo-authored 16x16 tiles; `data/textures.json` carries per-texture metadata:

- `source` — override image source. `textures/...` paths are relative to the
  Bedrock resource pack root (extensionless, `.png`/`.tga` auto-detected);
  plain names try `data/textures/`, `textures/blocks/*.png|tga`, then
  `textures/items/`.
- `frame` — index into a vertically stacked flipbook (default 0); the crop
  must be 16x16 after framing.
- `tint` — `"#rrggbb"` multiplied into RGB only (alpha preserved).
- `alpha` — `0..255` written into every nonzero-alpha pixel (transparent
  pixels stay 0). For Bedrock sources that are opaque where our tile was
  translucent (e.g. `water_still` vs `water`).
- `overlay` — `{source, tint?, frame?}`: a second tile alpha-composited over
  the processed base, e.g. `grass_side_carried` is Bedrock `dirt` plus the
  grayscale `grass_side` fringe tinted to the legacy green. The tint applies
  to the overlay only.
- `extract` — `{from: [x,y], size: [w,h], to: [x,y]}`: blit a rect of the
  resolved tile onto a fresh transparent tile. Torch top/bottom faces sample
  the window that tracks the cuboid's x/z position (cols/rows 7-8 for the
  2-wide stick), not the spot the sprite paints the cap or base — `torch_top`
  is Bedrock `torch_on` with the cap rect (rows 6-7) copied to the sampled
  window (rows 7-8), `torch_bottom` likewise with the stick end (rows
  13-14). `data/textures/redstone_torch_on.png` is the Java-edition tile on
  purpose: Java paints the lit jewel's visible 2-column core pink/red, while
  Bedrock's clipped core is plain yellow-white and reads as a normal torch.
  `redstone_torch_glow` (`keep`) is the solid fd0000 tile for the lit
  torch's inward-facing 3x3x3 glow shell (Java's `template_redstone_torch`),
  emitted in `chunk.cpp` with the `BLOCK_COORDS_INSIDE_OUT |
  BLOCK_COORDS_HALF_PIXEL` `face_shift` flags — INSIDE_OUT mirrors the unit
  cube so every quad faces inward (back-face culling leaves the far walls),
  HALF_PIXEL makes scale/offset/tex_offset 1/32-block units instead of
  1/16.
- `random_rotate` — tile participates in per-block random rotation.
- `keep` — pack even when no block references it (e.g. `player_selection`).

`make bedrock-samples` clones Mojang's bedrock-samples (pinned
`BEDROCK_SAMPLES_TAG`) into `bedrock-samples/` (git-ignored);
`make blocks-import` runs `tools/blocks_import.py` with
`BEDROCK_SAMPLES=bedrock-samples/resource_pack` and regenerates the committed
outputs (idempotent/deterministic — a dirty diff after rerun is a bug):

- `include/common/block_ids_gen.hpp` — `BlockID` enum, explicit values + aliases
- `include/common/texture_ids_gen.hpp` — `#define`-only `TEX_<NAME>` layer ids
  (safe for the shader preprocessor; compare as `uint(TEX_*)` in GLSL), plus
  `TEXTURE_COUNT`, `TEXTURE_ROTATABLE_COUNT`, `BLOCK_TEXTURE_ATLAS_ROWS`
- `src/common/block_definitions_gen.inc` — all 6 `textures[]` + `icon_texture`
  per block plus non-default fields
- `src/common/block_names_gen.inc` / `src/common/texture_names_gen.inc` —
  per-id name tables backing `block_definition_get_name` / `texture_name`
  (nullptr for undefined ids)
- `bitmaps/blocks.png` — 24-column atlas, layer `i` at col `i%24`, row `i/24`

Layer order is deterministic: `random_rotate` textures first (name-sorted),
then the rest name-sorted, so the vertex shader tests
`texture_id < uint(TEXTURE_ROTATABLE_COUNT)` — all layers are 0-based end to
end. Adding a block = add `data/blocks/foo.json` (+ tile under
`data/textures/` or a `source` in `textures.json`), `make blocks-import`,
rebuild. `make mobs-import` similarly regenerates `mob_models_gen.hpp` +
`mobs.png` from `tools/mobs_manifest.txt` (see Mobs). For scripting,
`BLOCKS_IMPORT_DATA_DIR` / `BLOCKS_IMPORT_OUT_DIR` redirect the importer's
input/output (used for testing against a copy of `data/`). `TEXTURE_COUNT`
must stay ≤ 2048 (GLES3 guarantees far fewer layers; the importer enforces
it — today it is ~1020).

New blocks get their property set by render class, copied from a template
block rather than hand-tuned: opaque cubes use the defaults; binary-cutout
tiles copy `GLASS`/leaves (see-through, `should_draw_*` off);
partial-alpha tiles copy a translucent template like `GLASS_LIGHT_BLUE`;
cross-quad plants copy the `Flowers` render template. Unrenderable shapes
(stairs, fences, walls, doors, signs, beds, rails, panes, trapdoors...) are
placeholder full cubes until a render class exists for them — see
`data/shapes.json` below for the workaround. Default
hardness is 1.5. Light emitters set `emits_light` and mirror the torch-style
flags (`no_light`, `casts_shadow:false`): sea_lantern/lantern/shroomlight/
lit_pumpkin/beacon/conduit/froglights/campfire/end_gateway/end_portal 15,
soul_* 10, magma 3, crying_obsidian 10, glowingobsidian 12, glow_lichen 7,
sculk_catalyst 6, amethyst cluster/buds 5/4/2/1, brewing_stand 1,
lit redstone ores 9, cave_vines_*_with_berries 14, firefly_bush 2.

`blocks_light_down` (derived, no JSON key): bottom-aligned partial blocks —
casts_shadow, opaque, scale.x/z=16, offset 0, scale.y<16, e.g. bottom slabs
and grass path. The post-pass clears `casts_shadow` (cell holds light,
lit from above/sides) while light.cpp never spreads light downward from it
and the skylight column fill stops at it — slab roofs shade but path tops
next to full blocks aren't dark. Floor cells also get a bit in
`Chunk::light_shade_mask` (sibling of `light_air_mask`): the light
texture's A channel is 0 for them, and chunk_fragment re-darkens the
ground layer a floor sits in — the old opaque-cell contact shadow —
while wall faces beside it stay undarkened.

The creative menu lists `CREATIVE_BLOCKS` from
`include/common/creative_order_gen.hpp`, computed by the importer. Pickable
blocks are deduped by visual signature (6 face textures + icon +
icon_is_isometric + scale/offset/tex_offset + render_order): each identical
group contributes one representative (prefer the block named after its
material, then the lowest shape rank, then a pre-import id from
`data/kept_block_ids.txt`, then lowest id); hidden duplicates stay real,
placeable blocks. `"creative": true|false` in a block JSON forces listing
on/off. The list is grouped by material — the name minus `STRIPPED_`/`WAXED_`
prefixes and one shape suffix, with dye-coloured blocks grouped into colour
families (WOOL, CONCRETE, STAINED_GLASS, ...) sorted in dye order.

A block JSON may set `"shape": "<name>"` (defined in `data/shapes.json`,
three kinds). **Mask shapes** (stairs, wall, banner,
hanging_sign, sign) are for silhouettes we can't render: the importer
replaces the 4 side faces with generated `<face_tex>__<shape>` tiles — the
resolved source tile with the mask's `.` pixels cut to transparent black —
and applies the shared `cutout` property set (see-through, no light
blocking, no shadow). Top/bottom keep the plain material texture.
**Model shapes** (fence, fence_gate) have real multi-box geometry: the
importer applies the `cutout` set plus the entry's own properties
(`is_fence`, `scale`, `offset`, ...) and keeps the block's plain textures;
the entry's `"icon_mask"` grid only cuts a generated `<side_tex>__<shape>`
tile used as the flat inventory icon (`icon_is_isometric=false`).
**Template shapes** (slab, button, pressure_plate, carpet, trapdoor) copy
geometry (`scale`/`offset`/`tex_offset`) plus behaviour properties
(`is_button`, `is_pressure_plate`, `collides_with_player`, ...) from a named
kept block via `"template"`; `"drop"` removes copied keys and other entry
keys override the template but not the block's own JSON. Textures always
stay the block's own.

Fences and fence gates draw several box instances per cell in the CPU
meshing pass (chunk.cpp phase 2, `fence_boxes`/`fence_gate_boxes` in
block.cpp): a 4x16x4 post plus 2px rails that connect to neighbors.
Wooden fences connect to any wooden fence, nether brick only to nether
brick, and every fence connects to gates whose span axis matches the
direction and to `is_solid` blocks; rails on both sides of an axis merge
into one 0..16 box, and rail end faces get `NO_LIGHT_NO_DRAW`. Gates rotate
on placement and store open/closed in rotation bit 2
(`BLOCK_ROTATE_GATE_OPEN`; facing is `rotation & 3`, the gate spans world x
when `(rotation & 1) == 0`); right-click toggles them (sneak + right-click
places instead), and open gates don't collide. `block_yaw` masks the open
bit for single-box consumers (raycast, selection, debris), so
gates never hit the `rotation >= 4` wall-torch branches. Collision is
multi-box (`fence_collision_boxes` via `block_collision_boxes` in
collision.cpp): the post plus a 4px bar spanning from the post face to
the cell edge on each connected side (both sides merge to 0..16), all
1.5 blocks tall like Minecraft — closed gates also collide at 1.5
blocks from the cell floor — so players and mobs can neither slip
between adjacent posts nor jump the fence. Because a box can reach above
its cell, the collision scan includes one layer below the player AABB.
Open gates still don't collide. Selection/raycast stay a single box —
the fence post or the gate's bounding box.

Doors are two-cell blocks: every lower-half id is linked to an upper-half
id by `door_partner` (validated by the importer). Both halves use the
`door_lower`/`door_upper` model shapes — a 16x16x3 slab pushed to the
placer-facing cell edge (offset z 13, matching MC's NORTH AABB) — collide,
render and raycast as single boxes, and carry the slab's current yaw in
rotation bits 0-1, so `block_yaw` masks bit 2 for them exactly like gates.
The flag bit means open on the lower and hinge-right on the upper:
`door_yaw(F, open, hinge)` swings the slab to `F + (hinge ? 3 : 1)` when
open, `door_rotations`/`door_facing` pack and recover the two halves, and
only the toggle logic ever re-reads both cells. Placement is atomic in
`PlayerBlockPlacedEvent` (a lower needs a solid floor below and a
placeable cell above; the upper is unplaceable on its own) and picks the
hinge so a second door on the placer's left becomes the pair's hinge-right
member — MC double doors. `block_can_survive_at` requires the partner half
plus the floor, so breaking either half pops the other through the normal
neighbour-update kill path; drops and picks map through `door_item_id`.
Right-click toggles non-`door_redstone_only` doors through `DoorEvent`
(TOGGLE), like gates; iron doors fall through to placement. Doors are
`is_redstone_mechanism`, so each half stores 0/1 power: a state update
whose combined lower|upper power changed queues `DoorEvent(SYNC_POWER)`
one tick out, giving MC edge semantics — rising opens, falling closes, and
a hand toggle in between persists.

`REPGAME_BLOCK_WORLD=1` ("block world") is a debug mode for eyeballing
every block: `make linux-run-blocks` runs it. Normal terrain, structures,
water, mobs, persistence and multiplayer are all off — nothing is read or
written under `~/.repgame` and no server is contacted. The ground is solid
SANDSTONE for y≤0 (solid, not a one-block floor, so the light cascade has
no void volume to sweep) with every defined non-AIR block placed
once at y=1 on a 2-spaced grid (32 per row, starting at x=2/z=2), ordered
`CREATIVE_BLOCKS` first then the remaining defined ids ascending — the
layout lives in `MapGen::block_world_id`. The z<0 half of the y=1 plane
holds a fixed fence showcase: an oak run with an inline gate, an L corner,
a T and a + junction, a lone post, a fence against planks, an oak fence
against nether brick (must not connect), and a nether brick run. The HUD shows the targeted
block's `NAME (id)` and face textures under the crosshair; spawn is
(0.5, y=2, 0.5) facing +x/+z in creative mode.

## Items

`ItemID` (`uint32_t`, `include/common/item.hpp`) is the inventory-slot id
space. Ids 0..2046 are block items (same value as the `BlockID`),
`LAST_BLOCK_ID` (2047) is the empty-slot sentinel, and pure (non-block)
items are `ITEM_ID_BASE` (2048) and up. `InventorySlot` stays a packed
`{ ItemID, quantity }` pair; only block items can be placed or
middle-click-picked.

Pure items are data-driven, same rules as blocks: one JSON per item in
`data/items/<lowercase>.json` with keys `id` (>= 2048, never renumber),
`name` (the uppercase enum name), `icon` (a texture name resolved exactly
like block textures, including the `textures/items/` fallback and
`textures.json` metadata), optional `max_stack` (default 64) and optional
`aliases`. Unknown keys are a hard error. `make blocks-import` validates
them and emits `include/common/item_ids_gen.hpp` (enum + `item_id_defined`),
`src/common/item_definitions_gen.inc`, `src/common/item_names_gen.inc`,
packs the icons into `bitmaps/blocks.png`, and appends the items to
`include/common/creative_order_gen.hpp` (`CREATIVE_BLOCKS` holds ItemIDs).

Core API in `item.hpp`/`src/common/item.cpp`: `item_is_block`,
`item_id_valid`, `item_icon_texture`, `item_icon_is_isometric` (pure items
are flat), `item_max_stack`, `item_name`. Block items forward to the
`Block` definition.

Blocks declare drops with an optional `"drops"` JSON key:
`[{"item": "<block or item name>", "min": 1, "max": 1, "chance": 1.0}]`.
Absent = drop itself (door_item_id mapping plus the pickable /
non-transparent / non-water gate in `RepGame::break_selected_block`);
`[]` = drops nothing (glass, panes). Within one list, entries with
`chance < 1` are mutually exclusive — one roll picks the first entry whose
cumulative chance covers it (their chances must sum to <= 1.0, enforced by
the importer); `chance == 1` entries always drop. The importer emits
`src/common/block_drops_gen.inc`; `block_roll_drops(BlockID, uint32_t *rng_state,
ItemStack *out, int max_out)` rolls it deterministically (mutating state —
reseed for reproducible tests). Examples in tree: stone→cobblestone,
grass→dirt, ores→their item (lapis 4-8), leaves→sapling 5% + stick 2%
(+apple 0.5% oak/dark oak), gravel→flint 10% else gravel.

Mob drops live in `MobTypeDef::drops` (`MobDrop[2]` of
`{ ItemID item; int min; int max; }`, `LAST_BLOCK_ID` = unused slot).

## Dropped items

`ItemEntities` (`include/common/item_entities.hpp`), owned by `World` as
`world.items`, is the client-local dropped-item simulation — modelled on
BlockBodies (headless tick, render chains wired only after `init()`,
`render_inited` pattern, `update_render(alpha, chunk_loader)`, `draw` in the
main and reflection passes). Each entity is a 0.25 cube (box bottom-center
`pos`, `prev_pos` for interpolation): MC's item gravity/drag rescaled from
20 TPS to 144 UPS (gravity 0.00077/tick² ≈ 16 blocks/s², air drag 0.9972 —
gentler than the player's `GRAVITY_STRENGTH`), ground friction 0.6, no
bounce. Break/mob drops pop with `item_drop_hop` (peak ~0.2–0.35 blocks,
<0.3 sideways) so they land in the broken cell. In water
buoyancy +0.0015/tick with 0.9 drag and a 0.03 upward cap floats items to
the surface. Integration (and aging) freezes in capture mode and while the
item's chunk isn't `CHUNK_LIVE`. Same-item entities within 0.5 blocks merge
into the older one every 10 ticks, capped at `item_max_stack`. Despawn at 5
minutes (`ITEM_DESPAWN_TICKS`). `MAX_ITEM_ENTITIES` = 512; spawning at the
cap evicts the oldest. `impulse(center, radius, strength)` is called
wherever `bodies.impulse` is — items are light, so the shove is tripled.

Sources: survival block-break rolls `block_roll_drops` and spawns each
result at the cell center with a small random hop (creative keeps the block
instead); survival mob kills spawn the type's drops at the mob's center.
`Q` throws one of the selected hotbar item, `Ctrl+Q` the whole stack
(from eye + 0.3·look, velocity look·0.045 + up 0.01, ~3 blocks on flat ground). While the inventory is open
and a stack rides the cursor, a left click outside every visible panel
throws the whole stack, a right click throws one — "outside" is decided by
`InventoryRenderer::containsPoint` over the chest (if open), survival or
creative, and hotbar panels. Closing the inventory still returns the held
stack to the inventory first.

Pickup runs inside the item tick: items with `pickup_delay == 0` within
2.0 blocks of the player magnet toward the chest, and overlapping the feet
AABB (expanded 1.0 xz / 0.5 y) is collected via
`RepGame::give_item_to_player` — which returns the count that didn't fit,
leaving the remainder on the ground. A `pop` sfx plays on pickup.

Rendering: isometric block items draw as a 0.25 cube through the
falling-block path (`ParticlePosition`, `object_shader`); everything else
(pure items, sprite-icon blocks) draws as an extruded sprite — one shared
66-quad mesh (front/back + per-texel-column/row edge slices, 1/16 deep) via
`item_sprite_vertex.glsl`, instanced with `{uint layer, mat4 transform,
vec2 light}`. The fragment's `u_ExactTexel` path in `object_fragment.glsl`
texelFetches at lod 0 so edge slices alpha-test per texel without mipmap
bleed. Sprites scale 0.35, stand upright, spin ~2 rad/s and bob while
grounded/in water; both paths are lit by `ChunkLoader::light_sample`. The
debug overlay shows `Items: <n>`.

- `REPGAME_ITEM_TEST="name,x,y,z[,count][;...]"` — exact deterministic
  spawns at init (block or item name, case-insensitive), zero velocity and
  `INT_MAX/2` pickup delay so they stay put for captures. ITEM_TEST spawns
  are non-persistent (never written to an items file) and get `spin_seed` 0
  so sprites face ±z — capture along the z axis sees the icon face-on.

Persistence mirrors the mob sidecar end to end: `items_x_y_z` beside
`mobs_x_y_z` (`u32 record_size`, `u32 count`, packed `ItemFileRecord
{item, count, pos[3], vel[3], age_ticks}` — readers copy the shared prefix
and skip records failing `item_id_valid`/`count <= 0`). `load_terrain`
reads the file into `Chunk::pending_items`, `World::render` hands
just-finalized chunks to `ItemEntities::adopt_chunk`, the ~16-tick
`poll_owned` persists+removes items whose owning chunk left the window,
and `World::cleanup` persists every owned chunk. A chunk whose items were
all picked up rewrites its file empty so stale records can't resurrect.
Pickup delay is not persisted (loaded items are pickable immediately).
No reads or writes in capture mode, fly mode, block world, or
`REPGAME_MOBS=0`/`REPGAME_MOB_TEST` runs — same gates as mobs. Multiplayer
is identical to single-player: items are client-local and persist to the
local world dir exactly like mobs.

## Crafting

Data-driven recipes: one JSON per recipe in `data/recipes/*.json` plus
shared tags in `data/item_tags.json` (`{"tag": ["NAME", ...]}` — referenced
as `"#tag"` in ingredients). `make blocks-import` validates them (unknown
keys, unknown names/tags, malformed patterns and out-of-range counts are
hard errors) and emits `src/common/recipes_gen.inc` — option arrays plus a
`CraftRecipe` table in sorted filename order, so output is deterministic.

Recipe format — exactly one of `pattern` (shaped) or `ingredients`
(shapeless):

```
shaped:    {"pattern": ["#", "#"], "key": {"#": <ing>}, "result": "STICK", "count": 4}
shapeless: {"ingredients": [<ing>, ...], "result": "OAK_PLANKS", "count": 4}
<ing> := "NAME" | "#tag" | ["NAME" | "#tag", ...]
```

A shaped pattern is 1–3 equal-length rows (≤3 chars); space is an empty
cell, every pattern char must be in `key` and every key must be used.
`count` defaults to 1 and is capped by `item_max_stack(result)`.

`include/common/crafting.hpp` / `src/common/crafting.cpp` hold the pure
matcher (no GL): shaped recipes compare the bounding box of the non-empty
grid cells to the recipe's w×h and also try the horizontally mirrored
layout; shapeless assignment backtracks so overlapping option lists work.
`crafting_find_recipe(grid, dim)` returns the first match in table order.
`test_crafting` in rep_tests.cpp covers it.

UI: the grid slots live in `RepGameState::crafting` (renderer order —
slot 0 is the BOTTOM-left cell; they get flipped to top-first before the
matcher). A 2x2 grid shows in the survival inventory; right-clicking a
CRAFTING_TABLE opens a 3x3 in either game mode (closing on the same
gone/out-of-reach checks as a chest). Grid contents are temporary:
`crafting_return_grid` (idempotent, called from the every-tick
inventory-closed branch, the creative-mode toggle, and `cleanup()` before
`save_player_data()`) moves them survival → hotbar → thrown item entity.
Layout uses fixed-stride `InventoryRenderer` panels sharing the survival
panel's cell stride, with the group (grid + gap + 1x1 output) centred and
the grid's bottom pinned a quarter stride above the survival panel's items.
While the table is open, shift-clicking a survival or hotbar slot moves the
stack (half for right-shift-click) into the 3x3 grid via `crafting_grid_add`
(merge matching stacks, then empty cells, top-left first); without a table
the usual survival↔hotbar shift-move applies.

## Mobs

Client-local mobs — passive pig, cow, chicken and sheep plus hostile
zombie and creeper — and network-driven remote players (`MOB_PLAYER`),
driven by a type table and models imported from Bedrock
`geo.json`. Simulation (gravity, collision,
auto-jump, buoyancy, idle/wander/panic AI, hostile chase/fuse AI, hit
flash, death tip-over) runs
in `MobManager::tick` (`src/common/mobs.cpp`); rendering rebuilds one
instanced `RenderChain<ParticleVertex, MobPartInstance>` per frame from
CPU-evaluated bone transforms (`mob_vertex.glsl` + `object_fragment`;
negative `v_flash` is a red hurt tint, positive is the creeper's white
fuse flash). Mobs are drawn in both the main
and reflection passes and share the chunk light sampling.

Hostiles target the player only while targetable
(`MobTickInput.player_targetable` = survival + not no-clip; in-game that
means `GameMode_Survival && !no_clip` — `MobTickInput`/`MobTickOutput`
carry `player_hits` and `explosions` per tick). They acquire within 16
blocks, drop past 24, and chase at `chase_speed` (10°/tick max turn);
untargeted hostiles wander like passives but don't panic when hit.
Zombies melee in reach (mob half-width + 0.3 + 0.5, |dy| < 1.5) with a
1 s cooldown → a `MobPlayerHit`, handled by `RepGame::player_hurt`
(knockback away from the hit + a hop off the ground + the `player_hurt`
sound pool; there is no player health system yet, so `damage` is
otherwise unused). In daylight (`light_daylight_factor > 0.5`, head sky
light ≥ 0.95, not in water — see `mob_burns_in_daylight`) zombies burn at
1 hp/s with hurt flash/sound and no knockback. Creepers chase like
zombies, stop within 3 blocks, hiss for ~1.5 s (the `fuse` sfx +
`MOB_ANIM_SWELL` growing/white-flash render), and defuse if the player
gets > 7 away; at fuse end they push their position to
`out->explosions` and are removed — `RepGame::tick` turns each entry
into a `TntExplodeEvent` with a null `primed_entity` (null is a safe
no-op in `World::despawn_primed_tnt`/`PrimedTnt::despawn`). A
`fall_cap` per type limits downward speed (chickens glide at 0.02).

The night spawner runs inside `MobManager::tick` (off under
`REPGAME_MOB_TEST`, capture mode, fly mode and `REPGAME_MOB_SPAWNER=0`):
every 2 s, while fewer than 12 despawnable hostiles live, up to 4
attempts pick a random ring 24–44 blocks out, scan down from
player_y + 16 (40 cells) for a colliding top block with 2 air cells
above inside a `CHUNK_LIVE` chunk, and spawn where
`hostile_spawn_light_ok` (`max(sky * daylight, block) < 7/15`) holds —
60% zombie / 40% creeper, `persistent=0` + `despawnable=1`.
Despawnables are removed > 64 blocks out, randomly at
1/(8·UPS_RATE)/tick past 40, or when their feet chunk leaves
`CHUNK_LIVE`, and are never persisted.

- `REPGAME_MOBS=0` — disables local mobs (spawning, simulation,
  persistence) but NOT remote players: `spawn_remote` and remote
  rendering still work.
- `REPGAME_MOB_SPAWNER=0` — disables only the night spawner.
- `REPGAME_MOB_TEST="type,x,y,z,yaw[,pitch[,sneak]][;...]"` — exact
  deterministic spawns (non-persistent; also disables natural spawn and
  mobs-file loading for the run). `player` entries spawn a remote
  `MOB_PLAYER` at feet position through `set_remote_pose` (the optional
  pitch is the look pitch; the optional sneak is 0..1 and drops the eye
  height `sneak * PLAYER_SNEAK_EYE_DROP`).
- Capture mode (`REPGAME_CAPTURE`) freezes mob AI and physics — spawn
  poses are as authored, no falls or wandering mid-capture.
- `n` — debug spawn key: drops a mob three blocks in front of the
  player, cycling pig → cow → chicken → sheep → zombie → creeper on
  successive presses (`MobManager::spawn_in_front`; edge-triggered
  through `Input`, handled in `RepGame::tick`, persistent). The ImGui
  overlay shows `Mobs: <n> (players <m>)` — remote players count
  separately.

Remote players: `MultiplayerAvatars` is a thin adapter over `MobManager`
(`player_id → uid` map) — `add`/`update_position`/`remove` translate to
`spawn_remote`/`set_remote_pose`/`remove_remote` on a `MOB_PLAYER` mob.
The packet position is the sender's eye position plus its eye height
(feet = eye − eye_height; eye height drops to `PLAYER_SNEAK_EYE_HEIGHT`
while sneaking, driving the `MOB_ANIM_HUMANOID_SNEAK` crouch pose).
The camera rotation matrix is inverted by
`mob_look_from_camera_rotation` (`mobs.hpp`). `MOB_AI_REMOTE` mobs skip
physics/AI, ease toward the latest network pose, walk-animate from the
horizontal delta (`MOB_ANIM_HUMANOID_WALK`), never persist, never
raycast, and keep ticking in capture mode.

Adding a mob:

1. Add a line to `tools/mobs_manifest.txt`:
   `name|geo_file|geometry_id|skin1,skin2,...[|bone=rx,ry,rz;...]` —
   `geo_file`/`skin` paths are under `BEDROCK_SAMPLES`
   (`bedrock-samples/resource_pack` in the repo by default — see
   "Blocks & textures"), the
   trailing field overrides rest-pose bone rotations that Bedrock keeps in
   animation files (the pig's `body=-90,0,0` comes from
   `animation.pig.setup.v1.0`; with `+90` the body cube swings up and back
   instead of lying flat — keep the `-90` sign).
2. `make mobs-import` runs `tools/mob_import.py` →
   `include/common/mob_models_gen.hpp` + `bitmaps/mobs.png`. Output is
   deterministic (byte-identical reruns); the importer handles geo.json
   1.8 (`"geometry.a:geometry.b"` inheritance) and 1.12, box UVs only
   (per-face UVs are rejected), bone mirror/pivot/rotation, and pads each
   skin into one 64x64 layer of the atlas.
3. Add a `MobTypeDef` row in `src/common/mob_types.cpp` (+ `MOB_*` enum in
   `mob_types.hpp`): model/skin bindings, hitbox, health,
   walk/panic/chase speeds, `fall_cap`, `attack_damage`, drops, sound
   pool names, natural-spawn data, AI profile (`MOB_AI_PASSIVE`,
   `MOB_AI_HOSTILE_MELEE`, `MOB_AI_HOSTILE_CREEPER`, `MOB_AI_REMOTE`),
   animation mask (`MOB_ANIM_*` — quadruped/humanoid walk, head look,
   death tip, sneak, wing flap, creeper swell) and the head/leg/arm/
   body/wing bone names the animators bind to (NULL = unused).
4. Optional sounds: append `sfx` entries to `tools/sounds_manifest.txt`
   (base + `_N` variants pool, e.g. `pig_say_1..3`; `pig_death`,
   `pig_step_1..5`).

Skin atlas convention: each skin is one 64x64 layer (`MOB_SKIN_*` is the
layer index into `mobs.png`, `MK_TEXTURE(mobs, 64, 64*MOB_SKIN_COUNT,
64, 64)`, NEAREST + no mipmaps via `Texture::init(..., mipmaps=false)`).
The loader maps skin pixel `(px, py)` to UV `(1 - px/64, 1 - py/64)` —
vertical flip from the texture load plus a per-tile X mirror; mirrored
cubes also swap left/right face rects (`mob_face_rect` in
`include/common/mob_uv.hpp`, also unit-tested in `test_mobs`).

Persistence: each chunk's mobs live in `mobs_x_y_z` beside the
`chunk_x_y_z`/`chest_x_y_z` files — `u32 record_size`, `u32 count`, then
packed `MobFileRecord`s (readers copy the shared prefix and skip
`type >= MOB_TYPE_COUNT`). An existing-but-empty file is a *populated
marker*: a chunk whose herd was killed or removed never naturally
respawns. The terrain worker loads the file (or rolls the `spawn_chance`
≈1/12 natural spawn on fresh terrain, seeded `hash(chunk_pos, type,
MAP_SEED)`) into `Chunk::pending_mobs`; `World::render` hands
just-finalized chunks to `MobManager::adopt_chunk`, and a ~16-tick poll
persists+removes mobs whose owning chunk leaves the active window
(parked/reused — `CHUNK_LOADING` at the same position has not left).
`World::cleanup` persists every owned chunk. `despawnable` (night-spawn)
mobs are never written. No writes in capture or fly
mode. Natural spawn stands herds on `spawn_on` column tops with hitbox
headroom inside the chunk.

Hitting: left click raycasts mobs (`MobManager::raycast`, AABB slab test)
against `block_selection.ray_origin/ray_dir`; a mob nearer than the
selected block takes `HIT_DAMAGE` (4.0, creative kills in one hit), gets
knockback + a 0.3 s red flash + 5 s panic, and drops `drop_block` in
survival when killed.

## Block bodies (falling blocks + primed TNT)

`BlockBodies` (`include/common/block_bodies.hpp`, `src/common/block_bodies.cpp`,
owned by `World` as `world.bodies`) is the shared CPU simulation for
axis-aligned block-sized bodies (0.98 box, `MAX_BLOCK_BODIES` = 1024, stable
nonzero ids). It integrates two kinds: `BODY_FALLING` (any block with
`falls_with_gravity` — sand, gravel, concrete powder, anvils) and
`BODY_PRIMED_TNT` (ignited TNT, which replaced the old GPU-ballistic
`PrimedTnt`). Physics runs at `UPS_RATE` inside `bodies.tick` (called right
after `mobs.tick`): gravity/drag/terminal velocity (`GRAVITY_STRENGTH`,
`TERMINAL_VELOCITY`, air drag ~0.9972), water medium (`WATER_GRAVITY_STRENGTH`,
`SWIM_TERMINAL_VELOCITY`, ~0.97 drag), ground friction 0.952, wall restitution
0.2, per-cell broadphase body-vs-body collision with inelastic exchange, and
voxel collision via `Collision::move_box` (same axis resolution/substepping as
the player path, Y→X→Z, no step-up, no edge guard; unloaded cells are solid).
Bodies freeze in capture mode and while their containing chunk is not
`CHUNK_LIVE` — a primed TNT frozen in a parked chunk still explodes at its
frozen position when the fuse event lands.

A `falls_with_gravity` block releases as a body when a neighbor update leaves
it over a loaded non-colliding cell — the trigger sits in `perform_checks`
(`BlockNextToChangeEvent.cpp`), queuing `FallingBlockEvent` two ticks out so a
quick support restore wins; the event re-checks live state and releases the
whole contiguous run of falling blocks above. A grounded, slow, grid-aligned
body settles after a few ticks and `settle_land` writes it back
(`set_loaded_block` + `multiplayer.set_block` + neighbor updates): air/water/
replaceable cells take the block (a replaced plant breaks into debris), a
non-colliding cell (torch) breaks the falling block into debris, a cell that
went solid meanwhile (another body landed beneath) stacks on top, and a rest
cell over unloaded space drops as debris. Bodies blocked by the player keep
waiting. `land_all` runs before `world.cleanup` so live bodies persist into
the save.

Primed TNT keeps its fuse as an event: `tnt_ignite`/`tnt_launch` (in
`TntExplodeEvent.cpp`) remove the block, spawn a `BODY_PRIMED_TNT` with a
small hop, and queue `TntExplodeEvent(tick + fuse, fallback_pos, body_id,
inverse)` — body id 0 is the non-body path (creepers). When the blast runs it
`take()`s the body so it explodes at the real position, and shoves every live
body (`bodies.impulse`, radius R+2 — still-fusing primed TNT and falling
blocks already in flight). Gravity blocks sitting in the world are destroyed
like everything else; a gravity block that loses support outside the sphere
still falls normally through `FallingBlockEvent`. TNT spawning at the body cap evicts the oldest falling body; a falling
spawn at the cap leaves the block in place.

Rendering is client-side instances rebuilt per frame in `update_render`
(interpolating `prev_min`→`min` by alpha, per-body light sampling): falling
blocks draw through `render_falling` + `object_shader`
(`ParticlePosition`), primed TNT through `render_tnt` +
`primed_tnt_shader` (`PrimedTntInstance`: transform + light + CPU fuse-blink
`flash`, same shader file `primed_tnt_vertex.glsl` reworked to consume the
instance transform). Chains are only touched after `init()`/`render_inited`,
so headless tests simulate fine. Landed bodies keep an instance at their rest
cell until the target chunk's `needs_repopulation` drains (the remesh safety
net). Remote multiplayer block updates deliberately don't run these events —
only the client that caused a change simulates bodies.
