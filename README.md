## RepGame

RepGame is a Minecraft clone written by @Repkap11 in C++.


## Building on Ubuntu
RepGame supports multiple platforms, all of which can be built on Linux (Ubuntu).
Building for Linux and Windows is relativly easy, but building WASM or Android, or AppImage, or FlatPak, or with CUDA support will require more setup.

The easiest/fastest way to build is to simply delete those features:
```bash
rm makefiles/{cuda,flatpak,wasm,appimage,android}.mk
```
Building WASM is very difficult. See wasm.mk. You will need to manually install emdsk, and switch to a custom fork of enscripten (https://github.com/Repkap11/emscripten) which fixes a few bugs. You will also need to adjust a hard-coded path in wasm.mk.

Once you've narrowed down your build platforms, you can can install the needed dependencies and build.
```bash
# You will need to type your password to install apt packages.
# Some files are also downloaded from www.repkap11.com
make install
make
```
Alternitivly, if you don't want to install packages on your Linux distro, you can build for Windows and Linux (including AppImage) inside docker.
Currently docker targets Ubuntu 22.04. This can be useful to create a more compatiable Linux executable.
You can attempt to build for older Linux versions with docker, but linking GLEW can be problematic.
```bash
# Setup docker...
# Install the dependencies which aren't system dependencies.
make entt_build imgui_build appimage_build
# Building the docker image automatically cleans up any dannging docker images or volumes, including non RepGame ones.
make docker-compile
```

To run the game execute:
```bash
./out/linux/release/RepGame World1 www.repkap11.com
```
Or use one of the make targets to build and run in 1 step:
```bash
make linux-run
```
Build outputs are a volume shared with the docker build.
You should do a clean build when switching to or from docker building.
Similarlly, when adding or removing CUDA support, you sould do a clean build.
```bash
make clean
make
```

## Play on Windows, Linux, Web, or Android
Build artifacts are avaliable on https://www.repkap11.com/repgame

Windows: https://repkap11.com/repgame//RepGame.exe \
Linux: https://repkap11.com/repgame/RepGame \
Web (Web Assembly): https://repkap11.com/repgame \
Andorid: http://repkap11.com/repgame/RepGame.apk

## Screenshots
![RepGame House](https://github.com/Repkap11/RepGame/blob/master/screenshots/repgame_house.png "RepGame House")
