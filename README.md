## RepGame

RepGame is a simple game, modeled after Minecraft, mostly to learn about OpenGL.
RepGame was created by @Repkap11. RepGame is written in C/C++.


## Building on Linux
RepGame requires some dependencies to be installed on the your system before you can build. You can install these dependencies automatically by running 'make install' and typing your sudo password. After that, you can run "make linux" to build the project for Linux. Other platforms like "windows", "android" and "wasm" can be build from a Linux host as well. The target "linux-run" can be used to build and run the Linux build for easier testing.

## Building on Windows
Install Git (obviously)
Install Make http://gnuwin32.sourceforge.net/packages/make.htm and manually add to PATH
Install bsdtar https://www.libarchive.org/ and manually add to PATH. I had the error: "error while loading shared libraries: api-ms-win-crt-math-l1-1-0.dll" and had to manually install Visual Studio to get the right DLL's. I also HAD to use Edge to download Visual Studio.
Run "make windows_build" to download all the windows dependencies. If any error occurs, delete the windows_build folder and try again.
Run "make "

## Play on Windows, Linux, Web, or Android
Windows: https://repkap11.com/repgame//RepGame.exe \
Linux: https://repkap11.com/repgame/RepGame \
Web (Web Assembly): https://repkap11.com/repgame \
Andorid: http://repkap11.com/repgame/RepGame.apk

## Screenshots
![RepGame House](https://github.com/Repkap11/RepGame/blob/master/screenshots/repgame_house.png "RepGame House")
