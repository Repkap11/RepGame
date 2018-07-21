cmake_minimum_required(VERSION 3.4.1)

add_library( RepGameAndroid SHARED src/main/cpp/RepGameAndroid.cpp )
add_library( input SHARED src/main/cpp/input.cpp )
find_library( log-lib log )
include_directories(/usr/include/)
include_directories(../../include/)
target_link_libraries( RepGameAndroid input /home/paul/Software/RepGame/out/android/libRepGame.so  ${log-lib} GLESv3 )