#pragma once

#ifdef REPGAME_LINUX
#include "linux/RepGameLinux.hpp"
#endif
#ifdef REPGAME_WINDOWS
#include "windows/RepGameWindows.hpp"
#endif
#ifdef REPGAME_ANDROID
#include "android/RepGameAndroid.hpp"
#endif
#ifdef REPGAME_WASM
#include "wasm/RepGameWASM.hpp"
#endif