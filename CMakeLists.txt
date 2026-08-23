cmake_minimum_required(VERSION 3.16)
project(esp32_cam LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Projet ESP32-CAM (cote PC).
#
# Phase 0 : le protocole commun (fragmentation / reassemblage) -- logique pure,
# aucune dependance externe hormis Catch2 pour les tests.
# Les phases suivantes ajouteront asio (reseau UDP) et OpenCV (decodage,
# affichage, upscaling).
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
add_subdirectory(bench)
add_subdirectory(tests)

message(STATUS "")
message(STATUS "=====================================================")
message(STATUS " esp32-cam configure  (Phase 0 : protocole)")
message(STATUS " Compiler :  cmake --build build")
message(STATUS " Tester   :  ctest --test-dir build --output-on-failure")
message(STATUS "=====================================================")
message(STATUS "")
