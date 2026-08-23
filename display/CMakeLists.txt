# Affichage : LatestFrame (handoff thread-safe, pur, testable sous ThreadSanitizer)
# + visualiseur OpenCV (exécutable, construit seulement si OpenCV est présent).
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
  message(STATUS "OpenCV trouve (${OpenCV_VERSION}) -> cible 'viewer' activee")
else()
  message(STATUS "OpenCV absent -> cible 'viewer' desactivee "
                 "(installe libopencv-dev pour l'affichage). La lib display "
                 "et ses tests restent compiles.")
endif()
