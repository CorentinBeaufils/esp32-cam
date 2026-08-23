# ---------------------------------------------------------------------------
# Dependances, via FetchContent (comme le projet asio-tp).
# Phase 0 : Catch2. Phase 1 : asio (reseau UDP). Phase 3 : OpenCV (systeme).
# ---------------------------------------------------------------------------
include(FetchContent)

# --- asio standalone (Phase 1) ---------------------------------------------
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG        asio-1-30-2
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asio)

add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
find_package(Threads REQUIRED)
target_link_libraries(asio INTERFACE Threads::Threads)
if(WIN32)
  target_link_libraries(asio INTERFACE ws2_32 wsock32)
endif()

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.6.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
