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
  multiples of 13px with nearest filtering (`FontRenderer::pixelScale`),
  rendered proportionally: each glyph advances by its ink width plus 1
  raster texel (`glyph_advance` in `font_renderer.cpp`), with a 4-texel
  space.

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
  (1..`CHUNK_RADIUS_XZ`, default `CHUNK_RADIUS_XZ_DEFAULT`; both are
per-platform defines in `chunk_dimensions.hpp`). The chunk torus is always allocated
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
- `LIGHT_LOD_BIAS` / `LIGHT_FAR_AO` / `LIGHT_FAR_OCC` — footprint-mipmapped
  light tunables, compile-time defines at the top of
  `src/shaders/chunk_fragment.glsl` (1.5 / 0.0 / 0.5). `LIGHT_LOD_BIAS`
  shifts the LOD higher, `LIGHT_FAR_AO` blends toward the solid-inclusive
  average (0: its height-parity-dependent signal aliases at distance),
  `LIGHT_FAR_OCC` is the strength of a band-limited occlusion term: the
  level-1 air fraction sampled `LIGHT_OCC_OFFSET` cells further out along
  the face normal darkens `light_far` where the volume reads concave
  (remapped via `smoothstep(0.4, 0.875, b)`, faded out between 2 and 1
  px/block since the fixed mip-1 probe aliases past ~1 px; probes outside
  the light volume are treated as open). The effective mip level is capped
  at `LIGHT_LOD_MAX`, so raising the bias past the cap has no further
  effect.
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
  Separately, flower/cross-quad plants (short grass, flowers) render with
  `face_light` 1.0 — like Minecraft's cross model with `shade=false` —
  since their quads reuse side-face labels that would otherwise shade them
  at 0.75 (`chunk_vertex.glsl`, `gl_VertexID >= FLOWER_FIRST_VERTEX`).
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

## Translucent blocks (stained glass, ice, honey, slime, ...)

Blocks with `renderOrder = RenderOrder_Translucent` draw in their own pass
inside `ChunkLoader::draw` (`src/common/chunk_loader.cpp`), after opaque and
flowers and before the dedicated water pass. `REPGAME_TRANSLUCENT_BLEND`
selects the compositing mode:

- `legacy` — the historical behaviour: translucent faces write depth while
  blending. Whatever draws first wins the depth test, so overlapping panes
  punch order-dependent holes in each other (visible mostly across chunk
  boundaries, where the back-to-front chunk order flips with camera
  position). Kept for A/B captures.
- `alpha` (default) — same alpha blending, but the translucent pass runs
  with `glDepthMask(GL_FALSE)` (the depth TEST stays on, so opaque terrain
  still occludes glass). Applies to the reflected pass too.
- `multiply` — same depth-mask-off, plus multiplicative "colour filter"
  blending (`glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE)`;
  FBO builds set it via `glBlendFuncSeparatei` on attachments 0 and 1 only,
  fog/sky attachments stay replace). The chunk shader emits an UNLIT
  tint*coverage filter (`u_TranslucentMultiply` in
  `src/shaders/chunk_fragment.glsl`) instead of a lit colour, so draw order
  doesn't matter at all and the tint can't go black at night.

Under `alpha`/`multiply` the depth-mask-off chunk order is the compositing
order, so `ChunkLoader::draw` walks a dedicated `blended_chunks` list — the
drawable chunks with any Translucent/Water layer instances — back-to-front,
sorted by squared distance to the camera with a deterministic chunk_pos
tie-break (ties were common around the render-origin corner, and unstable
sort order flickered whenever the drawable list rebuilt). Membership is
maintained incrementally: each slot mirrors its drawable/blended wish in a
`slot_want` byte (refreshed by `Chunk::refresh_slot_flags` on every
relevant field write), and `rebuild_drawable_list` diffs that byte array,
compacting removals out in place and appending newcomers to the list tail.
A re-sort then re-sorts the surviving prefix for the current camera, sorts
the few newcomers, and merges the two halves — so the blended list re-sorts
on any camera move while the full drawable list only re-sorts past
`CHUNK_SORT_RESORT_DIST`, since stale order there just costs early-z
efficiency. Faces *within* one chunk are still in mesh order — they are not
view-sorted.

`REPGAME_TRANSLUCENT_AFTER_WATER` (ignored under `legacy`) moves
the translucent order out of the main Blocks draw (`order_mask` bit
argument; glass keeps stencil writes off so it can't mark water pixels for
the reflection composite). Three values:

- `0` — translucent draws with the main Blocks pass, before
  water. Water seen through glass composites on top of the glass tint and
  washes it out.
- `1` — the whole translucent pass draws after the water pass. Glass *in
  front of* water composites correctly, but glass *below* the surface is
  depth-culled by the water surface (water writes depth), so sunken glass
  disappears from above and glass under water seen from below draws over
  the surface where it shouldn't.
