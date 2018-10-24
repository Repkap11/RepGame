cmake_minimum_required(VERSION 3.4.1)

find_library( log-lib log )
find_library( android-lib android )
include_directories(/usr/include/glm/)
include_directories(../../include/)

file(GLOB PAUL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/android/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/abstract/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/utils/*.cpp)
#message(ERROR "Paul_Sources:${PAUL_SOURCES}End")
add_library( RepGameAndroid SHARED ${PAUL_SOURCES} )

#target_link_libraries(RepGameAndroid input ${PaulSource} ${log-lib} ${android-lib} GLESv3 )
target_link_libraries(RepGameAndroid ${log-lib} ${android-lib} GLESv3 )