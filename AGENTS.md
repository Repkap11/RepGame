# RepGame

Voxel game (C++17, SDL3, OpenGL). Build artifacts go to `out/`; the Linux
binary lands at `out/linux/release/RepGame`, the server at
`out/server/RepGameServer`.

## Build / run

C++ compilation is CMake — top-level `CMakeLists.txt`, helpers in `cmake/`,
presets in `CMakePresets.json`. The make targets below are thin wrappers
(each re-runs `cmake --preset` + `cmake --build`).

- `make linux` / `make linux_debug` — release / debug build for Linux
  (`make linux PERF=1` keeps frame pointers for `perf record --call-graph fp`)
- `make linux_asan` / `make linux_tsan` — ASan+UBSan / TSan client builds
  (`out/linux/asan|tsan/RepGame`); used via `rep-dbg --build asan|tsan`
  (docs/debug-tools.md)
- `make linux-run` — build and launch fullscreen, connecting to
  `www.repkap11.com` (`linux-run2` uses the second world)
- `make linux_debug-run` — debug build, single-player on `localhost`
  (`make dev` is an alias)
- `make linux-run-blocks` — launch the block world (`REPGAME_BLOCK_WORLD=1`,
  see `docs/blocks.md`)
- `make linux-vr` / `make linux-vr-run` — OpenXR client build / run against
  a locally built Monado simulated HMD (`docs/vr.md`)
- `make server` / `make server-run` — build / run the dedicated server
- Run: `./out/linux/release/RepGame <WorldName> <host>`
  (e.g. `RepGame World1 localhost`); server: `RepGameServer [port|--selftest]`
- Other platforms (ARM, Steam Frame, WASM, Windows, Android, Docker,
  AppImage, flatpak) and all deploy targets: `docs/platforms.md`.

`REPGAME_PCH` (CMake option, default ON) precompiles the vendored headers —
glm/entt/imgui/SDL3 for the client (SDL2 on wasm), glm/entt for the server.
Vendored headers only, never project headers: a PCH edit rebuilds the
serialized pch before every TU. `-DREPGAME_PCH=OFF` on the cmake command
line disables it for troubleshooting.

## Testing

- `make tests` — debug client unit tests (`RepGame --tests`,
  `src/common/tests/`); `make tests TESTS="net_ mobs"` runs only tests whose
  name contains a filter
- `make server-test` — server self-test (`RepGameServer --selftest`)
- `make test` — tests + server-test (`test-all` adds mp-test)
- `make mp-test` — end-to-end multiplayer scenarios; see the `mp-test` skill
- `make scenarios` — scripted game scenarios (`tests/scenarios/*.rds` via
  `rep-dbg script`; `SCENARIOS="substr"` filters); part of `test-all`
- `make check` — cppcheck over `src/`; verified false positives live in
  `tools/cppcheck-suppressions.txt` (vendor dirs use `*:path/*` entries).
  Caches per-file results under `out/cppcheck` — reruns are seconds;
  delete that dir to force a full recheck (~5-6 min cold). Expensive: a
  cold run is ~6 min and editing a widely-included header
  (constants.hpp etc.) invalidates most of the cache — run only when
  needed, not as a routine build/test verification step
- `tools/rep-dbg` — structured headless runs (`fly`/`walk`/`run` presets)
  with per-run `result.json`/`results.jsonl` under `~/.repgame/rep-dbg`;
  `rep-dbg inspect <world|run> <sub>` reads saved worlds offline
  (block/slice/chunk/entities/player); `rep-dbg gc` reclaims old
  runs/perf.data/worktrees;
  `python3 -m unittest discover tools/rep_dbg/tests` covers the Python
  side. See docs/debug-tools.md and the `debug-tools` skill
- Scratch files from ad-hoc debugging (screenshots, `.rds` scripts,
  patches, logs, temp homes) go under `/tmp/repgame/` — never loose in
  `/tmp` or the repo; tmpfs wipes it on reboot. Persistent rep-dbg run
  output lives under `~/.repgame/rep-dbg`; mp-test failure artifacts
  land in `/tmp/repgame/mptest_*`
