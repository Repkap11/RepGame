cmake_minimum_required(VERSION 3.4.1)

add_library( RepGameAndroid SHARED src/main/cpp/RepGameAndroid.cpp )
find_library( log-lib log )
include_directories(AFTER /usr/include/)
include_directories(../../include/)
target_link_libraries( RepGameAndroid GLESv3 ${log-lib} /home/paul/Software/RepGame/out/android/libRepGame.so)