- `split` (default; also used for unknown values) — translucent fragments are split at the sea-level water plane
  (`u_SplitY`/`u_SplitSign` in `chunk_vertex`/`chunk_fragment.glsl`, plane
  at `WATER_HEIGHT - 1` like the reflection plane): fragments on the far
  side from the camera draw before water, the camera side after — exact
  for sea-level water. Non-sea water (ponds, raised pools, water side
  faces) still misorders: on the camera's side of sea level it behaves
  like `1`, on the far side like `0`.

At HIGH+ the water reflection composite may still overlay glass where
glass covers water pixels. Weighted-blended OIT would be the full fix.

Since translucent faces no longer write depth, water emits no faces
against translucent neighbours (`liquid_boundary` in
`include/common/mesh_vis_logic.hpp`) — otherwise the coplanar water quad
shows through the glass (`tests/scenarios/glass_in_water.rds`). This
includes the water top face under glass at the surface.

Water (`RenderOrder_Water`) still writes depth in every mode: the depth
snapshot taken after the water pass feeds the X_HIGH water composite/SSR in
`full_screen_quad_fragment.glsl`, which reconstructs the surface from it.

### Sloped fluids

Water and lava render as variable-height surfaces (Minecraft's corner
average). Each fluid cell carries four corner heights in 1/64 units packed
into `BlockCoords::fluid_h0`/`fluid_h` (byte i = corner (i&1, i>>1)); the
vertex shader drops the `vd_data_water` cube's y=1 verts onto those
heights, so flowing cells slope and falling columns fill their cell. Cells
whose fluid is level everywhere emit the `*_STILL` top texture and keep
greedy merging; sloped or falling cells emit 1x1 with the `*_FLOW` top,
whose UVs the vertex shader rotates onto the surface gradient so the strip
scrolls downhill. A sub-source-height surface can also be perfectly level
(e.g. a sealed uniform-level patch — solid corner contributors are
skipped, so all four corners land on `own_height`): its `v_flow_dir` is
zero and its top must keep unrotated, unscaled UVs — the by-height
v-scale is for side-top verts only. Side faces always draw the flow
texture and scroll downward. Instance packing is flagged by `BLOCK_COORDS_FLUID` in
`face_shift` (fluids never yaw — `rotation` holds the sim level). Corner
heights come from `fluid_corner_height()`; per-face visibility is
recomputed on CPU in `calculate_populated_blocks` because phase-1 meshing
packs fluids as NO_LIGHT_BRIGHT on every face. The same helpers emit a
water shell instance for a waterlogged cell at the top of a water column
(it renders as a WATER source on its own cell; the host block's instance
is untouched). Fluid faces render unshaded (`face_light` 1.0 like
Minecraft's `shade=false`): directional shading made sky-lit internal
surfaces brighter than the walls they show through — per-cell banding
across flows. `water_still` is tinted toward `water_flow`'s hue in
`textures.json` (the Bedrock tiles were authored ~25 red levels apart, so
still-vs-flow boundaries read as purple-on-blue). Sim-side:
`docs/fluids.md`.

Known remaining limitations: `alpha` mode still composites in chunk/instance
order, so the hue of overlapping different-coloured panes can shift slightly
as the order flips (multiply fixes exactly this); and without
after-water mode, water draws after glass so water seen through glass
composites on top of the glass tint rather than behind it.

## Sign text (world space)

Sign front text renders as real 3D geometry, not screen overlays.
`WorldTextRenderer` (`include/common/world_text.hpp`,
`src/common/world_text.cpp`, shaders `world_text_vertex/fragment.glsl`)
batches glyph quads into its own VBO/IBO and draws them in the world
pass — after the opaque/translucent block draw and before the selection
outline and water passes, so terrain depth culls text correctly.

Each frame `World::collect_signs_near` gathers stored `SignText` records
within `SIGN_TEXT_DRAW_RADIUS` (24 blocks) of the camera; per sign,
`append_sign` lays out the four lines centred on the board's front face
using `FontRenderer` glyph metrics (`glyph_quads` — the same ProggyClean
atlas the 2D UI draws). Geometry is emitted in sign def space and yawed
with the block's `rotation`: the standing and hanging signs draw just
off their board plate's +z face (def z≈9, differing only in the board's
y band), the wall sign just off its plate front (def z≈2) —
`block_is_wall_sign`/`block_is_hanging_sign` pick the plate geometry.
Only the front face gets text. Vertices are quad-wound so they face the
readable side (front-face culling is on).

The fragment shader samples the font atlas and multiplies by the
per-sign tint — pure-black ink, like Minecraft signs — and applies the
standard distance/water fog uniforms, so the pass matches the terrain
around it. Depth test on, depth write
on: the quads sit ~0.01 off the board face so they can't z-fight the
plate but still hide behind walls.

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
  (a nonzero `cl` keeps the settle gate from opening). The same counters
  also land as structured `metric` records on the `REPGAME_RESULTS`
  stream — prefer `rep-dbg fly` + `result.json` stats over parsing FLYMET
  text (docs/debug-tools.md).
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
