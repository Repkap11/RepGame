# Platform builds, packaging and deploy targets

Everything here is a `make` target. The everyday Linux build/run/test
targets are in `AGENTS.md`; this file covers the other platforms and the
packaging/deploy plumbing. Internal helper targets (`*_build` dirs,
`imgui_build`, `entt_build`, `packages`, `install`, `vars`, `nothing`,
file-level `out/...` rules) are omitted on purpose.

## Linux extras

- `make linux-deploy` — rsync the release binary to repkap11.com's website
  dir. `make deploy` builds the portable AppImage and rsyncs it instead
  (see AppImage below).
- `make linux-run2` — build and launch against `www.repkap11.com` with the
  `WORLD_DEBUG` world (second client alongside `linux-run`).
- `make player` — delete `~/.repgame/$(WORLD)/player.dat`.
- `make update-world` — prepend an 8-byte header to every `chunk_*` file
  under `~/.repgame/$(WORLD)/` (save-format migration helper).
- `make bedrock-samples` — clone Mojang's bedrock-samples (pinned
  `BEDROCK_SAMPLES_TAG`) into `bedrock-samples/` (git-ignored); the input
  to `make blocks-import` / `make mobs-import` (see docs/blocks.md).
- `make sounds-prebuilt` — convert every manifest sound into
  `prebuilt/sounds/` on the host; `make flatpak` consumes these (its
  sandbox has no ffmpeg).
- `make hip` / `make cuda` — compile the GPU mapgen/mesh objects.
  `make hip-install` installs the HIP toolchain.
- `make clean` / `make clean-<platform>` — remove `out/` trees
  (`clean-linux`, `clean-android`, `clean-arm`, `clean-appimage`,
  `clean-bitmaps`, `clean-cuda`, `clean-flatpak`, `clean-hip`,
  `clean-openxr`, `clean-server`, `clean-sounds`, `clean-steamframe`,
  `clean-wasm`, `clean-windows`).

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
- `make steamframe-sysroot` / `make steamframe-sdl3` / `make clean-steamframe-deps`
  — fetch the sysroot, (re)build SDL3, wipe both.
- `make openxr-host` / `make openxr-steamframe` — static OpenXR-SDK 1.1.63
  loader installs under `out/openxr-{host,steamframe}/install`.

## WASM

- `make wasm` — Emscripten build into `out/wasm/delivery/` (needs emsdk;
  `make wasm-install` installs the host packages).
- `make wasm-start-server` — serve `out/wasm/delivery` on :8080 via
  `wasm_server.py` (COOP/COEP headers for SharedArrayBuffer).
- `make wasm-run` — build + open in Chrome (`WASM_START_COMMAND`).
- `make wasm-deploy` — rsync `out/wasm/delivery/` to repkap11.com.

## Windows

- `make windows` — cross-compile `out/windows/RepGame.exe`.
- `make windows-run` — run the exe under wine (single-player `WORLD_DEBUG`).
- `make windows-deploy` — rsync the exe to repkap11.com.

## Android

- `make android` / `make android_debug` — assets + APK build
  (`android-shaders`, `android-bitmaps`, `android-fonts`,
  `android-find-new-files` are the sub-steps).
- `make android-run` / `make android_debug-run` — install to a device over
  adb and tail logcat.
- `make android-deploy` — rsync the release APK to repkap11.com.

## Docker

- `make docker-image` — build the `repgame` image (Ubuntu 24.04, source SDL3,
  HIP + CUDA toolchains).
- `make docker-shell` — shell inside the image with the repo mounted.
- `make docker-run` — build + run the AppImage inside the container
  (X11 forwarded, `--gpus all`).
- `make docker-save` — export the image to `repgame_docker.tar.gz`.

## AppImage / flatpak

- `make appimage-host` / `make appimage-host-run` — host-built AppImage
  (not portable — uses your host's glibc and libs) into `out/appimage`.
- `make appimage-portable` — portable AppImage built inside Docker
  (Ubuntu 24.04, glibc 2.38 floor) into `out/appimage-portable`, using
  separate output dirs so host and Docker artifacts don't clobber each
  other. `make appimage-portable-run` runs it;
  `make appimage-portable-deploy` rsyncs it to repkap11.com (this is what
  `make deploy` does).
- `make flatpak` — build + install the flatpak (`flatpak run com.repkap11.repgame`).
  The flatpak `command:` is `flatpak/RepGameRun`, a wrapper like the AppImage's
  AppRun: no args → `RepGameBin World1 repkap11.com` (public server);
  explicit args pass through. `make flatpak-run` runs it.
- `make gpu-prebuilt` — compile HIP/CUDA objects + harvest runtime libs in Docker
  into `prebuilt/gpu` (gitignored). `make flatpak` depends on this: the flatpak
  sandbox has no hipcc/nvcc, so its manifest links the prebuilt objects via
  `GPU_PREBUILT_DIR` (see hip.mk/cuda.mk) and installs the runtime libs the
  freedesktop runtime lacks into `/app/lib` (see `flatpak/com.repkap11.repgame.yml`
  for the whitelist — do NOT bundle runtime-provided libs like libdrm_amdgpu,
  they shadow the GL extension's copies and break EGL).
  `gpu-prebuilt-in-docker` is the in-container half of that target.

## Server packaging

- `make server-deploy` — `tools/server_deploy.sh`: push HEAD to the `repkap11`
  remote's master, ff-pull + `make server` on repkap11.com (build happens before
  the old server is stopped), Ctrl-C the server in tmux session `repgame`
  (pane found via the `@repgame_server` pane option, since automatic-rename
  clobbers window names; graceful flush), start `make server-run` there, then attach.
  Uncommitted changes only warn. Aborts if `RepGameServer.service` is active or
  another RepGameServer is running outside that session. Overrides:
  `SERVER_HOST`, `SERVER_DIR`, `SERVER_GIT_REMOTE`, `SERVER_BRANCH`,
  `SERVER_TMUX_SESSION`, `SERVER_ATTACH=0`.
- `make server-systemd-install` / `server-systemd-status` /
  `server-systemd-logs` / `server-systemd-uninstall` — install, inspect and
  remove the `RepGameServer.service` systemd unit.
