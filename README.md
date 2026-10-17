## RepGame

RepGame is a Minecraft clone written by @Repkap11 in C++.

## Play on Windows, Linux, Web, or Android
Build artifacts are available on https://www.repkap11.com/repgame

Windows: https://repkap11.com/repgame//RepGame.exe \
Linux: https://repkap11.com/repgame/RepGame \
Web (Web Assembly): https://repkap11.com/repgame \
Android: http://repkap11.com/repgame/RepGame.apk

## Screenshots
![RepGame House](https://github.com/Repkap11/RepGame/blob/master/screenshots/repgame_house.png "RepGame House")

## Selecting Build Platforms
RepGame supports multiple platforms, all of which must be built on Ubuntu (or maybe any distro with the "apt" package manager).

Building for Linux and Windows is relatively easy, but building WASM, Android, AppImage, FlatPak, or with CUDA/HIP support will require more setup.

The easiest/fastest way to build is to simply delete those features:
```bash
rm makefiles/{cuda,hip,flatpak,wasm,appimage,android}.mk
```
Building WASM is very difficult. See wasm.mk. You will need to manually install emsdk, and switch to a custom fork of emscripten (https://github.com/Repkap11/emscripten) which fixes a few bugs. You will also need to adjust a hard-coded path in wasm.mk.

Building Android requires installing Android Studio and the required SDK and NDK versions.
Running a command line build at least once is required to add images and shaders to the build.
```bash
make android
```
Once run once, the project can be built in Android Studio if desired, or entirely on the command line.

Building with HIP support (AMD GPU accelerated terrain gen) requires the ROCm toolchain. HIP is enabled automatically when /usr/bin/hipcc is present; install it with:
```bash
make hip-install
```
The HIP kernels target generic code objects (gfx9 through gfx12 GPU families), so a single binary runs on every supported AMD GPU. This requires ROCm 6.0+ on the build machine. Builds without the toolchain (flatpak) link objects prebuilt by `make gpu-prebuilt` instead.

## Building the Code
Once you've narrowed down your build platforms, you can can install the needed dependencies and build.
```bash
# You will need to type your password to install apt packages.
# Some files are also downloaded from www.repkap11.com
make install
make
```
Alternitivly, if you don't want to install packages on your Linux distro, you can build a portable AppImage inside docker.
The docker image targets Ubuntu 24.04 and builds SDL3 from source, so the resulting AppImage has a glibc floor of 2.38 and runs on most modern x86_64 Linux distros.
```bash
# Setup docker...
# Install the dependencies which aren't system dependencies.
make entt_build imgui_build appimage_build
# Building the docker image automatically cleans up any danging docker images or volumes, including non RepGame ones.
make appimage-portable
```
## Running the Game
To play the game, run the final executable with a name for the world save, and a multiplayer server url. On Linux, worlds will be saved in ~/.repgame. On Windows, they are stored in the current directory.
```bash
./out/linux/release/RepGame World1 www.repkap11.com
```
On linux_debug version is compiled with "-g", no "-flto", no optimizations, and no FPS cap (to more easily see performance regressions).
The other builds are FPS caped so they don't burn your CPU/GPU.

Or use one of the make targets to build and run in 1 step:
```bash
make linux-run
make linux_debug-run
make appimage-host-run
make wasm-run
make windows-run #If you have wine installed.
```
Host and portable (Docker) builds use separate output directories (`out/linux` and `out/linux-portable`), so you can switch between them without a clean build.
However, when adding or removing CUDA or HIP support on the host, you should still do a clean build:
```bash
make clean
make
```

## Controls
- mouse to move camera
- "wasd" "okl;" or arrow keys to move
- "e" or "i" to open the inventory
- scroll-wheel to switch inventory page or hotbar slot
- mouse to select an item in the inventory
- "r" or "shift-r" to cycle render mode
- "m" to show meshing debug view
- "q" to clear a hotbar slot
- "p" to sprint
- "'" to sneak (prevents walking off edges), descend while flying, or sink while swimming
- "f" to fly
- "c" to no-clip
- "ESC" to exit

## Feature List
A short list of RepGame's features:

- Portable executable with all game assets and shaders are statically linked into the executable 
- AppImage and Flatpak packaging.
- Instance based rendering (glDrawElementsInstanced).
- Frustum culling.
- Block meshing.
- Rotatable textures (grass) while supporting instanced rendering and meshing.
- Position dependent index buffer based back face culling.
- Perlin noise terrain gen with biomes, mountains, overhangs, custom trees, and decorations (flowers, bamboo, mushrooms, coral, seaweed).
- CUDA (NVIDIA) and HIP (ROCm/AMD) accelerated terrain gen.
- Flood-fill sky and block lighting stored in a mipmapped 3D light volume.
- Practically 0 dynamic memory allocations.
- Ray-tracing based block selection and collision physics.
- Creative and survival game modes; survival consumes placed blocks and collects mined ones.
- Persistent world and player saves (position, game mode, time, inventories, chunks).
- Inventory and hotbar rendered with an interesting ENTT based instanced rendering implementation.
- Prototype redstone (blocks, dust, torches, lamps, pressure plates, pistons).
- Exploding TNT.
- Instanced block-breaking debris particles.
- Multiple block models (half-slabs, glass-panes, torches, flowers, chests)
- Chest inventories that persist and sync over multiplayer.
- Flowing water
- Day/night cycle with a textured sky box, synced between multiplayer clients.
- Distance fog blended toward the sky color.
- Deferred rendering pipeline with world space reflections in water. A standard forward rendering pipeline is also supported.
- Selectable render quality tiers (LOW, MEDIUM, HIGH, X_HIGH) with MSAA and mipmapped textures.
- ImGui debug overlay, and an env-var driven capture mode for deterministic screenshots.
- Multiplayer support on Linux, Android, and Web (WASM), with lit player avatars.
- Multiplayer server for Linux. Many clients handled by a single thread using non-blocking IO and epoll.
- WIP: World file stored on server and synced between clients.
