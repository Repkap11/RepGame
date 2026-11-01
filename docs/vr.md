# VR (OpenXR)

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

Builds:

- `make linux REPGAME_OPENXR=1` — desktop VR build at `out/linux-openxr/`;
  needs an OpenXR runtime offering `XR_MNDX_egl_enable` (e.g. Monado —
  SteamVR for Linux x64 lacks it).
- `make steamframe` — Steam Frame build (static OpenXR loader), see
  `docs/platforms.md`.

Note: `capture_0` flat captures are bistable run-to-run (pre-existing
nondeterminism, reproducible on the unmodified binary), so a capture diff
should be rerun once before being treated as a regression.
