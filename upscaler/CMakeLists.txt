# Upscaling classique adaptatif : bibliothèque PURE (ScaleStats + UpscalePolicy),
# testable seule ; seul le visualiseur dépend d'OpenCV.
add_library(upscale STATIC src/scale_stats.cpp src/upscale_policy.cpp)
target_include_directories(upscale PUBLIC include)
target_link_libraries(upscale PRIVATE projet_warnings)

find_package(OpenCV QUIET)
if(OpenCV_FOUND)
  add_executable(viewer_up_app main.cpp)
  target_include_directories(viewer_up_app PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(viewer_up_app
    PRIVATE upscale display receiver ${OpenCV_LIBS} projet_warnings)
  set_target_properties(viewer_up_app PROPERTIES OUTPUT_NAME viewer_up)
  message(STATUS "OpenCV trouve -> cible 'viewer_up' (upscaling) activee")
else()
  message(STATUS "OpenCV absent -> cible 'viewer_up' desactivee "
                 "(la lib upscale et ses tests restent compiles).")
endif()
