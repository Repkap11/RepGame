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
into `out/<platform>/shaders/`. Android/WASM also get
`-DREPGAME_LOW_GRAPHICS`; all low-end conditionals must use
`#if defined(REPGAME_LOW_GRAPHICS)` / `#if !defined(REPGAME_LOW_GRAPHICS)`
and LOW builds must preprocess to identical output when a desktop-only
feature is added.

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

Per-view `flags` is a bitmask bound to `u_DebugFlags` in the chunk shader
(non-LOW builds only; LOW ignores it):

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
