cmake_minimum_required(VERSION 3.8)
project(stellar)

set(CMAKE_CXX_STANDARD 11)

#set(SOURCE_FILES main.cpp)

add_subdirectory(lib)
add_subdirectory(src)
add_subdirectory(lib/libsodium)
add_subdirectory(lib/xdrpp)
#add_executable(stellar ${SOURCE_FILES})