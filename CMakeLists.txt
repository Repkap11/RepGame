cmake_minimum_required(VERSION 3.21)
project(RepGame CXX)

# One CMake build parameterized by platform (REPGAME_PLATFORM):
# linux (client/server/GPU), arm, steamframe, windows, wasm. Android reuses
# cmake/RepGameSources.cmake and cmake/RepGameBlobs.cmake from its own
# Gradle-driven CMakeLists.
# Make stays the task runner — see makefiles/*.mk.
set(REPGAME_PLATFORM "linux" CACHE STRING "Target platform: linux|arm|steamframe|windows|wasm")
set_property(CACHE REPGAME_PLATFORM PROPERTY STRINGS linux arm steamframe windows wasm)

# Blobs are .S files on every platform except wasm, which ships files in the
# WASMFS preload instead.
if(NOT REPGAME_PLATFORM STREQUAL "wasm")
	enable_language(ASM)
	# The cross toolchains set CMAKE_CXX_COMPILER_TARGET; ASM doesn't inherit
	# it, so the integrated assembler would default to the host CPU.
	if(CMAKE_CXX_COMPILER_TARGET)
		set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} --target=${CMAKE_CXX_COMPILER_TARGET}")
	endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CMake's default Release adds -DNDEBUG, which would change assert behaviour.
