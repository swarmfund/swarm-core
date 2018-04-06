cmake_minimum_required(VERSION 3.8)

project(core)

#definitions for compilers
include(platform_definitions.cmake)

#include headers directory
include(platform_include.cmake)

#path to .x
set(XDRS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/xdr)

#generate xdr files in path XDRS_PATH
include(gen_xdr.cmake)

#include generated files .h from .x
include_directories(${XDRS_PATH})

#include headers files without .cpp
set(NOT_COMMON_SOURCE_FILES
        util/asio.h
        util/make_unique.h
        util/must_use.h
        util/NonCopyable.h
        util/optional.h
        util/XDRStream.h
        test/test_marshaler.h
        work/WorkManager.h
        crypto/ByteSlice.h
        bucket/LedgerCmp.h
        bucket/BucketManager.h
        ledger/LedgerManager.h
        history/HistoryManager.h
        overlay/BanManager.h
        overlay/StellarXDR.h
        overlay/OverlayManager.h
        process/ProcessManager.h
        transactions/SourceDetails.h
        transactions/CounterpartyDetails.h
        transactions/test/test_helper/Account.h
        transactions/ComisssionHandler.cpp transactions/ComisssionHandler.h transactions/ComisssionHandler.cpp transactions/ComisssionHandler.h ledger/KeyValueEntryFrame.cpp ledger/KeyValueEntryFrame.h ledger/KeyValueHelper.cpp ledger/KeyValueHelper.h)

#find source files
file(GLOB STELLAR_CORE_SOURCE_FILES **/*.cpp **/**/*.cpp **/**/**/*.cpp)

#create executeble file "core"
add_executable(core ${STELLAR_CORE_SOURCE_FILES} ../config.h ${XDRS_PATH} ${H_GENERATE_FILES} ${NOT_COMMON_SOURCE_FILES} transactions/ComisssionHandler.cpp transactions/ComisssionHandler.h)

#link libs
include(platform_libs.cmake)