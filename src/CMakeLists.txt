cmake_minimum_required(VERSION 3.8)

project(core)

#definitions for compilers
include(platform_definitions.cmake)

include(platform_include.cmake)
#path to .x
set(XDRS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/xdr)

#generate xdr files in path XDRS_PATH
include(gen_xdr.cmake)

#include generated files .h from .x
include_directories(${XDRS_PATH})

#include headers directory

set(NOT_COMMON_SOURCE_FILES
        transactions/CounterpartyDetails.h
        transactions/review_request/ReviewRequestHelper.cpp transactions/review_request/ReviewRequestHelper.h)

#find source files
file(GLOB STELLAR_CORE_SOURCE_FILES **/*.cpp **/**/*.cpp **/**/**/*.cpp)
#file(GLOB STELLAR_CORE_SOURCE_FILES_H **/*.h)

#create executeble file "core"
add_executable(core ${STELLAR_CORE_SOURCE_FILES} ../config.h ${XDRS_PATH} ${H_GENERATE_FILES} ${NOT_COMMON_SOURCE_FILES})

#link
include(platform_libs.cmake)