# Keep today's flag sets exactly.
set(CMAKE_CXX_FLAGS_RELEASE "-O3" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-g" CACHE STRING "" FORCE)

option(REPGAME_BUILD_CLIENT "Build the client" ON)
option(REPGAME_BUILD_SERVER "Build the dedicated server (linux only)" OFF)
set(REPGAME_OUTPUT_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Directory the client binary is written to")
option(REPGAME_OPENXR "Build with OpenXR (VR) support" OFF)
set(REPGAME_OPENXR_PREFIX "${CMAKE_SOURCE_DIR}/out/openxr-host/install" CACHE PATH "OpenXR loader install prefix")
option(REPGAME_AUDIO "Embed sounds (requires converted .ogg files)" OFF)
set(REPGAME_SOUND_OGG_DIR "${CMAKE_SOURCE_DIR}/out/sounds" CACHE PATH "Directory containing converted .ogg files")
# Final link goes through lld: fastest maintained linker and produces the
# smallest binaries here. Link-only flag — clang's -Werror rejects -fuse-ld
# on -c compiles as an unused argument.
option(REPGAME_USE_LLD "Link with -fuse-ld=lld" ON)
option(REPGAME_LTO "Link-time optimization for Release builds (linux client)" ON)
option(REPGAME_FRAME_POINTERS "Keep frame pointers (perf --call-graph fp)" OFF)
set(REPGAME_SANITIZER "" CACHE STRING "Sanitizer variant: '' or 'asan' or 'tsan'")
# Real LTO via CMake's IPO support: clang gets -flto=thin on compile AND link,
# gcc -flto/-fno-fat-lto-objects. Set globally before targets exist so RepGame
# and the repgame_imgui OBJECT library both pick it up; Release config only.
# No check_ipo_supported() — its probe wouldn't use our -fuse-ld=lld and can
# fail spuriously with clang + GNU ld.
if(REPGAME_LTO AND REPGAME_PLATFORM STREQUAL "linux")
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()
set(REPGAME_GPU_PREBUILT_DIR "" CACHE PATH "Prebuilt GPU objects (prebuilt/gpu) when no toolchain is present")
set(REPGAME_GPU_OUT "${CMAKE_BINARY_DIR}/gpu" CACHE PATH "Output directory for built GPU objects")
# Static SDL3 install prefix for the cross builds (arm-sdl3 / steamframe-sdl3
# make targets build it; the headers are on every compile line).
set(REPGAME_SDL3_PREFIX "" CACHE PATH "Static SDL3 install prefix (arm/steamframe)")
if(EXISTS /usr/bin/nvcc)
	set(_cuda_default ON)
else()
	set(_cuda_default OFF)
endif()
if(EXISTS /usr/bin/hipcc)
	set(_hip_default ON)
else()
	set(_hip_default OFF)
endif()
option(REPGAME_CUDA "Build CUDA GPU objects" ${_cuda_default})
option(REPGAME_HIP "Build HIP GPU objects" ${_hip_default})

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/entt_build)
	message(FATAL_ERROR "entt_build/ is missing — run `make install` (or `make server-install` on a server-only host) first")
endif()
# The dedicated server never includes ImGui, so a server-only host doesn't
# need imgui_build/.
if(REPGAME_BUILD_CLIENT AND NOT EXISTS ${CMAKE_SOURCE_DIR}/imgui_build)
	message(FATAL_ERROR "imgui_build/ is missing — run `make install` first")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(RepGameSources)
include(RepGameBlobs)
if(REPGAME_PLATFORM STREQUAL "linux")
	include(RepGameGpu)
endif()

# Shared compile flags — the same warning set on every platform: warnings are
# errors so a warning on one toolchain can't silently ship on another.
add_library(repgame_flags INTERFACE)
target_compile_options(repgame_flags INTERFACE
	-Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable)
if(REPGAME_FRAME_POINTERS)
	target_compile_options(repgame_flags INTERFACE
		-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer)
endif()
# Sanitizer variants (make linux_asan / linux_tsan presets). Debug-ish -O1
# keeps stacks readable; no LTO. When set, the linux link drops
# -static-libgcc/-static-libstdc++ (see the RepGame link line).
if(REPGAME_SANITIZER STREQUAL "asan")
	target_compile_options(repgame_flags INTERFACE -O1 -g -fno-omit-frame-pointer
		-fsanitize=address,undefined)
	target_link_options(repgame_flags INTERFACE
		-fsanitize=address,undefined)
elseif(REPGAME_SANITIZER STREQUAL "tsan")
	target_compile_options(repgame_flags INTERFACE -O1 -g -fno-omit-frame-pointer
		-fsanitize=thread)
	target_link_options(repgame_flags INTERFACE -fsanitize=thread)
endif()
# In release mode, don't check for GL errors. HW vsync is a runtime toggle on
# the 'v' key (default on), not a compile flag.
target_compile_definitions(repgame_flags INTERFACE
	$<$<CONFIG:Release>:REPGAME_SKIP_CHECK_FOR_GL_ERRORS>
	$<$<CONFIG:Debug>:REPGAME_DEBUG>)
target_include_directories(repgame_flags INTERFACE
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/include/glm
	${CMAKE_SOURCE_DIR}/imgui_build
	${CMAKE_SOURCE_DIR}/imgui_build/backends)
target_include_directories(repgame_flags SYSTEM INTERFACE
	${CMAKE_SOURCE_DIR}/entt_build/src)

# Platform-specific compile/link config. repgame_platform carries the
# defines/includes/pic flags that differ per platform.
add_library(repgame_platform INTERFACE)

if(REPGAME_PLATFORM STREQUAL "linux")
	# Nothing uses GLU (gluErrorString has a local replacement in
	# Logging.hpp); GLEW_NO_GLU stops glew.h from pulling in <GL/glu.h>.
	target_compile_definitions(repgame_platform INTERFACE REPGAME_LINUX GLEW_NO_GLU)
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		# clang compiles -fPIE (it wins over -fno-pie in the old flags) and
		# links the toolchain default; -Wno-c++20-designator for the C++20
		# designated initializers used throughout.
		target_compile_options(repgame_platform INTERFACE -fPIE -Wno-c++20-designator)
	else()
		# g++: non-PIE objects and a non-PIE link.
		target_compile_options(repgame_platform INTERFACE -fno-pie)
		target_link_options(repgame_platform INTERFACE -no-pie)
		# GCC 15 (flatpak SDK) flags every bounded snprintf whose %s args
		# could exceed the buffer; truncation there is intended, so don't
		# fail on it.
		target_compile_options(repgame_platform INTERFACE -Wno-format-truncation)
	endif()
	set(REPGAME_PLATFORM_SHADER_DEFINES -DREPGAME_LINUX)
	set(REPGAME_IMGUI_BACKEND ${CMAKE_SOURCE_DIR}/imgui_build/backends/imgui_impl_sdl3.cpp)
	# SDL3 is dynamically linked, because it's too hard to statically link
	# against X11, and I don't want to compile it from source. Client-only:
	# the dedicated server has no client deps (repkap11.com builds it via
	# `make server` on a host without SDL3/Wayland dev packages).
	if(REPGAME_BUILD_CLIENT)
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(SDL3 REQUIRED sdl3)
		pkg_check_modules(WAYLAND REQUIRED wayland-client)
		target_compile_options(repgame_platform INTERFACE ${SDL3_CFLAGS_OTHER} ${WAYLAND_CFLAGS_OTHER})
		target_include_directories(repgame_platform INTERFACE ${SDL3_INCLUDE_DIRS} ${WAYLAND_INCLUDE_DIRS})
	endif()
elseif(REPGAME_PLATFORM STREQUAL "arm" OR REPGAME_PLATFORM STREQUAL "steamframe")
	# Same warning/std/pic flags as the linux clang build, but GLES instead
	# of desktop GL+GLEW and the static SDL3 headers from the sysroot build.
	target_compile_definitions(repgame_platform INTERFACE
		REPGAME_LINUX GLEW_NO_GLU REPGAME_GLES IMGUI_IMPL_OPENGL_ES3)
	target_include_directories(repgame_platform INTERFACE ${REPGAME_SDL3_PREFIX}/include)
	set(REPGAME_IMGUI_BACKEND ${CMAKE_SOURCE_DIR}/imgui_build/backends/imgui_impl_sdl3.cpp)
	if(REPGAME_PLATFORM STREQUAL "arm")
		target_compile_options(repgame_platform INTERFACE -fno-pie -fPIE -Wno-c++20-designator)
		set(REPGAME_PLATFORM_SHADER_DEFINES -DREPGAME_LINUX -DREPGAME_GLES)
	else()
		# -march=armv8.2-a stays a platform flag (NOT the toolchain file) so
		# the SDL3/OpenXR builds keep their flags. steamframe adds GLES +
		# OpenXR on top of the arm config.
		target_compile_options(repgame_platform INTERFACE -march=armv8.2-a -fPIE -Wno-c++20-designator)
		target_compile_definitions(repgame_platform INTERFACE REPGAME_STEAMFRAME)
		set(REPGAME_PLATFORM_SHADER_DEFINES -DREPGAME_LINUX -DREPGAME_GLES -DREPGAME_STEAMFRAME)
	endif()
elseif(REPGAME_PLATFORM STREQUAL "windows")
	# SDL3 MinGW devel only ships a shared library (libSDL3.dll.a + SDL3.dll),
	# so Windows dynamically links SDL3 and ships SDL3.dll next to the exe.
	target_compile_definitions(repgame_platform INTERFACE
		GLEW_STATIC GLEW_NO_GLU _WIN32_WINNT=0x0A00 REPGAME_WINDOWS)
	target_compile_options(repgame_platform INTERFACE -fno-pie -mwindows)
	target_link_options(repgame_platform INTERFACE -mwindows)
	# windows_build (make windows_build): SDL3 MinGW devel + glew, used
	# before the common includes just like INCLUDES_WINDOWS.
	target_include_directories(repgame_platform BEFORE INTERFACE
		${CMAKE_SOURCE_DIR}/windows_build/sdl3/x86_64-w64-mingw32/include/SDL3
		${CMAKE_SOURCE_DIR}/windows_build/sdl3/x86_64-w64-mingw32/include
		${CMAKE_SOURCE_DIR}/windows_build/glew/include)
	set(REPGAME_PLATFORM_SHADER_DEFINES -DREPGAME_WINDOWS)
	set(REPGAME_IMGUI_BACKEND ${CMAKE_SOURCE_DIR}/imgui_build/backends/imgui_impl_sdl3.cpp)
elseif(REPGAME_PLATFORM STREQUAL "wasm")
	# USE_SDL is needed at compile time too: modern emscripten ports are no
	# longer installed into the shared sysroot, the flag injects the SDL2
	# include path. emdawnwebgpu likewise injects <webgpu/webgpu.h> — the
	# Dawn-based WebGPU binding for browser GPU mapgen (see
	# src/shaders/map_gen.wgsl and src/wasm/webgpu_mapgen.cpp).
	target_compile_definitions(repgame_platform INTERFACE REPGAME_WASM)
	target_compile_options(repgame_platform INTERFACE
		-Wno-c++20-designator -pthread -s USE_SDL=2 --use-port=emdawnwebgpu)
	set(REPGAME_IMGUI_BACKEND ${CMAKE_SOURCE_DIR}/imgui_build/backends/imgui_impl_sdl2.cpp)
else()
	message(FATAL_ERROR "Unknown REPGAME_PLATFORM: ${REPGAME_PLATFORM}")
endif()

if(REPGAME_AUDIO)
	target_compile_definitions(repgame_flags INTERFACE REPGAME_AUDIO)
	target_include_directories(repgame_flags INTERFACE ${CMAKE_BINARY_DIR}/include)
	# Generated MK_BLOB decls + music/sfx tables included by audio.cpp.
	set(SOUNDS_MANIFEST_HPP ${CMAKE_BINARY_DIR}/include/sounds_manifest.hpp)
	add_custom_command(OUTPUT ${SOUNDS_MANIFEST_HPP}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/include
		COMMAND ${CMAKE_SOURCE_DIR}/tools/convert_sounds.sh gen ${SOUNDS_MANIFEST_HPP}
		DEPENDS ${CMAKE_SOURCE_DIR}/tools/sounds_manifest.txt ${CMAKE_SOURCE_DIR}/tools/convert_sounds.sh
		COMMENT "convert_sounds.sh gen sounds_manifest.hpp"
		VERBATIM)
	set_source_files_properties(${CMAKE_SOURCE_DIR}/src/common/audio.cpp
		PROPERTIES OBJECT_DEPENDS ${SOUNDS_MANIFEST_HPP})
endif()

# ---- blobs (every platform except wasm) -----------------------------------
set(REPGAME_BLOB_SOURCES)
if(REPGAME_BUILD_CLIENT AND NOT REPGAME_PLATFORM STREQUAL "wasm")
	repgame_asset_blobs(asset_blobs shader_processed DEFINES ${REPGAME_PLATFORM_SHADER_DEFINES})
	list(APPEND REPGAME_BLOB_SOURCES ${asset_blobs})

	if(REPGAME_AUDIO)
		# Sound blob per manifest entry; the .ogg conversion stays in make —
		# the wrapper builds the files before cmake runs.
		file(STRINGS ${CMAKE_SOURCE_DIR}/tools/sounds_manifest.txt _sound_lines)
		foreach(line IN LISTS _sound_lines)
			if(line MATCHES "^([a-z][a-z0-9_]*)\\|")
				set(name ${CMAKE_MATCH_1})
				repgame_blob(blob out_sounds ${name} ogg ${REPGAME_SOUND_OGG_DIR}/${name}.ogg)
				list(APPEND REPGAME_BLOB_SOURCES ${blob})
			endif()
		endforeach()
	endif()
endif()

# ---- wasm preload fs -------------------------------------------------------
# Sounds can't be embedded via ld -b binary on wasm — they ship as files in
# the WASMFS preload and audio.cpp loads "sounds/<name>.ogg" at runtime.
# LINK_DEPENDS on every staged file so asset changes repackage.
set(REPGAME_WASM_FS_FILES)
if(REPGAME_BUILD_CLIENT AND REPGAME_PLATFORM STREQUAL "wasm")
	set(_fs ${CMAKE_BINARY_DIR}/fs)
	# Same bitmap set as the blob builds (bitmaps/*.png — icons/ is a subdir
	# and doesn't ship in the preload).
	file(GLOB bitmap_pngs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/bitmaps/*.png)
	repgame_preprocess_glsl(${_fs}/src/shaders wasm_glsl DEFINES -DREPGAME_WASM)
	repgame_preprocess_wgsl(${_fs}/src/shaders ${CMAKE_BINARY_DIR}/gen wasm_wgsl)
	list(APPEND REPGAME_WASM_FS_FILES ${wasm_glsl} ${wasm_wgsl})

	foreach(png ${bitmap_pngs})
		get_filename_component(name ${png} NAME_WE)
		set(out ${_fs}/bitmaps/${name}.bin)
		add_custom_command(OUTPUT ${out}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${_fs}/bitmaps
			COMMAND ${CMAKE_COMMAND} -E copy ${png} ${out}
			DEPENDS ${png}
			VERBATIM)
		list(APPEND REPGAME_WASM_FS_FILES ${out})
	endforeach()

	file(GLOB font_ttfs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/fonts/*.ttf)
	foreach(ttf ${font_ttfs})
		get_filename_component(name ${ttf} NAME)
		set(out ${_fs}/fonts/${name})
		add_custom_command(OUTPUT ${out}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${_fs}/fonts
			COMMAND ${CMAKE_COMMAND} -E copy ${ttf} ${out}
			DEPENDS ${ttf}
			VERBATIM)
		list(APPEND REPGAME_WASM_FS_FILES ${out})
	endforeach()

	if(REPGAME_AUDIO)
		file(STRINGS ${CMAKE_SOURCE_DIR}/tools/sounds_manifest.txt _sound_lines)
		foreach(line IN LISTS _sound_lines)
			if(line MATCHES "^([a-z][a-z0-9_]*)\\|")
				set(name ${CMAKE_MATCH_1})
				set(out ${_fs}/sounds/${name}.ogg)
				add_custom_command(OUTPUT ${out}
					COMMAND ${CMAKE_COMMAND} -E make_directory ${_fs}/sounds
					COMMAND ${CMAKE_COMMAND} -E copy ${REPGAME_SOUND_OGG_DIR}/${name}.ogg ${out}
					DEPENDS ${REPGAME_SOUND_OGG_DIR}/${name}.ogg
					VERBATIM)
				list(APPEND REPGAME_WASM_FS_FILES ${out})
			endif()
		endforeach()
	endif()
endif()

# ---- imgui (vendored: -Wall/-Wextra but not -Werror) -----------------------
if(REPGAME_BUILD_CLIENT)
	add_library(repgame_imgui OBJECT
		${REPGAME_IMGUI_CORE_SOURCES}
		${REPGAME_IMGUI_BACKEND})
	target_link_libraries(repgame_imgui PRIVATE repgame_flags repgame_platform)
	# Source-level options append after the interface -Werror, so this wins.
	set_source_files_properties(${REPGAME_IMGUI_CORE_SOURCES} ${REPGAME_IMGUI_BACKEND}
		PROPERTIES COMPILE_OPTIONS -Wno-error)
	if(REPGAME_PLATFORM STREQUAL "wasm")
		# imgui_impl_opengl3 auto-detects Emscripten as GLES2/WebGL1, but the
		# game creates a WebGL2/GLES3 context (SDL_GL_CONTEXT_MAJOR_VERSION=3,
		# MAX_WEBGL_VERSION=2), so force the ES3 path ("#version 300 es").
		set_source_files_properties(${REPGAME_IMGUI_CORE_SOURCES} ${REPGAME_IMGUI_BACKEND}
			PROPERTIES COMPILE_OPTIONS "-Wno-error;-DIMGUI_IMPL_OPENGL_ES3")
	endif()
endif()

# ---- client ---------------------------------------------------------------
if(REPGAME_BUILD_CLIENT)
	if(REPGAME_PLATFORM STREQUAL "wasm")
		file(GLOB platform_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/wasm/*.cpp)
	elseif(REPGAME_PLATFORM STREQUAL "windows")
		file(GLOB platform_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/windows/*.cpp)
	else()
		file(GLOB platform_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/linux/*.cpp)
	endif()
	add_executable(RepGame
		${REPGAME_COMMON_SOURCES}
		${platform_srcs}
		${REPGAME_BLOB_SOURCES}
		$<TARGET_OBJECTS:repgame_imgui>)
	if(REPGAME_AUDIO)
		target_sources(RepGame PRIVATE ${SOUNDS_MANIFEST_HPP})
	endif()
	# Build the preprocessed shaders before the .S blobs that .incbin them.
	if(shader_processed)
		set_source_files_properties(${shader_processed} PROPERTIES HEADER_FILE_ONLY TRUE)
		target_sources(RepGame PRIVATE ${shader_processed})
	endif()

	target_link_libraries(RepGame PRIVATE repgame_flags repgame_platform)

	if(REPGAME_PLATFORM STREQUAL "linux")
		if(REPGAME_USE_LLD)
			target_link_options(RepGame PRIVATE -fuse-ld=lld)
		endif()
		# ThinLTO cache: a one-file change re-optimizes only that module at
		# link instead of the whole binary. Lives in the build dir so
		# clean-linux wipes it. Clang+lld only — gcc/GNU ld (e.g. flatpak)
		# just doesn't get a cache.
		if(REPGAME_LTO AND REPGAME_USE_LLD AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			target_link_options(RepGame PRIVATE
				$<$<CONFIG:Release>:-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/thinlto-cache>
				$<$<CONFIG:Release>:-Wl,--thinlto-cache-policy=cache_size_bytes=2g>)
		endif()
		repgame_gpu_link(RepGame)
		# Link line kept literal from linux.mk; LTO comes from REPGAME_LTO /
		# CMAKE_INTERPROCEDURAL_OPTIMIZATION (-flto=thin on compile+link).
		target_link_libraries(RepGame PRIVATE
			-lSDL3 -Wl,-Bstatic -lGLEW -Wl,-Bdynamic -lGL -lpthread -lm -ldl
			-lwayland-client)
		# Sanitizer runtimes need the shared C++ runtime; the default builds
		# keep the static link.
		if(NOT REPGAME_SANITIZER)
			target_link_libraries(RepGame PRIVATE -static-libgcc -static-libstdc++)
		endif()
	elseif(REPGAME_PLATFORM STREQUAL "arm")
		# The old arm.mk's link-only -flto was a no-op (objects were never
		# LTO-compiled) and is dropped here.
		target_link_options(RepGame PRIVATE -fuse-ld=lld)
		target_link_libraries(RepGame PRIVATE
			${REPGAME_SDL3_PREFIX}/lib/libSDL3.a -lGLESv2 -lwayland-client
			-lpthread -lm -ldl -static-libgcc -static-libstdc++)
	elseif(REPGAME_PLATFORM STREQUAL "steamframe")
		# SDL3 loads most X11/Wayland extension libs via dlopen at runtime;
		# -lX11 and -lwayland-client cover the core entry points, all present
		# in sniper. A static libSDL3.a resolves its video backends at link
		# time (SDL_*_SHARED ended up OFF), so the Wayland/X11 support libs
		# it recorded must be linked too — all present in the sniper runtime.
		target_link_options(RepGame PRIVATE -fuse-ld=lld)
		target_link_libraries(RepGame PRIVATE
			${REPGAME_SDL3_PREFIX}/lib/libSDL3.a -lGLESv2 -lEGL
			-lwayland-client -lwayland-cursor -lwayland-egl -lxkbcommon
			-lX11 -lXext -lXcursor -lXi -lXfixes -lXrandr -lXss -lXtst
			-lpulse
			-lpthread -lm -ldl -static-libgcc -static-libstdc++)
	elseif(REPGAME_PLATFORM STREQUAL "windows")
		target_link_libraries(RepGame PRIVATE
			${CMAKE_SOURCE_DIR}/windows_build/glew/lib/libglew32.a
			${CMAKE_SOURCE_DIR}/windows_build/sdl3/x86_64-w64-mingw32/lib/libSDL3.dll.a
			-Wl,--no-undefined -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va
			-lm -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32
			-lsetupapi -lversion -luuid
			-lopengl32 -Wl,-Bstatic -lpthread -Wl,-Bdynamic
			-static-libgcc -static-libstdc++)
		# SDL3 is dynamically linked on Windows, so ship SDL3.dll next to the exe.
		add_custom_command(TARGET RepGame POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
				${CMAKE_SOURCE_DIR}/windows_build/sdl3/x86_64-w64-mingw32/bin/SDL3.dll
				${REPGAME_OUTPUT_DIR}/
			VERBATIM)
	elseif(REPGAME_PLATFORM STREQUAL "wasm")
		set_target_properties(RepGame PROPERTIES SUFFIX ".js")
		# SHELL: keeps each `-s X=Y` pair together — CMake would otherwise
		# de-duplicate the repeated `-s` tokens in the link line.
		target_link_options(RepGame PRIVATE
			"SHELL:-s WASMFS=1"
			"SHELL:-s USE_SDL=2"
			--use-port=emdawnwebgpu
			"SHELL:-s MAX_WEBGL_VERSION=2"
			"SHELL:-s ASSERTIONS=1"
			"SHELL:-s EXPORTED_FUNCTIONS='[\"_main\", \"_repgame_wasm_start_storage\", \"_repgame_wasm_request_exit\", \"_repgame_wasm_request_menu\", \"_repgame_wasm_pointer_lock_result\", \"_repgame_wasm_set_server\", \"_repgame_main_probe_get\"]'"
			-lwebsocket.js
			"SHELL:-s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\"]'"
			-pthread
			"SHELL:-s PTHREAD_POOL_SIZE=navigator.hardwareConcurrency"
			# INITIAL_MEMORY=3968MB (wasm4gb): the default 2GB cap was hit
			# while loading chunks — WebGPU mapgen fills the loader faster so
			# more chunks are resident (observed demand ~3.77GB). Preallocate
			# rather than grow: with pthreads, ALLOW_MEMORY_GROWTH makes a
			# heap grow sync-park every worker on a futex, which deadlocks
			# against a worker blocked in pthread_cond_wait (observed as a
			# full-page freeze when the UI allocated while a worker waited on
			# a WebGPU job); and the alternative — GROWABLE_ARRAYBUFFERS
			# resizable SharedArrayBuffers — is rejected by Chrome's WebGL
			# entry points (bufferSubData et al. refuse resizable-buffer
			# views). A fixed SharedArrayBuffer commits lazily and sidesteps
			# both. Stay under 4GB: exactly 4GB wraps wasm32 heap accounting
			# (arena computes to ~0 -> instant OOM). >2GB needs wasm4gb,
			# supported by modern Chrome/Safari and Firefox 154+.
			"SHELL:-s INITIAL_MEMORY=3968MB"
			# Default STACK_SIZE is 64KB. emdawnwebgpu delivers JS
			# error/diagnostic strings to wasm callbacks via
			# stringToUTF8OnStack — a large Dawn message (validation errors
			# embed indented context) overflows a small stack and shows up as
			# a stack-cookie smash. 8MB covers arbitrarily large messages.
			"SHELL:-s STACK_SIZE=8MB"
			--preload-file ${CMAKE_BINARY_DIR}/fs@)
		set_target_properties(RepGame PROPERTIES LINK_DEPENDS "${REPGAME_WASM_FS_FILES}")
		target_sources(RepGame PRIVATE ${REPGAME_WASM_FS_FILES})
		# src/wasm/input.cpp is just #include "../linux/input.cpp" — the
		# compiler depfile covers it.
	endif()

	if(REPGAME_OPENXR)
		target_compile_definitions(RepGame PRIVATE REPGAME_OPENXR)
		target_include_directories(RepGame PRIVATE ${REPGAME_OPENXR_PREFIX}/include)
		if(REPGAME_PLATFORM STREQUAL "steamframe")
			target_link_libraries(RepGame PRIVATE ${REPGAME_OPENXR_PREFIX}/lib/libopenxr_loader.a)
		else()
			target_link_libraries(RepGame PRIVATE ${REPGAME_OPENXR_PREFIX}/lib/libopenxr_loader.a -lEGL)
		endif()
	endif()

	set_target_properties(RepGame PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ${REPGAME_OUTPUT_DIR})
endif()

# ---- server (linux only) ---------------------------------------------------
if(REPGAME_BUILD_SERVER)
	file(GLOB server_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/server/*.cpp)
	# Exactly the common objects the old server.mk linked from the debug tree.
	set(server_common
		src/common/utils/file_utils.cpp
		src/common/utils/map_storage.cpp
		src/common/utils/player_data.cpp
		src/common/chunk_buffer_pool.cpp
		src/common/furnace.cpp
		src/common/net/packet.cpp
		src/common/net/framed_socket.cpp
		src/common/smelting.cpp)
	add_executable(RepGameServer ${server_srcs} ${server_common})
	target_link_libraries(RepGameServer PRIVATE repgame_flags repgame_platform)
	set_target_properties(RepGameServer PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out/server)
endif()
