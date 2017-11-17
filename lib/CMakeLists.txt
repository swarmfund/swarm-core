cmake_minimum_required(VERSION 3.2.2)

project(libs)

add_definitions(
        -DHAVE_CONFIG
        -DASIO_SEPARATE_COMPILATION=1
        -DSQLITE_OMIT_LOAD_EXTENSION=1
)

include_directories(../)
include_directories(sqlite)
include_directories(../src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(asio/include)
include_directories(soci/src/core)
include_directories(soci/src/backends/sqlite3)
include_directories(soci/src/backends/postgresql)
include_directories(libmedida/src)
include_directories(util)

message(${PostgreSQL_INCLUDE_DIRS})
include_directories(${PostgreSQL_INCLUDE_DIRS})

set(SOURCE_LIB3RDPARTY
        util/basen.h
        util/cpptoml.h
        util/crc16.cpp
        util/crc16.h
        util/easylogging++.h
        util/format.cc
        util/format.h
        util/getopt.h
        util/getopt_long.c
        util/lrucache.hpp
        util/uint128_t.cpp
        util/uint128_t.h
        http/connection.cpp
        http/connection.hpp
        http/connection_manager.cpp
        http/connection_manager.hpp
        http/header.hpp
        http/HttpClient.cpp
        http/HttpClient.h
        http/reply.cpp
        http/reply.hpp
        http/request.hpp
        http/request_parser.cpp
        http/request_parser.hpp
        http/server.cpp
        http/server.hpp
        asio/src/asio.cpp
        json/json-forwards.h
        json/json.h
        json/jsoncpp.cpp
        sqlite/sqlite3.c
        sqlite/sqlite3.h
        )

set(SOURCE_LIBMEDIDA
        libmedida/src/medida/counter.cc
        libmedida/src/medida/counter.h
        libmedida/src/medida/histogram.cc
        libmedida/src/medida/histogram.h
        libmedida/src/medida/medida.h
        libmedida/src/medida/meter.cc
        libmedida/src/medida/meter.h
        libmedida/src/medida/metered_interface.h
        libmedida/src/medida/metric_interface.h
        libmedida/src/medida/metric_name.cc
        libmedida/src/medida/metric_name.h
        libmedida/src/medida/metric_processor.cc
        libmedida/src/medida/metric_processor.h
        libmedida/src/medida/metrics_registry.cc
        libmedida/src/medida/metrics_registry.h
        libmedida/src/medida/reporting/abstract_polling_reporter.cc
        libmedida/src/medida/reporting/abstract_polling_reporter.h
        libmedida/src/medida/reporting/collectd_reporter.cc
        libmedida/src/medida/reporting/collectd_reporter.h
        libmedida/src/medida/reporting/console_reporter.cc
        libmedida/src/medida/reporting/console_reporter.h
        libmedida/src/medida/reporting/json_reporter.cc
        libmedida/src/medida/reporting/json_reporter.h
        libmedida/src/medida/reporting/util.cc
        libmedida/src/medida/reporting/util.h
        libmedida/src/medida/sampling_interface.h
        libmedida/src/medida/stats/ewma.cc
        libmedida/src/medida/stats/ewma.h
        libmedida/src/medida/stats/exp_decay_sample.cc
        libmedida/src/medida/stats/exp_decay_sample.h
        libmedida/src/medida/stats/sample.h
        libmedida/src/medida/stats/snapshot.cc
        libmedida/src/medida/stats/snapshot.h
        libmedida/src/medida/stats/uniform_sample.cc
        libmedida/src/medida/stats/uniform_sample.h
        libmedida/src/medida/summarizable_interface.h
        libmedida/src/medida/timer.cc
        libmedida/src/medida/timer.h
        libmedida/src/medida/timer_context.cc
        libmedida/src/medida/timer_context.h
        libmedida/src/medida/types.h
        )

set(SOURCE_LIBSOCI
        soci/src/backends/postgresql/blob.cpp
        soci/src/backends/postgresql/common.cpp
        soci/src/backends/postgresql/common.h
        soci/src/backends/postgresql/error.cpp
        soci/src/backends/postgresql/factory.cpp
        soci/src/backends/postgresql/row-id.cpp
        soci/src/backends/postgresql/session.cpp
        soci/src/backends/postgresql/soci-postgresql.h
        soci/src/backends/postgresql/standard-into-type.cpp
        soci/src/backends/postgresql/standard-use-type.cpp
        soci/src/backends/postgresql/statement.cpp
        soci/src/backends/postgresql/vector-into-type.cpp
        soci/src/backends/postgresql/vector-use-type.cpp
        soci/src/backends/sqlite3/blob.cpp
        soci/src/backends/sqlite3/common.cpp
        soci/src/backends/sqlite3/common.h
        soci/src/backends/sqlite3/factory.cpp
        soci/src/backends/sqlite3/row-id.cpp
        soci/src/backends/sqlite3/session.cpp
        soci/src/backends/sqlite3/soci-sqlite3.h
        soci/src/backends/sqlite3/standard-into-type.cpp
        soci/src/backends/sqlite3/standard-use-type.cpp
        soci/src/backends/sqlite3/statement.cpp
        soci/src/backends/sqlite3/vector-into-type.cpp
        soci/src/backends/sqlite3/vector-use-type.cpp
        soci/src/core/backend-loader.cpp
        soci/src/core/backend-loader.h
        soci/src/core/blob-exchange.h
        soci/src/core/blob.cpp
        soci/src/core/blob.h
        soci/src/core/connection-parameters.cpp
        soci/src/core/connection-parameters.h
        soci/src/core/connection-pool.cpp
        soci/src/core/connection-pool.h
        soci/src/core/error.cpp
        soci/src/core/error.h
        soci/src/core/exchange-traits.h
        soci/src/core/into-type.cpp
        soci/src/core/into-type.h
        soci/src/core/into.h
        soci/src/core/once-temp-type.cpp
        soci/src/core/once-temp-type.h
        soci/src/core/prepare-temp-type.cpp
        soci/src/core/prepare-temp-type.h
        soci/src/core/procedure.cpp
        soci/src/core/procedure.h
        soci/src/core/query_transformation.h
        soci/src/core/ref-counted-prepare-info.cpp
        soci/src/core/ref-counted-prepare-info.h
        soci/src/core/ref-counted-statement.cpp
        soci/src/core/ref-counted-statement.h
        soci/src/core/row-exchange.h
        soci/src/core/row.cpp
        soci/src/core/row.h
        soci/src/core/rowid-exchange.h
        soci/src/core/rowid.cpp
        soci/src/core/rowid.h
        soci/src/core/rowset.h
        soci/src/core/session.cpp
        soci/src/core/session.h
        soci/src/core/soci-backend.h
        soci/src/core/soci-config.h
        soci/src/core/soci-platform.h
        soci/src/core/soci-simple.cpp
        soci/src/core/soci-simple.h
        soci/src/core/soci.h
        soci/src/core/soci_backends_config.h
        soci/src/core/statement.cpp
        soci/src/core/statement.h
        soci/src/core/transaction.cpp
        soci/src/core/transaction.h
        soci/src/core/type-conversion-traits.h
        soci/src/core/type-conversion.h
        soci/src/core/type-holder.h
        soci/src/core/type-ptr.h
        soci/src/core/unsigned-types.h
        soci/src/core/use-type.cpp
        soci/src/core/use-type.h
        soci/src/core/use.h
        soci/src/core/values-exchange.h
        soci/src/core/values.cpp
        soci/src/core/values.h
        soci/src/core/version.h
        )

if(${CMAKE_COMPILER_IS_GNUCXX})
    set(COMMON_FLAGS_WARNINGS "-Wall -Wno-unused-command-line-argument -Wno-unused-local-typedef -Wno-unknown-warning-option")
    set(COMMON_FLAGS_CXX_CONF "-std=c++11  -pthread -Wall -Wno-unused-command-line-argument -Wno-unused-local-typedef -Wno-unknown-warning-option -Werror=unused-result")
endif()


if((${CMAKE_COMPILER_IS_GNUCXX}) OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    set(CMAKE_C_FLAGS_DEBUG "-g -O0 ${COMMON_FLAGS_WARNINGS}")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -pthread ${COMMON_FLAGS_CXX_CONF}")
    #For Release
    set(CMAKE_C_FLAGS_RELEASE "-g -O2 ${COMMON_FLAGS_WARNINGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "-g -O2 -pthread ${COMMON_FLAGS_CXX_CONF}")
    #For MaxOptz
    set(CMAKE_C_FLAGS_MAXOPTZ "-g -O3 ${COMMON_FLAGS_WARNINGS}")
    set(CMAKE_CXX_FLAGS_MAXOPTZ "-g -O3 -pthread ${COMMON_FLAGS_CXX_CONF}")
endif()

add_library(soci STATIC ${SOURCE_LIBSOCI})
add_library(3rdparty STATIC ${SOURCE_LIB3RDPARTY})
add_library(medida STATIC ${SOURCE_LIBMEDIDA})

message(INFO "PostgreSQL_LIBRARIES: ${PostgreSQL_LIBRARIES}")

if(${CMAKE_COMPILER_IS_GNUCXX})
    target_link_libraries(soci ${PostgreSQL_LIBRARIES})
endif()