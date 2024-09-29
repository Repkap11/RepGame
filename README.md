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

Building for Linux and Windows is relatively easy, but building WASM, Android, AppImage, FlatPak, or with CUDA support will require more setup.

The easiest/fastest way to build is to simply delete those features:
```bash
rm makefiles/{cuda,flatpak,wasm,appimage,android}.mk
```
Building WASM is very difficult. See wasm.mk. You will need to manually install emsdk, and switch to a custom fork of emscripten (https://github.com/Repkap11/emscripten) which fixes a few bugs. You will also need to adjust a hard-coded path in wasm.mk.

Building Android requires installing Android Studio and the required SDK and NDK versions.
Running a command line build at least once is required to add images and shaders to the build.
```bash
make android
```
Once run once, the project can be built in Android Studio if desired, or entirely on the command line.

## Building the Code
Once you've narrowed down your build platforms, you can can install the needed dependencies and build.
```bash
# You will need to type your password to install apt packages.
# Some files are also downloaded from www.repkap11.com
make install
make
```
Alternitivly, if you don't want to install packages on your Linux distro, you can build for Windows and Linux (including AppImage) inside docker.
Currently docker targets Ubuntu 22.04. This can be useful to create a more compatible Linux executable.
You can attempt to build for older Linux versions with docker, but linking GLEW can be problematic.
```bash
# Setup docker...
# Install the dependencies which aren't system dependencies.
make entt_build imgui_build appimage_build
# Building the docker image automatically cleans up any danging docker images or volumes, including non RepGame ones.
make docker-compile
```
## Running the Game
To play the game, run the final executable with a name for the world save, and a multiplayer server url. On Linux, worlds will be saved in ~/.repgame. On Windows, they are stored in the current directory.
```bash
./out/linux/release/RepGame World1 www.repkap11.com
```
Or use one of the make targets to build and run in 1 step:
```bash
make linux-run
make appimage-run
make wasm-run
make windows-run #If you have wine installed.
```
Non-docker builds share files with docker builds using a volume.
You should do a clean build when switching to or from docker based builds.
Similarly, when adding or removing CUDA support, you should do a clean build.
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
- "f" to fly
- "c" to no-clip
- "ESC" to exit

## Feature List
A short list of RepGame's features:

- Portable executable with all game assets and shaders are statically linked into the executable 
- Instance based rendering (glDrawElementsInstanced).
- Frustum culling.
- Block meshing.
- Rotatable textures (grass) while supporting instanced rendering and meshing.
- Position dependent index buffer based back face culling.
- Perlin noise terrain gen, with custom trees.
- CUDA accelerated terrain gen.
- Ambient occlusion style lighting.
- Practically 0 dynamic memory allocations.
- Ray-tracing based block selection and collision physics.
- Inventory and hotbar rendered with an interesting ENTT based instanced rendering implementation.
- Prototype redstone (you can use redstone blocks, dust and lamps)
- Multiple block models (half-slabs, glass-panes, torches, flowers, chests)
- Flowing water
- Deferred rendering pipeline with world space reflections in water. A standard forward rendering pipeline is also supported.
- Multiplayer support on Linux and Android.
- Multiplayer server for Linux. Many clients handled by a single thread using non-blockign IO and epoll.
- WIP: World file stored on server and synced between clients.
