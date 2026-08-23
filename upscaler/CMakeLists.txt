# TP-P3 : upscaling classique dans le budget temps réel.
#
# La BIBLIOTHÈQUE upscale (ScaleStats + UpscalePolicy) est PURE (aucun OpenCV) ->
# testable seule. Seul l'EXÉCUTABLE (le visualiseur) dépend d'OpenCV, comme au
# TP-P1c.
#
#   -DUP_USE_SOLUTION=ON  -> compile la lib contre le corrigé.
option(UP_USE_SOLUTION "Compiler l'upscaler contre le corrige" OFF)

if(UP_USE_SOLUTION)
  set(UP_SOURCES solution/scale_stats.cpp solution/upscale_policy.cpp)
else()
  set(UP_SOURCES src/scale_stats.cpp src/upscale_policy.cpp)
endif()

add_library(upscale STATIC ${UP_SOURCES})
target_include_directories(upscale PUBLIC include)
target_link_libraries(upscale PRIVATE projet_warnings)

# Le visualiseur avec upscaling : réseau (receiver) + handoff (display) + OpenCV.
# Construit seulement si OpenCV est présent.
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
