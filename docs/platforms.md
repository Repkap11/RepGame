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
- Linux build knobs: `LINUX_CXX` (compiler), `LINUX_USE_LLD`, `LINUX_LTO`
  (thin LTO on the Release build via `REPGAME_LTO`/`CMAKE_INTERPROCEDURAL_OPTIMIZATION`,
  default 1 — `make linux LINUX_LTO=0` disables it). With clang+lld a ThinLTO
  cache under `<build dir>/thinlto-cache` makes incremental release relinks
  cheap (wiped by clean-linux); the gcc flatpak path gets no cache.
- `make player` — delete `~/.repgame/$(WORLD)/player.dat`.
- `make bedrock_samples_build` — clone Mojang's bedrock-samples (pinned
  `BEDROCK_SAMPLES_TAG`) into `bedrock-samples/` (git-ignored); the input
  to `make blocks-import` / `make mobs-import` (see docs/blocks.md).
- `make sounds-prebuilt` — convert every manifest sound into
  `prebuilt/sounds/` on the host; `make flatpak` consumes these (its
  sandbox has no ffmpeg).
- `make hip` / `make cuda` — compile the GPU mapgen/mesh objects (cmake
  custom commands around hipcc/nvcc — cmake/RepGameGpu.cmake — driven from a
  GPU-only configure in `$(LINUX_OUT)/cmake-gpu`).
  `make hip-install` installs the HIP toolchain.
- `make clean` / `make clean-<platform>` — remove `out/` trees
  (`clean-linux`, `clean-android`, `clean-arm`, `clean-appimage`,
  `clean-bitmaps`, `clean-cuda`, `clean-flatpak`, `clean-hip`,
  `clean-openxr`, `clean-server`, `clean-sounds`, `clean-steamframe`,
  `clean-wasm`, `clean-windows`).

## ARM build (aarch64 board, GLES/Wayland)

Cross-compiles the Linux client for an aarch64 board (PowerVR GLES 3.2,
Weston; device now runs Ubuntu 24.04) using a Yocto SDK on the host. `REPGAME_GLES` selects the GLES port:
GLES headers instead of glew, an ES 3.2 context, `glTexStorage2DMultisample`
resizes + `glCopyImageSubData` for the depth snapshot, and its own even
smaller chunk radii (`CHUNK_RADIUS_XZ` 6, default 1 — below the
Android/WASM small torus) since the AXM-8-256 is a weak GPU.
SDL3 is built static from source under `out/arm-sdl3` with the SDK toolchain.
Compile is CMake (`REPGAME_PLATFORM=arm`, toolchain
`cmake/toolchains/arm.cmake` driven by `ARM_SDK`); arm.mk is the task runner,
so arm can be built in the same invocation as other platforms (e.g.
`make linux arm`).

- `make arm` / `make arm_debug` — release/debug binaries in `out/arm/`.
- `make arm-sdl3` — (re)build the static SDL3.
- `make arm-deploy` — rsync the release binary to `$(ARM_DEVICE):$(ARM_DEPLOY_DIR)`.
- `make arm-run` / `make arm_debug-run` — deploy + run over ssh.
- Overrides: `ARM_SDK` (Yocto SDK path), `ARM_DEVICE` (default
  `ubuntu@renesas-v4h`), `ARM_DEPLOY_DIR`, `ARM_SDL3_VERSION`.
- Running over ssh needs Wayland env vars:
  `XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1`.
- GPU on the Ubuntu image (`libpvr1-v4h`): the proprietary GLES stack lives
  in `/usr/lib/aarch64-linux-gnu/pvr` and is NOT registered with glvnd, so a
  plain run loads Mesa — Zink rejects the PowerVR Vulkan device
  (`fillModeNonSolid`) and you get llvmpipe. `arm-run`/`arm_debug-run` set
  `LD_LIBRARY_PATH` to the pvr dir (same thing the image's `pvr-run` wrapper
  does); check the `Using OpenGL Renderer` log line — `PowerVR A-Series
  AXM-8-256` means GPU, `llvmpipe` means Mesa. On the Yocto BSP the pvr libs
  were the system default so no wrapper was needed.
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
Compile is CMake (`REPGAME_PLATFORM=steamframe`); the SDL3/OpenXR/RepGame
builds share the checked-in `cmake/toolchains/steamframe.cmake`.

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

