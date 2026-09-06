# Display: LatestFrame (thread-safe handoff, pure, testable under ThreadSanitizer)
# + OpenCV viewer (executable, built only if OpenCV is present).
add_library(display STATIC src/latest_frame.cpp)
target_include_directories(display PUBLIC include)
target_link_libraries(display
  PUBLIC  common          # cam::Frame
  PRIVATE projet_warnings Threads::Threads
)

find_package(OpenCV QUIET)
if(OpenCV_FOUND)
  add_executable(viewer_app main.cpp)
  target_include_directories(viewer_app PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(viewer_app PRIVATE display receiver ${OpenCV_LIBS} projet_warnings)
  set_target_properties(viewer_app PROPERTIES OUTPUT_NAME viewer)
  message(STATUS "OpenCV found (${OpenCV_VERSION}) -> target 'viewer' enabled")
else()
  message(STATUS "OpenCV missing -> target 'viewer' disabled "
                 "(install libopencv-dev for display). The display lib "
                 "and its tests still build.")
endif()
