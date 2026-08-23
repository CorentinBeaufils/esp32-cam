# TP-P1c : affichage.
#
# La BIBLIOTHÈQUE display (LatestFrame) est PURE (aucun OpenCV) -> testable seule,
# y compris sous ThreadSanitizer. Seul l'EXÉCUTABLE (le visualiseur) dépend
# d'OpenCV.
#
#   -DDISP_USE_SOLUTION=ON  -> compile la lib contre le corrigé.
option(DISP_USE_SOLUTION "Compiler l'affichage contre le corrige" OFF)

if(DISP_USE_SOLUTION)
  set(DISP_SOURCES solution/latest_frame.cpp)
else()
  set(DISP_SOURCES src/latest_frame.cpp)
endif()

add_library(display STATIC ${DISP_SOURCES})
target_include_directories(display PUBLIC include)
target_link_libraries(display
  PUBLIC  common          # cam::Frame
  PRIVATE projet_warnings Threads::Threads
)

# Le visualiseur : réseau (receiver) + OpenCV. Construit seulement si OpenCV est
# présent (sudo apt install libopencv-dev).
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
