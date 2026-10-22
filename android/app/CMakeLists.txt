cmake_minimum_required(VERSION 3.4.1)
project(RepGame)

find_library( log-lib log )
find_library( android-lib android )
include_directories(../../include/glm/)
include_directories(../../include/)
include_directories(../../entt_build/src/)

add_definitions(-DREPGAME_ANDROID -DGLM_FORCE_CXX17)
# Same warning set as the other platforms (NDK clang; c++20-designator covers
# the designated initializers used throughout).
add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable -Wno-c++20-designator)
file(GLOB REPGAME_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/android/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/net/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/renderer/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/utils/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/vendor/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/block_update_events/*.cpp)
#message(ERROR "REPGAME_SOURCES:${REPGAME_SOURCES}End")
add_library( RepGameAndroid SHARED ${REPGAME_SOURCES} )

#target_link_libraries(RepGameAndroid input ${PaulSource} ${log-lib} ${android-lib} GLESv3 )
target_link_libraries(RepGameAndroid ${log-lib} ${android-lib} GLESv3 )