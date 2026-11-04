# Single shared source list. No toolchain-specific logic here — per-platform
# files are added by the platform's CMakeLists/toolchain file. Included by the
# top-level CMakeLists and by android/app/CMakeLists.txt (CMake 3.10, whose
# CMAKE_SOURCE_DIR is android/app) — hence REPGAME_ROOT from
# CMAKE_CURRENT_LIST_DIR and the CONFIGURE_DEPENDS guard (needs 3.12+).
get_filename_component(REPGAME_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.12)
	set(_repgame_glob_depends CONFIGURE_DEPENDS)
else()
	set(_repgame_glob_depends)
endif()

# Same set as make's SRC_COMMON: src/common/*.cpp plus one level of
# subdirectories (utils, tests, renderer, net, game, vendor, ...).
file(GLOB REPGAME_COMMON_SOURCES ${_repgame_glob_depends}
	${REPGAME_ROOT}/src/common/*.cpp
	${REPGAME_ROOT}/src/common/*/*.cpp)

# imgui core + the OpenGL3 backend; each platform adds its own windowing
# backend (SDL3 on linux, SDL2 on wasm).
file(GLOB REPGAME_IMGUI_CORE_SOURCES ${_repgame_glob_depends}
	${REPGAME_ROOT}/imgui_build/imgui*.cpp)
list(APPEND REPGAME_IMGUI_CORE_SOURCES
	${REPGAME_ROOT}/imgui_build/backends/imgui_impl_opengl3.cpp)
