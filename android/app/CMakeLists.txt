cmake_minimum_required(VERSION 3.21)
project(RepGame CXX ASM)

find_library( log-lib log )
find_library( android-lib android )
include_directories(../../include/)
include_directories(../../entt_build/src/)

add_definitions(-DREPGAME_ANDROID -DGLM_FORCE_CXX17)
# Same warning set as the other platforms (NDK clang; c++20-designator covers
# the designated initializers used throughout).
add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-c++20-designator)
# Shared common source list (src/common/*.cpp + src/common/*/*.cpp — the same
# set as the old explicit one-level-subdir glob) and the blob/shader
# preprocessing helpers, both shared with the desktop builds via cmake/.
# CMAKE_SOURCE_DIR is android/app here, so the helpers root paths at
# REPGAME_ROOT internally.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/RepGameSources.cmake)
# CMP0116 OLD: hand the shader_pp gcc depfiles straight to Ninja. NEW makes
# the generated build run `cmake -E cmake_transform_depfile`, which fails
# under the Gradle-bundled cmake 3.22.1. Set before the include so the
# helper functions record it.
cmake_policy(SET CMP0116 OLD)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/RepGameBlobs.cmake)
file(GLOB REPGAME_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/android/*.cpp)
list(APPEND REPGAME_SOURCES ${REPGAME_COMMON_SOURCES})
#message(ERROR "REPGAME_SOURCES:${REPGAME_SOURCES}End")

# Shaders, bitmaps and fonts embed as .incbin blobs in the .so, like every
# other platform — no assets/ or res/raw copies. -DREPGAME_ANDROID is the
# same define the old make shader_pp used.
repgame_asset_blobs(ANDROID_BLOBS ANDROID_SHADER_PROCESSED DEFINES -DREPGAME_ANDROID)
set_source_files_properties(${ANDROID_SHADER_PROCESSED} PROPERTIES HEADER_FILE_ONLY TRUE)

# Same Clay carve-outs as the top-level CMakeLists: the vendored
# implementation trips warnings we don't patch out, and ui_layout.cpp's
# CLAY() declarations leave fields unmentioned on purpose. NDK clang
# reports the designated-init gaps as -Wmissing-field-initializers.
set_source_files_properties(${REPGAME_ROOT}/src/common/vendor/clay.cpp
	PROPERTIES COMPILE_OPTIONS -Wno-error)
set_source_files_properties(${REPGAME_ROOT}/src/common/ui_layout.cpp
	PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)

add_library( RepGameAndroid SHARED ${REPGAME_SOURCES} ${ANDROID_BLOBS} ${ANDROID_SHADER_PROCESSED} )

#target_link_libraries(RepGameAndroid input ${PaulSource} ${log-lib} ${android-lib} GLESv3 )
target_link_libraries(RepGameAndroid ${log-lib} ${android-lib} GLESv3 )