- Capture-diff caveat: `capture_0` flat captures are bistable run-to-run —
  rerun a diff once before treating it as a regression (see the `capture`
  skill).
- Headless runs: `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy`.

## Flat worlds / model voxelizer

A save dir with a `world_type` file containing `flat` gets a sandstone floor
and no terrain/multiplayer; `tools/model_to_world.py` voxelizes 3D models
into such worlds. `make car` / `make car-run` build and launch the car
model(s) in `CAR_GLB` — multiple glbs go in a row along +x (incremental;
`make clean-car` wipes the world so the next `make car` regenerates).
See `docs/models.md`.

## Repo map

- `src/common/` — platform-independent game code (world, mobs, items,
  bodies, crafting, light, net sync)
- `src/common/game/` — the `RepGame` member-function implementation,
  split by concern (player input/physics, inventory+crafting UI, tick +
  game_harness.cpp for the scripted/debug tick phases, init/save, render,
  VR); `src/common/RepGame.cpp` keeps only the thin
  harness/state accessors. `include/common/game/` holds the matching state
  types — `RepGame.hpp` is the API; `game_state.hpp` carries
  `RepGameState` + `ScreenState`, with `player_state.hpp`, `ui_state.hpp`,
  `vr_state.hpp` for the sub-structs
- `src/common/harness/` — `RunPolicy`: the parsed capture/fly/walktest/
  teleport debug state; all `REPGAME_*` env reads go through
  `include/common/utils/env_config.hpp`. `harness/results.cpp` is the
  `REPGAME_RESULTS` JSONL event-stream writer (docs/debug-tools.md)
- `src/common/tests/` — the in-game `rep_tests` suite, one file per
  subsystem, run via `RepGame --tests` / `make tests`
- `src/common/utils/map_storage.cpp` + `player_data.cpp` — chunk files and
  `player.dat`; the player save is a human-readable text format
  (`include/common/player_data.hpp`), with the five historical binary
  layouts still accepted on read
- `src/linux/` — SDL3 desktop client (also base for ARM/SteamFrame/Windows)
- `src/server/` — dedicated server (`ServerEntities`, `ServerLogic`)
- `src/wasm/` — Emscripten client (SDL2, WASMFS, WebGPU backend)
- `src/shaders/` — GLSL/WGSL sources, preprocessed at build time
- `include/common/`, `include/server/` — headers. Keep the include graph
  shallow: `block_types.hpp` is the leaf block vocabulary (Block/BlockState/
  BlockCell, FACE_*/NO_LIGHT_* defines) — `block_definitions.hpp` is only
  the definition table; `chunk_render.hpp` holds Chunk's GL draw state so
  `chunk.hpp` needs no renderer headers. `item_definitions_gen.inc` lives
  in item.cpp — generated tables never go in headers.
- `data/` — block/item/recipe/shape/texture definitions (JSON + PNGs)
- `tools/` — importers, deploy scripts, test drivers (`mp_test.py`,
  `wasm_capture.cjs`, `rep-dbg` + `rep_dbg/` debug runner)
- `makefiles/` — per-platform `.mk` files included by `Makefile`; thin cmake
  wrappers + task/deploy/asset rules (Android compiles via its own Gradle
  CMake)
- `cmake/` + `CMakeLists.txt` — all C++ compilation parameterized by
  `REPGAME_PLATFORM` (linux/arm/steamframe/windows/wasm): blobs, shader
  preprocessing, WASM preload staging, nvcc/hipcc custom commands, cross
  toolchains in `cmake/toolchains/`, CMakePresets.json

`*_gen.*` sources and `bitmaps/blocks.png` / `bitmaps/mobs.png` are
generated by `make blocks-import` / `make mobs-import` — never hand-edit.

## Critical gotchas

- Block/item ids are never renumbered; id 1249 is permanently reserved.
- `make blocks-import` must stay idempotent — a dirty diff after rerun is a
  bug.
- Never use `SDL_OpenAudioDeviceStream` — it silently kills all SFX
  (docs/audio.md).
