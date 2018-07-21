cmake_minimum_required(VERSION 3.4.1)

add_library( RepGameAndroid SHARED ../../src/android/RepGameAndroid.cpp )
find_library( log-lib log )
include_directories(AFTER /usr/include/)
include_directories(../../include/)
target_link_libraries( RepGameAndroid GLESv3 ${log-lib} ../../out/android/libRepGame.so)