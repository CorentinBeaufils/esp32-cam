# Shared protocol library: fragmentation + reassembly.
# Pure logic (no asio, no OpenCV) -> reusable everywhere, testable alone.
add_library(common STATIC
  src/protocol.cpp
  src/reassembler.cpp
)
target_include_directories(common PUBLIC include)
target_link_libraries(common PRIVATE projet_warnings)
