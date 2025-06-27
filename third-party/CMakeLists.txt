cmake_minimum_required(VERSION 3.10)

# default /W3 or /W4 for msvc at googletest
add_subdirectory(googletest)
add_subdirectory(thrift)

configure_third_party_compiler_flags()

add_subdirectory(berkeleydb)

# RocksDB configuration
set(WITH_TESTS OFF CACHE BOOL "Build RocksDB with tests")
set(WITH_TOOLS OFF CACHE BOOL "Build RocksDB with tools")
set(WITH_BENCHMARK_TOOLS OFF CACHE BOOL "Build RocksDB with benchmark tools")
set(WITH_CORE_TOOLS OFF CACHE BOOL "Build RocksDB with core tools")
set(WITH_GFLAGS OFF CACHE BOOL "Build RocksDB with gflags")
set(FAIL_ON_WARNINGS OFF CACHE BOOL "RocksDB fail on warnings")
set(PORTABLE ON CACHE BOOL "Build portable RocksDB binary")
set(ROCKSDB_BUILD_SHARED OFF CACHE BOOL "Build RocksDB shared library")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb/CMakeLists.txt)
    message(STATUS "Adding RocksDB subdirectory")
    add_subdirectory(rocksdb)
    # Verify the target was created
    if(TARGET rocksdb)
        message(STATUS "RocksDB target 'rocksdb' successfully created")
    else()
        message(WARNING "RocksDB subdirectory added but 'rocksdb' target not found")
    endif()
else()
    message(FATAL_ERROR "RocksDB CMakeLists.txt not found at ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb/CMakeLists.txt")
endif()

add_subdirectory(lz4)
add_subdirectory(rang)
add_subdirectory(nameof)

# Header-only libraries
add_library(websocketpp INTERFACE)
target_include_directories(websocketpp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/websocketpp)
add_library(websocketpp::websocketpp ALIAS websocketpp)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/json/single_include)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