- `make wasm` — Emscripten build into `out/wasm/delivery/` (needs emsdk —
  `make emsdk_build` clones it into `emsdk/`; override with
  `EM_ROOT` or `EMSDK_VERSION`. `make wasm-install` installs the
  host packages). Compile/link is CMake (`REPGAME_PLATFORM=wasm`, emsdk's
  Emscripten.cmake); the WASMFS preload is staged under `out/wasm/cmake/fs`.
- `make wasm-start-server` — serve `out/wasm/delivery` on :8080 via
  `wasm_server.py` (COOP/COEP headers for SharedArrayBuffer).
- `make wasm-run` — build + open in Chrome (`WASM_START_COMMAND`).
- `make wasm-deploy` — rsync `out/wasm/delivery/` to repkap11.com.

## Windows

- `make windows` — cross-compile `out/windows/RepGame.exe` (CMake
  `REPGAME_PLATFORM=windows`, mingw toolchain
  `cmake/toolchains/windows-mingw.cmake`); `SDL3.dll` is copied next to the
  exe post-build.
- `make windows-run` — run the exe under wine (single-player `WORLD_DEBUG`).
- `make windows-deploy` — rsync the exe to repkap11.com.

## Android

- `make android` / `make android_debug` — Gradle APK build; shaders,
  bitmaps and fonts embed in libRepGameAndroid.so as blobs like the
  desktop platforms (android/app/CMakeLists.txt reuses
  `cmake/RepGameBlobs.cmake`).
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
  `GPU_PREBUILT_DIR` (see cmake/RepGameGpu.cmake) and installs the runtime libs the
  freedesktop runtime lacks into `/app/lib` (see `flatpak/com.repkap11.repgame.yml`
  for the whitelist — do NOT bundle runtime-provided libs like libdrm_amdgpu,
  they shadow the GL extension's copies and break EGL).
  `gpu-prebuilt-in-docker` is the in-container half of that target.

## Server packaging

- `make server-install` — one-time setup for a server-only host: apt-installs
  just `SERVER_PACKAGES` (clang, ccache, cmake, ninja, wget, bsdtar, tmux) and
  fetches `entt_build/`. No SDL3/GLEW/CUDA/HIP/imgui — use it instead of
  `make install` on repkap11.com.
- `make server-deploy` — rsync `out/server/RepGameServer` to
  repkap11.com's `/home/paul/website/repgame/` like the other deploy
  targets. The VPS is too small to compile optimized builds, so nothing
  builds or pushes on the remote; both ends run the same toolchain. rsync's
  temp-file+rename is safe against the running binary.
- `make server-restart` — `tools/server_restart.sh`: `systemctl --user
  restart RepGameServer` on repkap11.com; SIGTERM triggers the server's
  graceful flush. Overrides: `SERVER_HOST`, `SERVER_SERVICE`.
- `make server-systemd-install` — installs `RepGameServer.service` as a
  user-level unit (`~/.config/systemd/user`) on repkap11.com, enables it,
  and sets `loginctl enable-linger` so it starts on boot without a login.
  No sudo needed. `server-status`, `server-logs-live` and
  `server-systemd-uninstall` wrap the matching `systemctl --user`/
  `journalctl --user` calls over ssh.
  `make server-logs` dumps the last `LOG_LINES` (200) journal lines —
  the tmux capture-pane equivalent; `server-logs-live` follows live.
- `make server-systemd-install` / `server-status` /
  `server-logs-live` / `server-systemd-uninstall` — install, inspect and
  remove the `RepGameServer.service` systemd unit.
