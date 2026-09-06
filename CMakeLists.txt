cmake_minimum_required(VERSION 3.16)
project(esp32_cam LANGUAGES CXX)

# ---------------------------------------------------------------------------
# ESP32-CAM project (PC side).
#
# Phase 0: the common protocol (fragmentation / reassembly) -- pure logic, no
# external dependency other than Catch2 for the tests.
# The following phases will add asio (UDP network) and OpenCV (decoding,
# display, upscaling).
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

add_library(projet_warnings INTERFACE)
target_compile_options(projet_warnings INTERFACE -Wall -Wextra -Wpedantic)

include(cmake/Dependencies.cmake)

enable_testing()
include(Catch)

add_subdirectory(common)
add_subdirectory(simulator)
add_subdirectory(receiver)
add_subdirectory(display)
add_subdirectory(upscaler)
add_subdirectory(viewer_qt)
add_subdirectory(bench)
add_subdirectory(tests)

message(STATUS "")
message(STATUS "=====================================================")
message(STATUS " esp32-cam configured  (Phase 0: protocol)")
message(STATUS " Build:  cmake --build build")
message(STATUS " Test :  ctest --test-dir build --output-on-failure")
message(STATUS "=====================================================")
message(STATUS "")