- `setenv`/`unsetenv` don't exist under mingw — `make windows` needs the
  `REPGAME_WINDOWS` shim in `src/common/tests/test_util.hpp` (test code)
  or `_putenv_s`; check before using POSIX env calls in shared code.
- GLSL hash math must stay `highp` (docs/rendering.md).
- Don't bundle runtime-provided libs in the flatpak (docs/platforms.md).
- Don't bump `SF_SNAPSHOT` blindly — later steamrt3 snapshots have no arm64
  images (docs/platforms.md).
- `make server-deploy` ships the binary to the live host;
  `make server-restart` restarts the live public server on repkap11.com.
- Keep headers standalone-parseable: forward-declare types used only by
  reference rather than pulling a big header mid-include (in particular,
  never `#include "common/RepGame.hpp"` from another header). `.clangd`
  points at the CMake compile DBs; include-cleaner diagnostics are off
  because several headers intentionally provide types transitively.
- GPU-animated particles (debris, hearts) live on the `u_Time` clock
  (`fmod(now, 3600)`): stamp spawn fields with the frame's `time_s`, not a
  mid-frame `now()` — and treat small negative ages as "not yet born",
  only diffs past half the period are a real rollover
  (`HeartParticles::expired`).

## Docs index — read before working on the matching area

- `docs/platforms.md` — all non-Linux builds, packaging and deploy targets
- `docs/vr.md` — OpenXR/VR session, input bindings, VR env vars
- `docs/rendering.md` — shaders, quality tiers, render distance, fog,
  light/mipmap tunables, fly-mode stress runs, FLYMET glossary
- `docs/audio.md` — sound pipeline, music/SFX runtime, audio env vars
- `docs/blocks.md` — block/texture JSON, importer, shapes, block world
- `docs/items-crafting.md` — items, drops, dropped-item entities, recipes,
  crafting UI
- `docs/mobs.md` — mob sim/AI, models/skins, spawner, persistence
- `docs/entities.md` — block bodies (falling blocks/TNT), arrows, and
  multiplayer entity sync
- `docs/multiplayer.md` — wire protocol, session handshake, chunk diffs,
  chest sync, world clock
- `docs/redstone.md` — redstone power model, mechanisms, the
  block-update event queue
- `docs/fluids.md` — water/lava flow sim, levels in rotation bits,
  lava reactions, buckets
- `docs/persistence.md` — `~/.repgame` layout, chunk/chest/mob/item
  files, `player.dat`/`world.dat`
- `docs/lighting.md` — light model: packed volume, skylight columns,
  BFS propagation, uploads
- `docs/chunks.md` — chunk torus, lifecycle, load/light/mesh/draw
  pipeline, parking
- `docs/player.md` — key bindings, movement physics, collision,
  mining/placement, game modes
- `docs/ui.md` — `RepGameState::ui`, inventory panels, chest/crafting
  UI, overlays
- `docs/models.md` — flat worlds, model voxelizer, `make car`
- `docs/mapgen.md` — terrain generation: generator selection, multi-backend
  dialect, legacy vs new fields, structures, editing gotchas
- `docs/env-vars.md` — table of every `REPGAME_*` variable
- `docs/debug-tools.md` — `rep-dbg` results model (`results.jsonl` records,
  `result.json` schema), subcommands, recipes
- `docs/lighting-thread-plan.md` — lighting worker/queue design notes

## Skills index

- `ui-layout` — add/move/debug UI panels through the shared Clay layout pass
- `mp-test` — run/extend/debug the multiplayer e2e harness (`make mp-test`)
- `capture` — deterministic screenshots, capture diffs, WASM capture,
  fly/walktest stress runs
- `add-content` — add a block, item or recipe (the blocks-import pipeline)
- `add-mob` — add a mob type (mobs-import pipeline)
- `debug-tools` — `tools/rep-dbg` structured runs, results.jsonl/result.json
  queries, `--rev` worktrees

## Doc upkeep

When behaviour, a make target or an env var in a subsystem changes, update
the matching `docs/*.md` / skill in the same change. Prefer pointing at
code (file/symbol) over restating volatile numbers.
