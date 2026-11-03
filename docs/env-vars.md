# Environment variables

Every `REPGAME_*` variable read in `src/`/`include/` (plus the
WASM URL-param seam). Reads go through the typed helpers in
`include/common/utils/env_config.hpp` (`env_is_set`, `env_bool`,
`env_int`, `env_long`, `env_float`, `env_double`, `env_str`) — don't add
new `getenv` calls. The capture/fly/walktest harness variables are parsed
once at startup into `RunPolicy` (`include/common/harness/run_policy.hpp`,
`src/common/harness/run_policy.cpp`); production code reads the policy,
not the environment. On WASM every `REPGAME_*` URL query parameter becomes
an environment variable (`src/wasm/index.js` `preRun` copies them into
Emscripten's `ENV`), so all of these work as `?REPGAME_FOO=...` there.
"Platforms" lists where the variable is actually read; unset vars elsewhere
are silently ignored. Deeper prose lives in the linked doc — this table is
the index, not the reference.

| Variable | Default | Platforms | Effect | Details |
|---|---|---|---|---|
| `REPGAME_AUDIO` | on (when built with audio) | all | `0` disables all audio; also a build-time switch (`REPGAME_AUDIO=0` compiles the stub; default 0 when `sounds/` is absent) | docs/audio.md |
| `REPGAME_BLOCK_WORLD` | off | all | `1` runs the block world debug mode (no terrain/mobs/persistence/network) | docs/blocks.md |
| `REPGAME_CAPTURE` | unset | all | `"x,y,z,yaw,pitch[,flags];..."` — presence enables capture mode | capture skill |
| `REPGAME_CAPTURE_ANOMALY` | off | all | `1` scans each display frame for isolated bright outliers; `2` also scans scene alpha (heavy) | capture skill |
| `REPGAME_CAPTURE_PERIODIC` | off | all | `n`: dump all FBO attachments + display every n frames | capture skill |
| `REPGAME_CAPTURE_QUALITY` | 1 (MEDIUM) | all | draw quality `0..3` during captures | capture skill |
| `REPGAME_CAPTURE_SETTLE` | 60 | all | consecutive idle ticks required before shooting a view | capture skill |
| `REPGAME_CAPTURE_TIME` | noon (`DAY_LENGTH_TICKS/4`) | all | world_time in ticks during captures | capture skill |
| `REPGAME_CAPTURE_TIMEOUT` | `30 * UPS_RATE` | all | max ticks per view before shooting anyway | capture skill |
| `REPGAME_CAPTURE_VIEW_TICKS` | 0 (idle-settle) | all | `n>0`: views after the first shoot exactly n ticks after the camera jump | capture skill |
| `REPGAME_DISPLAY` | primary | desktop (SDL3) | 0-based index into the SDL display list; invalid values warn and fall back to primary | — |
| `REPGAME_FACE_GROW_PX` | 0.125 (`CHUNK_FACE_GROW_PX`) | all | screen-space face grow in pixels | docs/rendering.md |
| `REPGAME_FLY` | unset | all | `"x,y,z,yaw,pitch[,speed]"` — scripted no-clip flight; presence enables fly mode | docs/rendering.md |
| `REPGAME_FLY_FLY_TICKS` | -1 (until `FLY_TICKS`) | all | flight duration in ticks, then the camera holds its final position | docs/rendering.md |
| `REPGAME_FLY_METRICS` | 1 | all | FLYMET telemetry cadence in ticks | docs/rendering.md |
| `REPGAME_FLY_RD` | unset | all | `"tick:radius,tick:radius"` render-distance schedule during flight | docs/rendering.md |
| `REPGAME_FLY_SETTLE` | 0 (fly immediately) | all | consecutive idle loader/light ticks required before the flight begins | docs/rendering.md |
| `REPGAME_FLY_SETTLE_TIMEOUT` | `30 * UPS_RATE` | all | give up waiting for settle after this many ticks | docs/rendering.md |
| `REPGAME_FLY_TICKS` | 0 (run forever) | all | exit at fly tick n via the normal cleanup path | docs/rendering.md |
| `REPGAME_FOG_RANGE` | 96 | all | width of the distance-fog band in blocks (capped at 38% of render distance) | docs/rendering.md |
| `REPGAME_ITEM_TEST` | unset | all | `"name,x,y,z[,count][;...]"` deterministic non-persistent item spawns at init | docs/items-crafting.md |
| `REPGAME_LIGHT_CLAIM_RADIUS` | 1 | all | `1..4`: Chebyshev claim-conflict radius between light-job center columns | docs/rendering.md |
| `REPGAME_LIGHT_MIPMAPS` | 1 | all | `0`/`1`: mipmapped light volume vs flat RG8 volume | docs/rendering.md |
| `REPGAME_LIGHT_SMOOTH` | unset | all | `0`/`1`: force the Shadows quality mode (overrides the pause-menu setting) | docs/lighting.md |
| `REPGAME_LIGHT_WORKERS` | 4 | all | `1..8`: light worker pool size | docs/rendering.md |
| `REPGAME_MIN_AMBIENT` | 0.05 | all | absolute light floor 0..1 (caves not pure black); overrides the `player.dat` value set by the pause-menu Cave Brightness row | docs/rendering.md |
| `REPGAME_MOBS` | on | all | `0` disables local mobs (spawn/sim/persistence), not remote players | docs/mobs.md |
| `REPGAME_MOB_SPAWNER` | on | all | `0` disables only the night spawner | docs/mobs.md |
| `REPGAME_MOB_TEST` | unset | all | `"type,x,y,z,yaw[,pitch[,sneak]][;...]"` deterministic mob spawns; disables natural spawn + mob-file loading | docs/mobs.md |
| `REPGAME_MP_CTL` | unset | all | append-only command file drained each tick (mp-test seam) | mp-test skill |
| `REPGAME_MP_DUMP` | unset | all | JSONL dump path; presence enables the mp-test client harness | mp-test skill |
| `REPGAME_MP_DUMP_EVERY` | 5 | all | state-dump cadence in ticks | mp-test skill |
| `REPGAME_MSAA` | on (4x) | WASM | `0` requests a non-antialiased WebGL context | capture skill |
| `REPGAME_MUSIC` | on | all | `0` mutes music only (SFX still play) | docs/audio.md |
| `REPGAME_MUSIC_GAP` | 240 | all | max seconds between tracks; gap uniform in `[gap/4, gap]`, `0` = continuous | docs/audio.md |
| `REPGAME_MUSIC_VOLUME` | 0.5 | all | linear music gain 0..1 | docs/audio.md |
| `REPGAME_NIGHT_AMBIENT` | 0.20 | all | moonlight floor on the daylight factor 0..1 | docs/rendering.md |
| `REPGAME_NO_HIP` | unset | desktop (HIP builds) | forces the CPU path for all HIP GPU work | docs/rendering.md |
| `REPGAME_NO_HIP_MAPGEN` | unset | desktop (HIP builds) | forces CPU terrain mapgen only | docs/mapgen.md |
| `REPGAME_NO_HIP_MESH` | unset | desktop (HIP builds) | forces CPU meshing only | docs/rendering.md |
| `REPGAME_NO_WEBGPU_MAPGEN` | unset | WASM | disables the WebGPU mapgen half of the backend | docs/mapgen.md |
| `REPGAME_NO_WEBGPU_MESH` | unset | WASM | disables the WebGPU mesh half of the backend | docs/rendering.md |
| `REPGAME_OCCLUSION` | 1 | all | `0` disables the hardware occlusion-culling probe pass | docs/rendering.md |
| `REPGAME_PORT` | 25566 | all | server port override (read in `RepGame::init` when a hostname is given) | mp-test skill |
| `REPGAME_RENDER_DISTANCE` | `RENDER_RADIUS_DEFAULT` | all | initial horizontal render distance in chunks (`1..RENDER_RADIUS_MAX`); overrides the `player.dat` value | docs/rendering.md |
| `REPGAME_SCREENSHOT_TICK` | 0 (off) | all | request a framebuffer screenshot on tick n (`./screenshots/`); only consulted under `REPGAME_WALKTEST` | capture skill |
| `REPGAME_SFX_VOLUME` | 1.0 | all | linear SFX gain 0..1 | docs/audio.md |
| `REPGAME_TELEPORT` | unset | all | `"x,y,z,angle,sprint,spin_deg_per_tick[,pitch]"` — teleport on tick 1 then hold forward; only consulted under `REPGAME_WALKTEST` | capture skill |
| `REPGAME_TEX_MIPMAPS` | 1 | all | `0` restores NEAREST minification of the block atlas | docs/rendering.md |
| `REPGAME_VALIDATE_INSTANCES` | off | all | when set, sanity-check every BlockCoords instance before GPU upload | docs/rendering.md |
| `REPGAME_VR` | on (OpenXR builds) | OpenXR | `0` disables VR entirely | docs/vr.md |
| `REPGAME_VR_MIRROR` | 1 desktop / 0 Steam Frame | OpenXR | blit eye 0 to the desktop window | docs/vr.md |
| `REPGAME_VR_QUALITY` | 0 (LOW) | OpenXR | world draw quality in VR `0..3` | docs/vr.md |
| `REPGAME_VR_RENDER_SCALE` | 1.0 | OpenXR | per-eye resolution scale (clamped to runtime max) | docs/vr.md |
| `REPGAME_VR_SMOOTH_TURN` | 0 (snap) | OpenXR | smooth turn rate in deg/sec | docs/vr.md |
| `REPGAME_VR_SNAP_DEGREES` | 30 | OpenXR | snap-turn angle | docs/vr.md |
| `REPGAME_VR_UI_DIST` | 1.0 | OpenXR | UI quad distance in meters | docs/vr.md |
| `REPGAME_VR_UI_WIDTH` | 1.0 | OpenXR | UI quad width in meters | docs/vr.md |
| `REPGAME_WALKTEST` | unset | all | when set: scripted forward walk each tick + `WT ...` movement logging to stderr | capture skill |
| `REPGAME_WALKTEST_TICKS` | 0 (run forever) | all | quit after n ticks through the normal exit path (walktest only) | capture skill |
| `REPGAME_WEBGPU` | 0 | WASM | `1` opts into the WebGPU terrain-gen/mesh backend | docs/rendering.md |
| `REPGAME_WINDOW_MSAA` | on (`MSAA_SAMPLES`) | desktop (SDL3) | `0` requests a non-multisampled window | docs/rendering.md |

Non-`REPGAME_*` env seams used by tests: `SDL_VIDEODRIVER=offscreen` +
`SDL_AUDIODRIVER=dummy` for headless runs (the dummy audio driver keeps test
runs from playing music/sfx).

Compile-time tunables that are NOT env vars (formerly configurable, now
`#define`s at the top of `src/shaders/chunk_fragment.glsl`):
`LIGHT_LOD_BIAS` / `LIGHT_FAR_AO` and `CUTOUT_FADE_START` /
`CUTOUT_FADE_END` — see docs/rendering.md.
