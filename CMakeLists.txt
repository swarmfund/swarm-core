cmake_minimum_required(VERSION 3.8)
project(stellar)

set(CMAKE_CXX_STANDARD 14)

add_subdirectory(lib)
add_subdirectory(src)
add_subdirectory(lib/libsodium)
add_subdirectory(lib/xdrpp)
add_subdirectory(lib/coincore)
add_subdirectory(lib/gtest)