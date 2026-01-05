# TopNotchNotes Harness - C++23 Audio/Video Backend
# Requires CMake 3.28+ for proper C++23 module support

cmake_minimum_required(VERSION 3.28)

# Force Clang 18
set(CMAKE_C_COMPILER /usr/bin/clang-18)
set(CMAKE_CXX_COMPILER /usr/bin/clang++-18)

project(TopNotchHarness 
    VERSION 1.0.0
    DESCRIPTION "C++23 Audio/Video Hardware Orchestration Daemon"
    LANGUAGES CXX
)

# ============================================================================
# C++23 Modern Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable module scanning for C++20/23 modules
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Clang 18 Settings
# ============================================================================

add_compile_options(
    -stdlib=libc++
    -fexperimental-library
)
add_link_options(-stdlib=libc++ -lc++abi)

# Warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

# ============================================================================
# Dependencies
# ============================================================================

# miniaudio is header-only, included in our source
# PocketSphinx for transcription (optional)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(POCKETSPHINX pocketsphinx)
endif()

# Threads
find_package(Threads REQUIRED)

# ============================================================================
# Harness Library (Modules)
# ============================================================================

add_library(harness_modules)

target_sources(harness_modules
    PUBLIC
        FILE_SET CXX_MODULES FILES
            src/modules/harness.ixx
            src/modules/audio.ixx
            src/modules/transcribe.ixx
            src/modules/io.ixx
            src/modules/ringbuffer.ixx
            src/modules/telemetry.ixx
)

target_include_directories(harness_modules
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(harness_modules
    PUBLIC
        Threads::Threads
)

if(POCKETSPHINX_FOUND)
    target_compile_definitions(harness_modules PUBLIC HAS_POCKETSPHINX)
    target_include_directories(harness_modules PUBLIC ${POCKETSPHINX_INCLUDE_DIRS})
    target_link_libraries(harness_modules PUBLIC ${POCKETSPHINX_LIBRARIES})
endif()

# ============================================================================
# Main Executable
# ============================================================================

add_executable(harness src/main.cpp)

target_link_libraries(harness
    PRIVATE
        harness_modules
        Threads::Threads
)

# ============================================================================
# Tests
# ============================================================================

option(BUILD_TESTS "Build test suite" ON)

if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS harness
    RUNTIME DESTINATION bin
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "TopNotchNotes Harness Configuration:")
message(STATUS "  C++ Standard:     ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:         ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build Type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  PocketSphinx:     ${POCKETSPHINX_FOUND}")
message(STATUS "")
